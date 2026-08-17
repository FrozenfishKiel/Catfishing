#include "Environment/CatWaterRegion.h"

#include "Engine/World.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Presentation/CatWaterRegionPresentationActor.h"
#include "Environment/Presentation/CatWaterRegionPresentationSubsystem.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#include "Environment/CatWaterBoundarySplineActor.h"
#include "Misc/DataValidation.h"
#include "UObject/ObjectSaveContext.h"
#endif

namespace CatWaterRegionPrivate
{
	static bool IsFinite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	static void ComputeWorldBounds(const FCatWaterGeometryCache& Cache, FVector& OutCenter, FVector& OutExtent)
	{
		FBox Box(ForceInit);
		const FVector2D Min = Cache.Bounds2D.Min;
		const FVector2D Max = Cache.Bounds2D.Max;
		for (const FVector2D Corner : {FVector2D(Min.X, Min.Y), FVector2D(Max.X, Min.Y),
			FVector2D(Max.X, Max.Y), FVector2D(Min.X, Max.Y)})
		{
			Box += Cache.PlaneToWorld.TransformPosition(FVector(Corner.X, Corner.Y, 0.0));
		}
		OutCenter = Box.GetCenter();
		OutExtent = Box.GetExtent();
		OutCenter.Z = Cache.WaterSurfaceZ;
		OutExtent.Z = FMath::Max(Cache.WaterPointVerticalToleranceCm, Cache.BankHeightToleranceCm);
	}

#if WITH_EDITOR
	static double Cross(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	static bool OnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(Cross(A, B, P)) <= UE_DOUBLE_SMALL_NUMBER
			&& P.X >= FMath::Min(A.X, B.X) && P.X <= FMath::Max(A.X, B.X)
			&& P.Y >= FMath::Min(A.Y, B.Y) && P.Y <= FMath::Max(A.Y, B.Y);
	}

	static bool SegmentsIntersect(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		const double C1 = Cross(A, B, C), C2 = Cross(A, B, D), C3 = Cross(C, D, A), C4 = Cross(C, D, B);
		if (((C1 > 0) != (C2 > 0)) && ((C3 > 0) != (C4 > 0))) return true;
		return (FMath::IsNearlyZero(C1) && OnSegment(C, A, B))
			|| (FMath::IsNearlyZero(C2) && OnSegment(D, A, B))
			|| (FMath::IsNearlyZero(C3) && OnSegment(A, C, D))
			|| (FMath::IsNearlyZero(C4) && OnSegment(B, C, D));
	}

	static bool PointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Vertices)
	{
		bool bInside = false;
		for (int32 I = 0, J = Vertices.Num() - 1; I < Vertices.Num(); J = I++)
		{
			if (OnSegment(Point, Vertices[J], Vertices[I])) return true;
			const bool bCrosses = (Vertices[I].Y > Point.Y) != (Vertices[J].Y > Point.Y);
			if (bCrosses && Point.X < (Vertices[J].X - Vertices[I].X) * (Point.Y - Vertices[I].Y)
				/ (Vertices[J].Y - Vertices[I].Y) + Vertices[I].X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	static bool PolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
	{
		for (int32 I = 0; I < A.Num(); ++I)
			for (int32 J = 0; J < B.Num(); ++J)
				if (SegmentsIntersect(A[I], A[(I + 1) % A.Num()], B[J], B[(J + 1) % B.Num()])) return true;
		return PointInPolygon(A[0], B) || PointInPolygon(B[0], A);
	}

	static bool RegionsOverlap(const FCatWaterGeometryCache& A, const FCatWaterGeometryCache& B)
	{
		if (!FMath::IsNearlyEqual(A.WaterSurfaceZ, B.WaterSurfaceZ, 0.1)) return false;
		for (const FCatWaterBakedPolygon& APolygon : A.IncludePolygons)
		{
			TArray<FVector2D> AWorld;
			for (const FVector2D& Point : APolygon.Vertices)
			{
				const FVector World = A.PlaneToWorld.TransformPosition(FVector(Point.X, Point.Y, 0));
				AWorld.Add(FVector2D(World.X, World.Y));
			}
			for (const FCatWaterBakedPolygon& BPolygon : B.IncludePolygons)
			{
				TArray<FVector2D> BWorld;
				for (const FVector2D& Point : BPolygon.Vertices)
				{
					const FVector World = B.PlaneToWorld.TransformPosition(FVector(Point.X, Point.Y, 0));
					BWorld.Add(FVector2D(World.X, World.Y));
				}
				if (PolygonsOverlap(AWorld, BWorld)) return true;
			}
		}
		return false;
	}
#endif
}

ACatWaterRegion::ACatWaterRegion()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

bool ACatWaterRegion::HasValidBakedGeometry() const
{
	if (BakedSourceDigest == 0 || GeometryRevision <= 0 || !BakedGeometry.IsRuntimeReady()
		|| BakedGeometry.Handle.RegionId != RegionId || BakedGeometry.Handle.GeometryRevision != GeometryRevision)
	{
		return false;
	}
#if WITH_EDITOR
#if WITH_DEV_AUTOMATION_TESTS
	if (bTrustInjectedGeometryForTests) return true;
#endif
	FCatWaterGeometryBuildInput CurrentInput;
	TArray<FString> Errors;
	if (!BuildCurrentGeometryInput(CurrentInput, Errors)
		|| FCatWaterGeometry::ComputeRevision(CurrentInput) != BakedSourceDigest)
	{
		return false;
	}
#endif
	return true;
}

FCatWaterRegionHandle ACatWaterRegion::GetWaterRegionHandle() const
{
	return HasValidBakedGeometry() ? BakedGeometry.Handle : FCatWaterRegionHandle();
}

const FBox2D& ACatWaterRegion::GetBakedBoundsForQuery() const
{
	return BakedGeometry.Bounds2D;
}

bool ACatWaterRegion::IsRuntimeConfigured() const
{
	return HasValidBakedGeometry();
}

bool ACatWaterRegion::ContainsWorldPoint(const FVector& WorldPoint) const
{
	if (!HasValidBakedGeometry()) return false;
	const FCatWaterSpatialResult Result = FCatWaterGeometry::QueryPoint(BakedGeometry, WorldPoint, BakedGeometry.BankHeightToleranceCm);
	return Result.bSucceeded && Result.Containment != ECatWaterContainment::Outside;
}

FCatWaterRegionSnapshot ACatWaterRegion::MakeSnapshot() const
{
	FCatWaterRegionSnapshot Snapshot;
	if (!HasValidBakedGeometry()) return Snapshot;
	Snapshot.RegionId = BakedGeometry.Handle.RegionId;
	Snapshot.GeometryRevision = BakedGeometry.Handle.GeometryRevision;
	Snapshot.AggregationRevision = AggregationRevision;
	CatWaterRegionPrivate::ComputeWorldBounds(BakedGeometry, Snapshot.WorldCenter, Snapshot.HalfExtent);
	Snapshot.ChumPool = ChumPool;
	return Snapshot;
}

void ACatWaterRegion::BeginPlay()
{
	Super::BeginPlay();
	if (!HasValidBakedGeometry()) return;
	if (UCatWaterQuerySubsystem* Query = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>())
	{
		Query->RegisterRegion(this);
	}
	if (GetNetMode() == NM_DedicatedServer || WaterPresentationClass.IsNull()) return;
	UClass* PresentationClass = WaterPresentationClass.LoadSynchronous();
	if (!PresentationClass || !PresentationClass->IsChildOf(ACatWaterRegionPresentationActor::StaticClass())) return;
	SpawnedPresentation = GetWorld()->SpawnActor<ACatWaterRegionPresentationActor>(PresentationClass, BakedGeometry.PlaneToWorld);
	if (!SpawnedPresentation) return;
	SpawnedPresentation->ApplyWaterGeometryPresentation(BakedGeometry);
	SpawnedPresentation->SetWaterPreviewVisible(false);
	if (UCatWaterRegionPresentationSubsystem* Presentation = GetWorld()->GetSubsystem<UCatWaterRegionPresentationSubsystem>())
	{
		Presentation->RegisterPresentation(GetWaterRegionHandle(), SpawnedPresentation);
	}
}

void ACatWaterRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>()) Query->UnregisterRegion(this);
		if (UCatWaterRegionPresentationSubsystem* Presentation = World->GetSubsystem<UCatWaterRegionPresentationSubsystem>())
			Presentation->UnregisterPresentation(GetWaterRegionHandle(), SpawnedPresentation);
	}
	if (SpawnedPresentation)
	{
		SpawnedPresentation->Destroy();
		SpawnedPresentation = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
bool ACatWaterRegion::BuildCurrentGeometryInput(FCatWaterGeometryBuildInput& OutInput, TArray<FString>& OutErrors) const
{
	OutInput = FCatWaterGeometryBuildInput(); OutErrors.Reset();
	const FTransform ActorTransform = GetActorTransform();
	if (ActorTransform.ContainsNaN() || !ActorTransform.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyZero(ActorTransform.Rotator().Pitch) || !FMath::IsNearlyZero(ActorTransform.Rotator().Roll)
		|| !CatWaterRegionPrivate::IsFinite(GetActorLocation()) || !FMath::IsFinite(WaterSurfaceZ))
	{
		OutErrors.Add(TEXT("WaterRegion transform must be finite, yaw-only, and unit scale."));
		return false;
	}
	OutInput.RegionId = RegionId;
	OutInput.PlaneToWorld = FTransform(FRotator(0, ActorTransform.Rotator().Yaw, 0),
		FVector(GetActorLocation().X, GetActorLocation().Y, WaterSurfaceZ));
	OutInput.WaterPointVerticalToleranceCm = WaterPointVerticalToleranceCm;
	OutInput.BankHeightToleranceCm = BankHeightToleranceCm;
	OutInput.BoundaryToleranceCm = BoundaryToleranceCm;
	OutInput.MaxLandingCorrectionCm = MaxLandingCorrectionCm;
	OutInput.MinimumWaterInsetCm = MinimumWaterInsetCm;
	OutInput.MaxSampleSegmentLengthCm = MaxSampleSegmentLengthCm;
	OutInput.MaxChordErrorCm = MaxChordErrorCm;
	const FTransform WorldToPlane = OutInput.PlaneToWorld.Inverse();
	for (const ACatWaterBoundarySplineActor* Boundary : BoundaryActors)
	{
		if (!Boundary)
		{
			OutErrors.Add(TEXT("BoundaryActors contains null."));
			continue;
		}
		if (Boundary->OwningRegion != this)
		{
			OutErrors.Add(FString::Printf(TEXT("Boundary '%s' ownership does not match this region."), *Boundary->GetName()));
			continue;
		}
		FCatWaterPolygonBuildInput Polygon; FString Error;
		if (!Boundary->BuildPolygonInput(WorldToPlane, MaxSampleSegmentLengthCm, MaxChordErrorCm, Polygon, Error))
		{
			OutErrors.Add(Error);
			continue;
		}
		OutInput.Boundaries.Add(MoveTemp(Polygon));
	}
	return OutErrors.IsEmpty();
}

void ACatWaterRegion::BakeGeometry()
{
	FCatWaterGeometryBuildInput Input; TArray<FString> Errors;
	if (!BuildCurrentGeometryInput(Input, Errors))
	{
		InvalidateBakedGeometry();
		return;
	}
	FCatWaterGeometryBuildResult Build = FCatWaterGeometry::Build(Input);
	if (!Build.bSucceeded)
	{
		InvalidateBakedGeometry();
		return;
	}
	BakedGeometry = MoveTemp(Build.Cache);
	GeometryRevision = BakedGeometry.Handle.GeometryRevision;
	BakedSourceDigest = FCatWaterGeometry::ComputeRevision(Input);
}

void ACatWaterRegion::InvalidateBakedGeometry()
{
	BakedGeometry = FCatWaterGeometryCache();
	GeometryRevision = 0;
	BakedSourceDigest = 0;
}

EDataValidationResult ACatWaterRegion::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FCatWaterGeometryBuildInput Input; TArray<FString> Errors;
	if (!BuildCurrentGeometryInput(Input, Errors))
	{
		for (const FString& Error : Errors) Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	if (!HasValidBakedGeometry())
	{
		Context.AddError(FText::FromString(TEXT("WaterRegion baked geometry is missing or stale.")));
		return EDataValidationResult::Invalid;
	}
	for (TActorIterator<ACatWaterRegion> It(GetWorld()); It; ++It)
	{
		const ACatWaterRegion* Other = *It;
		if (!Other || Other == this || !Other->HasValidBakedGeometry()) continue;
		if (Other->RegionId == RegionId)
		{
			Context.AddError(FText::FromString(TEXT("WaterRegion RegionId is duplicated.")));
			Result = EDataValidationResult::Invalid;
		}
		if (CatWaterRegionPrivate::RegionsOverlap(BakedGeometry, Other->BakedGeometry))
		{
			Context.AddError(FText::FromString(TEXT("WaterRegion overlaps another valid region at the same height.")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}

void ACatWaterRegion::PreSave(FObjectPreSaveContext SaveContext)
{
	FCatWaterGeometryBuildInput Input; TArray<FString> Errors;
	if (!BuildCurrentGeometryInput(Input, Errors)
		|| FCatWaterGeometry::ComputeRevision(Input) != BakedSourceDigest)
	{
		InvalidateBakedGeometry();
	}
	Super::PreSave(SaveContext);
}

void ACatWaterRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const bool bGeometryProperty = PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, RegionId)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, WaterSurfaceZ)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, WaterPointVerticalToleranceCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, BankHeightToleranceCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, BoundaryToleranceCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, MaxLandingCorrectionCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, MinimumWaterInsetCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, MaxSampleSegmentLengthCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, MaxChordErrorCm)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACatWaterRegion, BoundaryActors);
	if (bGeometryProperty) InvalidateBakedGeometry();
}

void ACatWaterRegion::PostEditMove(const bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished) InvalidateBakedGeometry();
}
#endif

FCatAggregationResult ACatWaterRegion::ContributeAggregation(const FCatAggregationCommand& Command)
{
	FCatAggregationResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *Command.Context.StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatAggregationResult* Cached = AggregationTerminalCache.Find(CacheKey))
	{
		Result = *Cached; Result.Command.bCommitted = false; Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	Result.Command.Error = ValidateAggregation(Command);
	if (Result.Command.Error == ECatDomainCommandError::None)
	{
		ChumPool.Fishy += Command.Contribution.Fishy; ChumPool.Fragrant += Command.Contribution.Fragrant;
		ChumPool.Fermented += Command.Contribution.Fermented; ++AggregationRevision;
		Result.Command.bCommitted = true;
	}
	Result.AggregationRevision = AggregationRevision; Result.Command.Revision = AggregationRevision; Result.ChumPool = ChumPool;
	AggregationTerminalCache.Add(CacheKey, Result);
	return Result;
}

ECatDomainCommandError ACatWaterRegion::ValidateAggregation(const FCatAggregationCommand& Command) const
{
	const double ExistingTotal = ChumPool.Fishy + ChumPool.Fragrant + ChumPool.Fermented;
	const double AddedTotal = Command.Contribution.Fishy + Command.Contribution.Fragrant + Command.Contribution.Fermented;
	if (!HasAuthority() || !bEnableAggregation || !FMath::IsFinite(AggregationBudget) || AggregationBudget <= 0.0)
		return ECatDomainCommandError::PolicyUndecided;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| Command.RegionId != RegionId || !Command.Contribution.IsValidContribution()) return ECatDomainCommandError::InvalidPayload;
	if (Command.ExpectedAggregationRevision != AggregationRevision) return ECatDomainCommandError::RevisionConflict;
	return !FMath::IsFinite(ExistingTotal + AddedTotal) || ExistingTotal + AddedTotal > AggregationBudget
		? ECatDomainCommandError::CapacityExceeded : ECatDomainCommandError::None;
}
