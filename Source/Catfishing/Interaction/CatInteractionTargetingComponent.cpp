#include "Interaction/CatInteractionTargetingComponent.h"

#include "Interaction/CatInteractable.h"
#include "Interaction/CatInteractionSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

UCatInteractionTargetingComponent::UCatInteractionTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UCatInteractionTargetingComponent::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PlayerController = GetOwningPlayerController();
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	if (!PlayerController || !PlayerController->IsLocalController() || !Settings || !GetWorld())
	{
		return;
	}
	const float Interval = static_cast<float>(FMath::Max(0.016, Settings->TargetingIntervalSeconds));
	GetWorld()->GetTimerManager().SetTimer(TargetingTimer, this,
		&ThisClass::RefreshTargetFromCrosshair, Interval, true, 0.0f);
}

void UCatInteractionTargetingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TargetingTimer);
	}
	ClearTarget();
	Super::EndPlay(EndPlayReason);
}

APlayerController* UCatInteractionTargetingComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

AActor* UCatInteractionTargetingComponent::TraceInteractableFromCrosshair() const
{
	APlayerController* PlayerController = GetOwningPlayerController();
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	if (!PlayerController || !PlayerController->IsLocalController() || !Settings || !World)
	{
		return nullptr;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	PlayerController->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		return nullptr;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!PlayerController->DeprojectScreenPositionToWorld(
		static_cast<float>(SizeX) * 0.5f, static_cast<float>(SizeY) * 0.5f, RayOrigin, RayDirection))
	{
		return nullptr;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatInteractionTargeting), Settings->bTraceComplex);
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}
	FHitResult Hit;
	const FVector TraceEnd = RayOrigin + RayDirection.GetSafeNormal() * Settings->MaximumTargetingDistanceCentimeters;
	if (!World->LineTraceSingleByChannel(Hit, RayOrigin, TraceEnd, Settings->TargetingTraceChannel, QueryParams))
	{
		return nullptr;
	}
	AActor* HitActor = Hit.GetActor();
	return HitActor && HitActor->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		&& ICatInteractable::Execute_CanInteract(HitActor, PlayerController)
		? HitActor : nullptr;
}

void UCatInteractionTargetingComponent::RefreshTargetFromCrosshair()
{
	ApplyTarget(TraceInteractableFromCrosshair());
}

void UCatInteractionTargetingComponent::ApplyTarget(AActor* NewTarget)
{
	const bool bPreviousTargetWasDestroyed = CurrentTarget.IsStale();
	if (!bPreviousTargetWasDestroyed && CurrentTarget.Get() == NewTarget)
	{
		return;
	}
	AActor* PreviousTarget = CurrentTarget.Get();
	LastTarget = CurrentTarget;
	CurrentTarget = NewTarget;
	if (IsValid(PreviousTarget) && PreviousTarget->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
	{
		ICatInteractable::Execute_EndLocalFocus(PreviousTarget);
	}
	if (IsValid(NewTarget) && NewTarget->GetClass()->ImplementsInterface(UCatInteractable::StaticClass()))
	{
		ICatInteractable::Execute_BeginLocalFocus(NewTarget);
	}
	OnTargetChanged.Broadcast(PreviousTarget, NewTarget);
}

void UCatInteractionTargetingComponent::ClearTarget()
{
	ApplyTarget(nullptr);
	LastTarget.Reset();
}

void UCatInteractionTargetingComponent::TryInteract()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	AActor* Target = CurrentTarget.Get();
	if (PlayerController && PlayerController->IsLocalController() && IsValid(Target)
		&& Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass())
		&& ICatInteractable::Execute_CanInteract(Target, PlayerController))
	{
		ICatInteractable::Execute_Interact(Target, PlayerController, FGuid::NewGuid());
	}
}
