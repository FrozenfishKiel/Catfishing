#include "Fishing/Actors/CatFishEncounterActor.h"

#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"

ACatFishEncounterActor::ACatFishEncounterActor()
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
}

void ACatFishEncounterActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishEncounterActor::InitializeAuthoritativeIdentity(const FGuid InFishingSessionId, const FGuid InCastAttemptId,
	const FName InFishDefinitionId, const double InInitialLineLength)
{
	if (!HasAuthority() || !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid()
		|| InFishingSessionId == InCastAttemptId || InFishDefinitionId.IsNone()
		|| !FMath::IsFinite(InInitialLineLength) || InInitialLineLength < 0.0)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		return PresentationState.FishingSessionId == InFishingSessionId
			&& PresentationState.CastAttemptId == InCastAttemptId
			&& PresentationState.FishDefinitionId == InFishDefinitionId;
	}
	const FCatFishEncounterPresentationState Previous = PresentationState;
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.CastAttemptId = InCastAttemptId;
	PresentationState.FishDefinitionId = InFishDefinitionId;
	PresentationState.MotionIntent = ECatFishMotionIntent::None;
	PresentationState.CurrentLineLength = InInitialLineLength;
	bIdentityInitialized = true;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

const FCatFishEncounterPresentationState& ACatFishEncounterActor::GetPresentationState() const { return PresentationState; }

void ACatFishEncounterActor::DeferInitialPresentationFromAuthority()
{
	if (HasAuthority() && !HasActorBegunPlay()) bPresentationDeferred = true;
}

void ACatFishEncounterActor::PublishInitialPresentationFromAuthority()
{
	if (!HasAuthority()) return;
	bPresentationDeferred = false;
	if (bHasPendingPresentationNotification && HasActorBegunPlay())
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishEncounterActor::BeginPlay()
{
	Super::BeginPlay();
	if (bHasPendingPresentationNotification && !bPresentationDeferred)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishEncounterActor::OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous)
{
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishEncounterActor::QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous,
	const FCatFishEncounterPresentationState& Current)
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

void ACatFishEncounterActor::DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous,
	const FCatFishEncounterPresentationState& Current)
{
	BP_OnFishPresentationChanged(Previous, Current);
}
