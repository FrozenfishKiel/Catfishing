#include "Items/World/CatFishPickupActor.h"

#include "Algo/Unique.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/CatInteractionSettings.h"
#include "Items/CatWorldItemSettings.h"
#include "Net/UnrealNetwork.h"

ACatFishPickupActor::ACatFishPickupActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	PrimaryActorTick.bCanEverTick = false;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	SetRootComponent(InteractionSphere);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionSphere->SetGenerateOverlapEvents(false);

	FishMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FishMesh"));
	FishMesh->SetupAttachment(InteractionSphere);
	FishMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FishMesh->SetRenderCustomDepth(false);
}

void ACatFishPickupActor::BeginPlay()
{
	Super::BeginPlay();
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
	if (Settings && InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(static_cast<float>(Settings->PickupCollisionRadiusCentimeters));
		const UCatInteractionSettings* InteractionSettings = GetDefault<UCatInteractionSettings>();
		if (InteractionSettings)
		{
			InteractionSphere->SetCollisionResponseToChannel(InteractionSettings->TargetingTraceChannel, ECR_Block);
		}
	}
	if (Settings && FishMesh && GetNetMode() != NM_DedicatedServer)
	{
		FishMesh->SetRelativeTransform(Settings->LandedFishMeshRelativeTransform);
		if (USkeletalMesh* Mesh = Settings->LandedFishMesh.LoadSynchronous())
		{
			FishMesh->SetSkeletalMeshAsset(Mesh);
		}
	}
	ApplyVisualScale();
}

void ACatFishPickupActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishPickupActor::InitializeFromAuthority(const FGuid InFishingSessionId, const FGuid InFishInstanceId,
	UCatFishDefinition* InFishDefinition, const double InWeightKilograms, const double InVisualScale, const FName InRegionId,
	const TArray<FString>& InFishingParticipantStableNetIds)
{
	if (!HasAuthority() || bIdentityInitialized || !InFishingSessionId.IsValid() || !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| !FMath::IsFinite(InWeightKilograms) || InWeightKilograms <= 0.0
		|| !FMath::IsFinite(InVisualScale) || InVisualScale <= 0.0 || InRegionId.IsNone())
	{
		return false;
	}
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.FishInstanceId = InFishInstanceId;
	PresentationState.FishDefinitionId = InFishDefinition->FishDefinitionId;
	PresentationState.WeightKilograms = InWeightKilograms;
	PresentationState.VisualScale = InVisualScale;
	PresentationState.State = ECatFishPickupState::Available;
	FishDefinition = InFishDefinition;
	RegionId = InRegionId;
	FishingParticipantStableNetIds = InFishingParticipantStableNetIds;
	FishingParticipantStableNetIds.RemoveAll([](const FString& Entry) { return Entry.IsEmpty(); });
	FishingParticipantStableNetIds.Sort();
	FishingParticipantStableNetIds.SetNum(Algo::Unique(FishingParticipantStableNetIds));
	bIdentityInitialized = true;
	ApplyVisualScale();
	ForceNetUpdate();
	return true;
}

void ACatFishPickupActor::ApplyVisualScale()
{
	if (!FishMesh)
	{
		return;
	}
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
	const FVector BaseScale = Settings
		? Settings->LandedFishMeshRelativeTransform.GetScale3D() : FVector::OneVector;
	const double Scale = FMath::IsFinite(PresentationState.VisualScale) && PresentationState.VisualScale > 0.0
		? PresentationState.VisualScale : 1.0;
	FishMesh->SetRelativeScale3D(BaseScale * Scale);
}

void ACatFishPickupActor::ApplyLocalFocus(const bool bFocused)
{
	bLocallyFocused = bFocused && PresentationState.State == ECatFishPickupState::Available;
	if (FishMesh)
	{
		const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
		FishMesh->SetCustomDepthStencilValue(Settings ? Settings->FocusStencilValue : 1);
		FishMesh->SetRenderCustomDepth(bLocallyFocused);
	}
}

bool ACatFishPickupActor::CanInteract_Implementation(AController* RequestingController) const
{
	return RequestingController
		&& PresentationState.State == ECatFishPickupState::Available
		&& PresentationState.FishingSessionId.IsValid()
		&& PresentationState.FishInstanceId.IsValid();
}

void ACatFishPickupActor::BeginLocalFocus_Implementation()
{
	ApplyLocalFocus(true);
}

void ACatFishPickupActor::EndLocalFocus_Implementation()
{
	ApplyLocalFocus(false);
}

FText ACatFishPickupActor::GetInteractionPrompt_Implementation() const
{
	if (PresentationState.State != ECatFishPickupState::Available)
	{
		return FText::GetEmpty();
	}
	return FText::Format(NSLOCTEXT("Catfishing", "FishPickupPrompt", "拾取 {0}  {1} kg"),
		FText::FromName(PresentationState.FishDefinitionId),
		FText::AsNumber(PresentationState.WeightKilograms));
}

double ACatFishPickupActor::GetInteractionRadius_Implementation() const
{
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	return Settings ? Settings->MaximumServerInteractionDistanceCentimeters : 0.0;
}

bool ACatFishPickupActor::IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const
{
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !Pawn || !Settings || !World
		|| FVector::Dist(Pawn->GetPawnViewLocation(), GetActorLocation())
			> Settings->MaximumServerInteractionDistanceCentimeters)
	{
		return false;
	}
	if (!Settings->bRequireServerLineOfSight)
	{
		return true;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatFishPickupLineOfSight), true, Pawn);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetActorLocation(),
		Settings->TargetingTraceChannel, QueryParams);
	return !bHit || Hit.GetActor() == this;
}

// 拾取提交流程：先做身份、状态、倒地和空间校验；通过后本应把鱼提交进玩家的独立鱼护容器。
// 客户端只把同一个 Actor 接口请求转发到服务器；当前尚未选定目标地面鱼护，服务器不会把鱼写进 Character 背包。
bool ACatFishPickupActor::Interact_Implementation(AController* RequestingController,
	const FGuid RequestId)
{
	if (!HasAuthority())
	{
		ACatfishingPlayerController* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
		if (!PlayerController || !PlayerController->IsLocalController() || !RequestId.IsValid())
		{
			return false;
		}
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}

	const APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (PickupTerminalByRequester.Contains(CacheKey))
	{
		return true;
	}
	FCatDomainCommandResult Terminal;
	Terminal.RequestId = RequestId;
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!HasAuthority() || !bIdentityInitialized || !RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Terminal.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (PresentationState.State != ECatFishPickupState::Available)
	{
		Terminal.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Character || !Condition || Condition->GetSnapshot().bDowned || !IsAuthorityRequestSpatiallyValid(RequestingController))
	{
		Terminal.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (!Imprint || !FishDefinition || !Imprint->CanRecordCommittedCapture())
	{
		Terminal.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Terminal.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	PickupTerminalByRequester.Add(CacheKey, Terminal);
	return Terminal.bCommitted;
}

void ACatFishPickupActor::ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed,
	const FString& PickerStableNetId)
{
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!Imprint || !FishDefinition)
	{
		return;
	}
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = RegionId;
	const FGuid FishRecordedGrantId = Imprint->RecordCommittedCapture(Committed, PickerStableNetId, Condition);
	if (FishDefinition->CaptureImprintEventId.IsNone())
	{
		return;
	}
	FCatImprintCandidate Candidate;
	Candidate.CandidateId = Committed.FishInstance.FishInstanceId;
	if (const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>())
	{
		Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	}
	Candidate.EventType = FishDefinition->CaptureImprintEventId;
	Candidate.SubjectId = Committed.FishInstance.FishInstanceId;
	Candidate.FishDefinitionId = Committed.FishInstance.FishDefinitionId;
	Candidate.ParticipantStableNetIds = FishingParticipantStableNetIds;
	Candidate.ParticipantStableNetIds.AddUnique(PickerStableNetId);
	Candidate.ParticipantStableNetIds.Sort();
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	TArray<FCatCapturePlan> Plans;
	if (FishRecordedGrantId.IsValid() && Imprint->CanAcceptImprintCandidate(Candidate)
		&& Imprint->SubmitImprintCandidate(Candidate))
	{
		Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
			Candidate.ParticipantStableNetIds, false, Plans);
	}
}

void ACatFishPickupActor::OnRep_PresentationState(const FCatFishPickupPresentationState& Previous)
{
	ApplyVisualScale();
	if (PresentationState.State != ECatFishPickupState::Available)
	{
		if (InteractionSphere)
		{
			InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		ApplyLocalFocus(false);
	}
	BP_OnPickupPresentationChanged(Previous, PresentationState);
}
