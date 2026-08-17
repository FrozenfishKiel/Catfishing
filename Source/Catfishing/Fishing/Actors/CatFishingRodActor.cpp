#include "Fishing/Actors/CatFishingRodActor.h"

#include "Components/SceneComponent.h"
#include "Fishing/CatFishingService.h"
#include "Net/UnrealNetwork.h"

ACatFishingRodActor::ACatFishingRodActor()
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
	RodTipAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RodTipAnchor"));
	RodTipAnchor->SetupAttachment(SceneRoot);
	StandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("StandAnchor"));
	StandAnchor->SetupAttachment(SceneRoot);
	GripAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("GripAnchor"));
	GripAnchor->SetupAttachment(SceneRoot);
	SceneRoot->bEditableWhenInherited = false;
	RodTipAnchor->bEditableWhenInherited = false;
	StandAnchor->bEditableWhenInherited = false;
	GripAnchor->bEditableWhenInherited = false;
}

void ACatFishingRodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishingRodActor::InitializeAuthoritativeIdentity(const FGuid InRodActorId, const FName InRodDefinitionId,
	const FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState, APlayerState* InOperatorPlayerState,
	const bool bInDeployed, const bool bInBroken)
{
	if (!HasAuthority() || !InRodActorId.IsValid() || InRodDefinitionId.IsNone() || !InOwnerPlayerState)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		return PresentationState.RodActorId == InRodActorId
			&& PresentationState.RodDefinitionId == InRodDefinitionId
			&& PresentationState.OwnerPlayerState == InOwnerPlayerState;
	}

	const FCatFishingRodPresentationState Previous = PresentationState;
	FCatFishingRodPresentationState Next;
	Next.RodActorId = InRodActorId;
	Next.RodActorRevision = 1;
	Next.RodDefinitionId = InRodDefinitionId;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	Next.OwnerPlayerState = InOwnerPlayerState;
	Next.OperatorPlayerState = InOperatorPlayerState;
	Next.bDeployed = bInDeployed;
	Next.bBroken = bInBroken;
	PresentationState = Next;
	bIdentityInitialized = true;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

bool ACatFishingRodActor::ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip,
	const FTransform& InStand, const FTransform& InGrip)
{
	if (!HasAuthority() || bIdentityInitialized || InRodTip.ContainsNaN() || InStand.ContainsNaN() || InGrip.ContainsNaN())
	{
		return false;
	}
	RodTipCanonicalLocalTransform = InRodTip;
	StandCanonicalLocalTransform = InStand;
	GripCanonicalLocalTransform = InGrip;
	RodTipAnchor->SetRelativeTransform(InRodTip);
	StandAnchor->SetRelativeTransform(InStand);
	GripAnchor->SetRelativeTransform(InGrip);
	return true;
}

bool ACatFishingRodActor::CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next,
	const int64 ExpectedRevision)
{
	if (!HasAuthority() || !bIdentityInitialized || ExpectedRevision != PresentationState.RodActorRevision)
	{
		return false;
	}
	FCatFishingRodPresentationState Committed = Next;
	Committed.RodActorId = PresentationState.RodActorId;
	Committed.RodDefinitionId = PresentationState.RodDefinitionId;
	Committed.OwnerPlayerState = PresentationState.OwnerPlayerState;
	Committed.RodActorRevision = PresentationState.RodActorRevision + 1;
	const FCatFishingRodPresentationState Previous = PresentationState;
	PresentationState = Committed;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

bool ACatFishingRodActor::SetOperatorFromAuthority(APlayerState* InOperatorPlayerState, const int64 ExpectedRevision)
{
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerState = InOperatorPlayerState;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetRodSkinFromAuthority(const FName InRodSkinDefinitionId, const int64 ExpectedRevision)
{
	if (InRodSkinDefinitionId.IsNone()) return false;
	FCatFishingRodPresentationState Next = PresentationState;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetBrokenFromAuthority(const bool bInBroken, const int64 ExpectedRevision)
{
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bBroken = bInBroken;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetDeployedFromAuthority(const bool bInDeployed, const int64 ExpectedRevision)
{
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bDeployed = bInDeployed;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

const FCatFishingRodPresentationState& ACatFishingRodActor::GetPresentationState() const { return PresentationState; }
FTransform ACatFishingRodActor::GetRodTipWorldTransform() const { return RodTipCanonicalLocalTransform * GetActorTransform(); }
FTransform ACatFishingRodActor::GetStandWorldTransform() const { return StandCanonicalLocalTransform * GetActorTransform(); }
FTransform ACatFishingRodActor::GetGripWorldTransform() const { return GripCanonicalLocalTransform * GetActorTransform(); }

void ACatFishingRodActor::BeginPlay()
{
	Super::BeginPlay();
	if (bHasPendingPresentationNotification)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishingRodActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && PresentationState.OwnerPlayerState)
	{
		if (UWorld* World = GetWorld())
		{
			if (UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>())
			{
				Fishing->UnregisterDeployedRod(PresentationState.OwnerPlayerState, this);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ACatFishingRodActor::OnRep_PresentationState(const FCatFishingRodPresentationState& Previous)
{
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishingRodActor::QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	if (!HasActorBegunPlay())
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

void ACatFishingRodActor::DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	BP_ApplyRodSkin(Current.RodSkinDefinitionId);
	BP_OnRodPresentationChanged(Previous, Current);
}
