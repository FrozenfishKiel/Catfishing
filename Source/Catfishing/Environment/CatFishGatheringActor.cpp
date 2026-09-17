#include "Environment/CatFishGatheringActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Logging/CatLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

bool FCatFishGatheringState::IsActive(const double ServerTime) const
{
	return EventId.IsValid() && WaterRegion.IsValid() && !Center.ContainsNaN()
		&& FMath::IsFinite(RadiusCentimeters) && RadiusCentimeters > 0.0
		&& FMath::IsFinite(BiteSpeedMultiplier) && BiteSpeedMultiplier > 1.0
		&& FMath::IsFinite(StartedServerTime) && FMath::IsFinite(EndsServerTime)
		&& FMath::IsFinite(ServerTime) && ServerTime >= StartedServerTime && ServerTime < EndsServerTime;
}

bool FCatFishGatheringState::Contains(const FVector& Point, const FCatWaterRegionHandle& Region,
	const double ServerTime) const
{
	return IsActive(ServerTime) && Region == WaterRegion && !Point.ContainsNaN()
		&& FVector::DistSquared2D(Point, Center) <= FMath::Square(RadiusCentimeters);
}

ACatFishGatheringActor::ACatFishGatheringActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	SetReplicateMovement(false);
	Ring = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GatheringRing"));
	SetRootComponent(Ring);
	FishShadows = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GatheringFishShadows"));
	FishShadows->SetupAttachment(Ring);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (auto* Mesh : {Ring.Get(), FishShadows.Get()})
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(false);
		Mesh->SetCanEverAffectNavigation(false);
		if (Material.Succeeded()) Mesh->SetMaterial(0, Material.Object);
	}
	if (Cube.Succeeded()) Ring->SetStaticMesh(Cube.Object);
	if (Sphere.Succeeded()) FishShadows->SetStaticMesh(Sphere.Object);
	SetActorEnableCollision(false);
}

void ACatFishGatheringActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, State);
}

bool ACatFishGatheringActor::InitializeFromAuthority(const FCatFishGatheringState& InState)
{
	if (!HasAuthority() || State.EventId.IsValid() || !InState.IsActive(GetWorld()->GetTimeSeconds())) return false;
	State = InState;
	OnRep_State();
	GetWorldTimerManager().SetTimer(EndTimer, this, &ThisClass::FinishFromAuthority,
		State.EndsServerTime - GetWorld()->GetTimeSeconds(), false);
	ForceNetUpdate();
	return true;
}

double ACatFishGatheringActor::GetServerTime() const
{
	const auto* GameState = GetWorld()->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void ACatFishGatheringActor::OnRep_State()
{
	if (!State.EventId.IsValid()) return;
	bReceivedState = true;
	SetActorLocation(State.Center);
	UE_LOG(LogCatEnvironment, Log, TEXT("Event=fish_gathering_observed EventId=%s RequestId=%s Region=%s Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d EndsServerTime=%.3f Speed=%.3f"),
		*State.EventId.ToString(), *State.RequestId.ToString(), *State.WaterRegion.RegionId.ToString(), *GetName(),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), int32(GetLocalRole()), State.EndsServerTime, State.BiteSpeedMultiplier);
	if (GetNetMode() == NM_DedicatedServer) return;
	Ring->ClearInstances();
	FishShadows->ClearInstances();
	if (auto* Material = Ring->CreateDynamicMaterialInstance(0))
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.8f, 0.15f));
	if (auto* Material = FishShadows->CreateDynamicMaterialInstance(0))
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.035f, 0.10f, 0.09f));
	constexpr int32 Segments = 80;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const double Angle = 2.0 * UE_PI * Index / Segments;
		const FVector Offset(State.RadiusCentimeters * FMath::Cos(Angle), State.RadiusCentimeters * FMath::Sin(Angle), 6.0);
		Ring->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90.0, 0), Offset,
			FVector(2.0 * State.RadiusCentimeters * FMath::Sin(UE_PI / Segments) / 100.0, 0.10, 0.018)));
	}
	for (int32 Index = 0; Index < 36; ++Index) FishShadows->AddInstance(FTransform::Identity);
	AnimateFishShadows();
	GetWorldTimerManager().SetTimer(VisualTimer, this, &ThisClass::AnimateFishShadows, 0.1f, true);
}

void ACatFishGatheringActor::AnimateFishShadows()
{
	const double Now = GetServerTime();
	const bool bActive = State.IsActive(Now);
	Ring->SetVisibility(bActive);
	FishShadows->SetVisibility(bActive);
	if (!bActive) return;
	FRandomStream Random(GetTypeHash(State.EventId));
	for (int32 Index = 0; Index < FishShadows->GetInstanceCount(); ++Index)
	{
		const double Radius = State.RadiusCentimeters * Random.FRandRange(0.15f, 0.85f);
		const double Angle = Random.FRandRange(0.0f, 2.0f * UE_PI) + (Now - State.StartedServerTime) * (0.18 + Index * 0.004);
		FishShadows->UpdateInstanceTransform(Index, FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90.0, 0),
			FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 2.0), FVector(0.28, 0.08, 0.015)), false, false, true);
	}
	FishShadows->MarkRenderStateDirty();
}

void ACatFishGatheringActor::FinishFromAuthority()
{
	if (HasAuthority()) Destroy();
}

void ACatFishGatheringActor::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(EndTimer);
	GetWorldTimerManager().ClearTimer(VisualTimer);
	if (bReceivedState)
		UE_LOG(LogCatEnvironment, Log, TEXT("Event=fish_gathering_ended EventId=%s RequestId=%s Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=%d"),
			*State.EventId.ToString(), *State.RequestId.ToString(), *GetName(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), int32(GetLocalRole()), int32(Reason));
	if (HasAuthority() && State.EventId.IsValid()) OnEnded.Broadcast(State.EventId);
	Super::EndPlay(Reason);
}
