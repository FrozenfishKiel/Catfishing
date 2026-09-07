#include "Fishing/Presentation/CatRodBendComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Presentation/CatRodBendCurve.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerState.h"
#include "StaticMeshResources.h"

namespace
{
	FProcMeshVertex Midpoint(const FProcMeshVertex& A, const FProcMeshVertex& B)
	{
		FProcMeshVertex V;
		V.Position = (A.Position + B.Position) * 0.5;
		V.Normal = (A.Normal + B.Normal).GetSafeNormal();
		V.Tangent = FProcMeshTangent((A.Tangent.TangentX + B.Tangent.TangentX).GetSafeNormal(), A.Tangent.bFlipTangentY);
		V.Color = FMath::Lerp(FLinearColor(A.Color), FLinearColor(B.Color), 0.5f).ToFColor(true);
		V.UV0 = (A.UV0 + B.UV0) * 0.5;
		V.UV1 = (A.UV1 + B.UV1) * 0.5;
		V.UV2 = (A.UV2 + B.UV2) * 0.5;
		V.UV3 = (A.UV3 + B.UV3) * 0.5;
		return V;
	}

	// Split long axial edges once at initialization. Preserve UV seams, material sections, winding and vertex colour.
	bool Subdivide(FProcMeshSection& Section, const FVector& Axis, const FVector& Base, const double MaxSpan)
	{
		TArray<FIntVector> Pending;
		for (int32 I = 0; I < Section.ProcIndexBuffer.Num(); I += 3)
			Pending.Emplace(Section.ProcIndexBuffer[I], Section.ProcIndexBuffer[I + 1], Section.ProcIndexBuffer[I + 2]);
		Section.ProcIndexBuffer.Reset();
		TMap<uint64, int32> Midpoints;
		while (!Pending.IsEmpty())
		{
			const FIntVector Tri = Pending.Pop(EAllowShrinking::No);
			const int32 Indices[] = {Tri.X, Tri.Y, Tri.Z};
			double Along[3];
			for (int32 I = 0; I < 3; ++I)
				Along[I] = FVector::DotProduct(Section.ProcVertexBuffer[Indices[I]].Position - Base, Axis);
			int32 Edge = 0;
			for (int32 I = 1; I < 3; ++I)
				if (FMath::Abs(Along[I] - Along[(I + 1) % 3]) > FMath::Abs(Along[Edge] - Along[(Edge + 1) % 3])) Edge = I;
			if (FMath::Max3(Along[0], Along[1], Along[2]) <= 0.0
				|| FMath::Abs(Along[Edge] - Along[(Edge + 1) % 3]) <= MaxSpan)
			{
				Section.ProcIndexBuffer.Append({static_cast<uint32>(Tri.X), static_cast<uint32>(Tri.Y), static_cast<uint32>(Tri.Z)});
				continue;
			}
			if (Section.ProcVertexBuffer.Num() >= 100000 || Section.ProcIndexBuffer.Num() >= 600000) return false;
			const int32 A = Indices[Edge], B = Indices[(Edge + 1) % 3], C = Indices[(Edge + 2) % 3];
			const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
			int32 M;
			if (const int32* Existing = Midpoints.Find(Key)) M = *Existing;
			else
			{
				const FProcMeshVertex Vertex = Midpoint(Section.ProcVertexBuffer[A], Section.ProcVertexBuffer[B]);
				M = Section.ProcVertexBuffer.Add(Vertex);
				Midpoints.Add(Key, M);
			}
			Pending.Emplace(A, M, C);
			Pending.Emplace(M, B, C);
		}
		return true;
	}
}

UCatRodBendComponent::UCatRodBendComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

void UCatRodBendComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) SetComponentTickEnabled(false);
	else InitializeVisual();
}

void UCatRodBendComponent::RestoreSource()
{
	if (SourceMesh.IsValid() && !RestSections.IsEmpty())
	{
		SourceMesh->SetVisibility(bSourceWasVisible, false);
		SourceMesh->SetHiddenInGame(bSourceWasHidden, false);
		if (TipMarker.IsValid()) TipMarker->SetWorldLocation(SourceMesh->GetComponentTransform().TransformPosition(RestTip));
	}
	RestSections.Reset();
	SourceMaterialIndices.Reset();
	ClearAllMeshSections();
	SmoothedBend = FVector::ZeroVector;
}

void UCatRodBendComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreSource();
	LoadHook.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UCatRodBendComponent::InitializeVisual()
{
	if (!GetOwner() || GetNetMode() == NM_DedicatedServer) return false;
	if (SourceMesh.IsValid() && TipMarker.IsValid() && BuiltMesh == SourceMesh->GetStaticMesh() && IsVisualReady()) return true;
	RestoreSource();
	SourceMesh.Reset();
	TipMarker.Reset();
	TInlineComponentArray<USceneComponent*> Components(GetOwner());
	for (USceneComponent* Component : Components)
	{
		if (Component->ComponentHasTag(TEXT("RodBendSource"))) SourceMesh = Cast<UStaticMeshComponent>(Component);
		if (Component->ComponentHasTag(TEXT("RodTipMarker"))) TipMarker = Component;
	}
	// Unconfigured native rods and other skins keep their existing visual contract.
	if (!SourceMesh.IsValid()) return false;
	const auto Fail = [this](const TCHAR* Reason)
	{
		if (!bFailureLogged) LogVisualEvent(TEXT("fishing_rod_bend_rejected"), Reason, 0.0);
		bFailureLogged = true;
		RestoreSource();
		return false;
	};
	UStaticMesh* Mesh = SourceMesh->GetStaticMesh();
	if (!TipMarker.IsValid() || !Mesh || !Mesh->bAllowCPUAccess) return Fail(TEXT("MissingMarkerOrCPUReadableMesh"));
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.IsEmpty()) return Fail(TEXT("MissingRenderData"));
	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	BendAxis = Settings->RodBendAxisLocal.GetSafeNormal();
	if (BendAxis.ContainsNaN() || BendAxis.IsNearlyZero() || !FMath::IsFinite(Settings->RodBendRigidFraction)
		|| Settings->RodBendRigidFraction < 0.0 || Settings->RodBendRigidFraction > 0.9)
		return Fail(TEXT("InvalidBendGeometrySettings"));
	const FTransform SourceTransform = SourceMesh->GetComponentTransform();
	RestTip = SourceTransform.InverseTransformPosition(TipMarker->GetComponentLocation());
	double MinimumAlong = UE_DOUBLE_BIG_NUMBER;
	for (int32 I = 0; I < LOD.GetNumVertices(); ++I)
		MinimumAlong = FMath::Min(MinimumAlong, FVector::DotProduct(FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(I)), BendAxis));
	const double Length = (FVector::DotProduct(RestTip, BendAxis) - MinimumAlong) * (1.0 - Settings->RodBendRigidFraction);
	if (!FMath::IsFinite(Length) || Length < 1.0) return Fail(TEXT("InvalidFlexibleSpan"));
	BendBase = RestTip - BendAxis * Length;
	bSourceWasVisible = SourceMesh->IsVisible();
	bSourceWasHidden = SourceMesh->bHiddenInGame;
	AttachToComponent(SourceMesh.Get(), FAttachmentTransformRules::SnapToTargetIncludingScale);
	SetRelativeTransform(FTransform::Identity);
	const FIndexArrayView IndexBuffer = LOD.IndexBuffer.GetArrayView();
	for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& Input = LOD.Sections[SectionIndex];
		FProcMeshSection Rest;
		TMap<uint32, int32> VertexMap;
		for (uint32 I = Input.FirstIndex; I < Input.FirstIndex + Input.NumTriangles * 3; ++I)
		{
			const uint32 SourceIndex = IndexBuffer[I];
			int32* Found = VertexMap.Find(SourceIndex);
			int32 Index;
			if (Found) Index = *Found;
			else
			{
				FProcMeshVertex V;
				V.Position = FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(SourceIndex));
				const auto& Vertices = LOD.VertexBuffers.StaticMeshVertexBuffer;
				V.Normal = FVector(Vertices.VertexTangentZ(SourceIndex));
				const FVector X(Vertices.VertexTangentX(SourceIndex)), Y(Vertices.VertexTangentY(SourceIndex));
				V.Tangent = FProcMeshTangent(X, FVector::DotProduct(FVector::CrossProduct(V.Normal, X), Y) < 0.0);
				FVector2D* UVs[] = {&V.UV0, &V.UV1, &V.UV2, &V.UV3};
				for (uint32 UV = 0; UV < FMath::Min(Vertices.GetNumTexCoords(), 4u); ++UV)
					*UVs[UV] = FVector2D(Vertices.GetVertexUV(SourceIndex, UV));
				if (LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices() > SourceIndex)
					V.Color = LOD.VertexBuffers.ColorVertexBuffer.VertexColor(SourceIndex);
				Index = Rest.ProcVertexBuffer.Add(V);
				VertexMap.Add(SourceIndex, Index);
			}
			Rest.ProcIndexBuffer.Add(Index);
		}
		if (!Subdivide(Rest, BendAxis, BendBase, Length / FCatRodBendCurve::Segments)) return Fail(TEXT("SubdivisionBudgetExceeded"));
		Rest.SectionLocalBox.Init();
		for (const FProcMeshVertex& Vertex : Rest.ProcVertexBuffer) Rest.SectionLocalBox += Vertex.Position;
		Rest.bEnableCollision = false;
		SetProcMeshSection(SectionIndex, Rest);
		SetMaterial(SectionIndex, SourceMesh->GetMaterial(Input.MaterialIndex));
		SourceMaterialIndices.Add(Input.MaterialIndex);
		RestSections.Add(MoveTemp(Rest));
	}
	if (RestSections.IsEmpty()) return Fail(TEXT("EmptyMesh"));
	BuiltMesh = Mesh;
	SetCastShadow(SourceMesh->CastShadow);
	SetVisibility(bSourceWasVisible, false);
	SetHiddenInGame(bSourceWasHidden, false);
	SourceMesh->SetVisibility(false, false);
	SourceMesh->SetHiddenInGame(true, false);
	bFailureLogged = false;
	LastUpdateSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	LogVisualEvent(TEXT("fishing_rod_bend_ready"), TEXT("ExistingMeshReused"), 0.0);
	return true;
}

void UCatRodBendComponent::BindHook(ACatFishingHookActor* Hook)
{
	if (Hook && Hook->GetOwner() == GetOwner() && LoadHook != Hook)
	{
		LoadHook = Hook;
		DiagnosticSessionId = Hook->GetPresentationState().FishingSessionId;
		LogVisualEvent(TEXT("fishing_rod_bend_bound"), TEXT("HookResolved"), Hook->GetPresentationState().LineTensionNewtons);
	}
}

FVector UCatRodBendComponent::GetRestTipWorld() const
{
	return SourceMesh.IsValid() ? SourceMesh->GetComponentTransform().TransformPosition(RestTip) : FVector::ZeroVector;
}

void UCatRodBendComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshVisual();
}

void UCatRodBendComponent::RefreshVisual()
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer || !InitializeVisual()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const double Delta = LastUpdateSeconds >= 0.0 ? FMath::Max(0.0, Now - LastUpdateSeconds) : 0.0;
	LastUpdateSeconds = Now;
	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	FVector Force = FVector::ZeroVector;
	double Tension = 0.0;
	const ACatFishingRodActor* Rod = Cast<ACatFishingRodActor>(GetOwner());
	const ACatFishingHookActor* Hook = LoadHook.Get();
	if (Hook && Hook->GetPresentationState().FishingSessionId.IsValid()) DiagnosticSessionId = Hook->GetPresentationState().FishingSessionId;
	if (Rod && Hook && Hook->GetOwner() == Rod && !Hook->IsActorBeingDestroyed()
		&& !Rod->GetPresentationState().bBroken && Rod->GetPresentationState().bDeployed
		&& Hook->GetPresentationState().Phase == ECatFishingHookPresentationPhase::Landed
		&& Hook->GetPresentationState().bLineTaut)
	{
		Tension = Hook->GetPresentationState().LineTensionNewtons;
		// Use the rest tip for the force direction: cosmetic deformation must not feed back into the load.
		const FVector RestWorldTip = SourceMesh->GetComponentTransform().TransformPosition(RestTip);
		const FVector WorldForce = (Hook->GetActorLocation() - RestWorldTip).GetSafeNormal() * Tension;
		Force = SourceMesh->GetComponentTransform().InverseTransformVectorNoScale(WorldForce);
	}
	const FVector Target = FCatRodBendCurve::TargetBend(BendAxis, Force, Settings->RodBendReferenceForceNewtons,
		FMath::DegreesToRadians(Settings->RodBendMaximumAngleDegrees));
	const FVector Previous = SmoothedBend;
	const double Response = FMath::IsFinite(Settings->RodBendResponseSeconds) ? Settings->RodBendResponseSeconds : 0.15;
	SmoothedBend = FCatRodBendCurve::SmoothBend(SmoothedBend, Target, Delta, Response);
	if (Target.IsNearlyZero() && SmoothedBend.IsNearlyZero(0.00001)) SmoothedBend = FVector::ZeroVector;
	const bool bLoaded = Tension > UE_DOUBLE_SMALL_NUMBER;
	if (bLoaded != bWasLoaded || (bLoaded && Now >= NextDiagnosticSeconds))
	{
		LogVisualEvent(TEXT("fishing_rod_bend_sample"), bLoaded ? TEXT("Loaded") : TEXT("Released"), Tension);
		bWasLoaded = bLoaded;
		NextDiagnosticSeconds = Now + 2.0;
	}
	FCatRodBendCurve Curve;
	if (!Curve.Initialize(BendBase, RestTip, SmoothedBend)) return;
	// Parent transforms may have moved even when the load is unchanged.
	TipMarker->SetWorldLocation(SourceMesh->GetComponentTransform().TransformPosition(Curve.DeformPosition(RestTip)));
	SourceMesh->SetVisibility(false, false);
	SourceMesh->SetHiddenInGame(true, false);
	for (int32 SectionIndex = 0; SectionIndex < SourceMaterialIndices.Num(); ++SectionIndex)
	{
		UMaterialInterface* Material = SourceMesh->GetMaterial(SourceMaterialIndices[SectionIndex]);
		if (GetMaterial(SectionIndex) != Material) SetMaterial(SectionIndex, Material);
	}
	if (SmoothedBend.Equals(Previous, 1.e-8)) return;
	for (int32 SectionIndex = 0; SectionIndex < RestSections.Num(); ++SectionIndex)
	{
		const FProcMeshSection& Rest = RestSections[SectionIndex];
		TArray<FVector> Positions, Normals;
		TArray<FProcMeshTangent> Tangents;
		Positions.Reserve(Rest.ProcVertexBuffer.Num());
		Normals.Reserve(Rest.ProcVertexBuffer.Num());
		Tangents.Reserve(Rest.ProcVertexBuffer.Num());
		for (const FProcMeshVertex& Vertex : Rest.ProcVertexBuffer)
		{
			const FQuat Rotation = Curve.RotationAt(Vertex.Position);
			Positions.Add(Curve.DeformPosition(Vertex.Position));
			Normals.Add(Rotation.RotateVector(Vertex.Normal));
			Tangents.Emplace(Rotation.RotateVector(Vertex.Tangent.TangentX), Vertex.Tangent.bFlipTangentY);
		}
		UpdateMeshSection(SectionIndex, Positions, Normals, TArray<FVector2D>(), TArray<FColor>(), Tangents);
	}
}

void UCatRodBendComponent::LogVisualEvent(const TCHAR* Event, const TCHAR* Result, const double TensionNewtons) const
{
	const ACatFishingRodActor* Rod = Cast<ACatFishingRodActor>(GetOwner());
	const ACatFishingHookActor* Hook = LoadHook.Get();
	const FString Fields = FString::Printf(
		TEXT("Event=%s Result=%s World=%s NetMode=%d Authority=%d LocalRole=%d Rod=%s RodActorId=%s SessionId=%s Hook=%s Mesh=%s Sections=%d LineTensionN=%.3f BendRadians=%s HolderPlayerId=%d"),
		Event, Result, *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), Rod && Rod->HasAuthority(),
		Rod ? static_cast<int32>(Rod->GetLocalRole()) : 0, *GetNameSafe(Rod),
		Rod ? *Rod->GetPresentationState().RodActorId.ToString() : TEXT("None"),
		*DiagnosticSessionId.ToString(), *GetNameSafe(Hook),
		*GetNameSafe(BuiltMesh.Get()), RestSections.Num(), TensionNewtons, *SmoothedBend.ToCompactString(),
		Rod && Rod->GetPresentationState().HolderPlayerState ? Rod->GetPresentationState().HolderPlayerState->GetPlayerId() : INDEX_NONE);
	if (FCString::Strstr(Event, TEXT("rejected")))
	{
		UE_LOG(LogCatFishing, Warning, TEXT("%s"), *Fields);
	}
	else
	{
		UE_LOG(LogCatFishing, Display, TEXT("%s"), *Fields);
	}
}
