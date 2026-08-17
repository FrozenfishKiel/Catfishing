#include "Fishing/Actors/CatFishingHookActor.h"

#include "Components/SceneComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"

ACatFishingHookActor::ACatFishingHookActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	bNetUseOwnerRelevancy = false;
	bOnlyRelevantToOwner = false;
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	HookVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HookVisualAnchor"));
	HookVisualAnchor->SetupAttachment(VisualRoot);
	BobberVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("BobberVisualAnchor"));
	BobberVisualAnchor->SetupAttachment(VisualRoot);
	BaitVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("BaitVisualAnchor"));
	BaitVisualAnchor->SetupAttachment(VisualRoot);
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
}

void ACatFishingHookActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishingHookActor::InitializeAuthoritativeIdentity(const FGuid InFishingSessionId, const FGuid InCastAttemptId)
{
	if (!HasAuthority() || !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid() || InFishingSessionId == InCastAttemptId)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		return PresentationState.FishingSessionId == InFishingSessionId && PresentationState.CastAttemptId == InCastAttemptId;
	}
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.CastAttemptId = InCastAttemptId;
	PresentationState.Phase = ECatFishingHookPresentationPhase::CastFlight;
	bIdentityInitialized = true;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

const FCatFishingHookPresentationState& ACatFishingHookActor::GetPresentationState() const { return PresentationState; }

bool ACatFishingHookActor::BeginAuthoritativeFlight(const FVector& InitialVelocity,
	const FVector& ExpectedLandingWorldPoint)
{
	if (!HasAuthority() || !bIdentityInitialized || bLandingFinalized || InitialVelocity.ContainsNaN()
		|| ExpectedLandingWorldPoint.ContainsNaN())
	{
		return false;
	}
	ProjectileMovement->Velocity = InitialVelocity;
	ProjectileMovement->Activate(true);
	return true;
}

bool ACatFishingHookActor::FinalizeAuthoritativeLandingOnce(const bool bSucceeded,
	const FVector& LandingWorldPoint)
{
	if (!HasAuthority() || !bIdentityInitialized || bLandingFinalized || LandingWorldPoint.ContainsNaN())
	{
		return false;
	}
	bLandingFinalized = true;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	SetActorLocation(LandingWorldPoint);
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.Phase = bSucceeded ? ECatFishingHookPresentationPhase::Landed : ECatFishingHookPresentationPhase::Failed;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

void ACatFishingHookActor::DeferInitialPresentationFromAuthority()
{
	if (HasAuthority() && !HasActorBegunPlay()) bPresentationDeferred = true;
}

void ACatFishingHookActor::PublishInitialPresentationFromAuthority()
{
	if (!HasAuthority()) return;
	bPresentationDeferred = false;
	if (bHasPendingPresentationNotification && HasActorBegunPlay())
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishingHookActor::BeginPlay()
{
	Super::BeginPlay();
	if (bHasPendingPresentationNotification && !bPresentationDeferred)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishingHookActor::OnRep_PresentationState(const FCatFishingHookPresentationState& Previous)
{
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishingHookActor::QueueOrDispatchPresentationChanged(const FCatFishingHookPresentationState& Previous,
	const FCatFishingHookPresentationState& Current)
{
	if (!HasActorBegunPlay() || bPresentationDeferred)
	{
		if (!bHasPendingPresentationNotification)
		{
			PendingPreviousPresentationState = Previous;
			bHasPendingPresentationNotification = true;
		}
		PendingCurrentPresentationState = Current;
		return;
	}
	DispatchPresentationChanged(Previous, Current);
}

void ACatFishingHookActor::DispatchPresentationChanged(const FCatFishingHookPresentationState& Previous,
	const FCatFishingHookPresentationState& Current)
{
	BP_OnHookPresentationChanged(Previous, Current);
}
