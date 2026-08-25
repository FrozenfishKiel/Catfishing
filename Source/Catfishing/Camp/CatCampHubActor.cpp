#include "Camp/CatCampHubActor.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Components/SceneComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Items/CatFishTankActor.h"
#include "Items/CatItemsService.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

// 构造流程：创建固定根与救援子节点、开启复制并关闭 Tick；营地布局始终来自 Lake 关卡资产而非玩家运行时修改。
ACatCampHubActor::ACatCampHubActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	CampRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CampRoot"));
	SetRootComponent(CampRoot);
	RescuePoint = CreateDefaultSubobject<USceneComponent>(TEXT("RescuePoint"));
	RescuePoint->SetupAttachment(CampRoot);
}

// 休息流程：现取本人 Character 并验证固定范围；随后只调用 Condition 的 CampRest 写口，营地不保存第二份身体数值。
FCatDomainCommandResult ACatCampHubActor::RequestRest(AController* RequestingController, const FGuid RequestId)
{
	ACatCharacter* Character = ResolveCharacterInCamp(RequestingController);
	UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	if (Conditions)
	{
		return Conditions->RequestCampRest(RequestingController, RequestId, true);
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::PolicyUndecided;
	return Result;
}

// 救援流程：先用救援者身份与 RequestId 重放成功终态，再从两个 Character 读服务器位置并要求救援者可行动、目标已倒地、距离不超营地显式交互范围；TeleportTo 成功后才提交 CarriedToCamp。
FCatDomainCommandResult ACatCampHubActor::RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const APlayerState* HelpingPlayerState = HelpingController ? HelpingController->PlayerState : nullptr;
	ACatCharacter* HelpingCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatConditionComponent* HelpingConditions = HelpingCharacter ? HelpingCharacter->GetConditionComponent() : nullptr;
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	if (!HasAuthority() || !HelpingPlayerState || !HelpingPlayerState->GetUniqueId().IsValid()
		|| !HelpingCharacter || !HelpingConditions || HelpingConditions->GetSnapshot().bDowned
		|| !TargetCharacter || TargetCharacter->GetWorld() != GetWorld() || !RequestId.IsValid()
		|| !RescuePoint || !TargetCharacter->GetConditionComponent() || !Settings || !Settings->IsRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = FString::Printf(TEXT("%s|Rescue|%s"),
		*HelpingPlayerState->GetUniqueId()->ToString(), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = RescueTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!TargetCharacter->GetConditionComponent()->GetSnapshot().bDowned
		|| FVector::DistSquared(HelpingCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->InteractionRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	if (!TargetCharacter->TeleportTo(RescuePoint->GetComponentLocation(), RescuePoint->GetComponentRotation(), false, true))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result = TargetCharacter->GetConditionComponent()->CompleteCarryToCamp(HelpingController, RequestId, true);
	if (Result.bCommitted)
	{
		RescueTerminalCache.Add(CacheKey, Result);
	}
	return Result;
}

// 入缸流程：验证本人在营地、真实鱼护/鱼缸 ID 与 Items；构造双 Revision 命令并只调用唯一原子转移入口。
FCatDomainCommandResult ACatCampHubActor::TransferFishToTank(AController* RequestingController, const FGuid RequestId,
	const FGuid FishInstanceId, const int64 ExpectedGuardRevision, const int64 ExpectedTankRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* Character = ResolveCharacterInCamp(RequestingController);
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	const APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!Character || !Items || !SharedFishTank || !PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	FCatContainerSnapshot GuardSnapshot;
	if (!Items->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), GuardSnapshot))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const FCatFishInstance* Fish = GuardSnapshot.Fish.FindByPredicate([FishInstanceId](const FCatFishInstance& Candidate)
	{
		return Candidate.FishInstanceId == FishInstanceId;
	});
	UCatFishDefinition* Definition = Fish
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId) : nullptr;
	if (!Definition || !Definition->bTankDisplayEligible)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	FCatFishTransferCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedGuardRevision;
	Command.Context.StableNetId = PlayerState->GetUniqueId()->ToString();
	Command.FishInstanceId = FishInstanceId;
	Command.SourceContainerId = Character->GetPersonalFishGuardId();
	Command.TargetContainerId = SharedFishTank->GetTankContainerId();
	Command.ExpectedTargetRevision = ExpectedTankRevision;
	return Items->TransferOwnedFish(Command);
}

// 共享鱼缸读取流程：只通过 Items 公开快照口读取当前固定鱼缸；缺 Items、缺鱼缸或容器未注册都返回 false 并清空输出。
bool ACatCampHubActor::TryGetSharedFishTankSnapshot(FCatContainerSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatContainerSnapshot();
	const UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	return Items && SharedFishTank && Items->TryGetContainerSnapshot(SharedFishTank->GetTankContainerId(), OutSnapshot);
}

// 篝火回看流程：先由服务器 UniqueId 与 RequestId 重放首次终态，再验证固定营地范围、结算夜和封面事件配置。随后逐个确认 GameState 玩家仍有有效身份、Controller 和营地内 Character，提交全员 Candidate，并通过批量接口先建齐全部 Planned 记录、再尝试投递；任一前置或落盘失败都会缓存拒绝且不发网络表现。全部事实成立后才用 Reliable NetMulticast 把原 RequestId 送到相关客户端，并缓存首次成功；本流程不写 next-day ready、不等待客户端播放完成，也不保存补播状态。
FCatDomainCommandResult ACatCampHubActor::RequestCampfirePlayback(AController* RequestingController, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const APlayerState* RequestingPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableNetId = RequestingPlayerState && RequestingPlayerState->GetUniqueId().IsValid()
		? RequestingPlayerState->GetUniqueId()->ToString() : FString();
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
		return Result;
	}
	const FString TerminalKey = FString::Printf(TEXT("%s|CampfirePlayback|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = CampfirePlaybackTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CampfirePlaybackTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!ResolveCharacterInCamp(RequestingController))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!GameState)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	const bool bSettlementNight =
		GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::FailureSettlementNight
		|| GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::SuccessSettlementNight;
	if (!bSettlementNight)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	if (!Settings || Settings->CampfireCoverEventId.IsNone())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}

	FCatImprintCandidate Candidate;
	Candidate.CandidateId = RequestId;
	Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	Candidate.EventType = Settings->CampfireCoverEventId;
	Candidate.SubjectId = Candidate.RunId;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		APlayerController* Controller = PlayerState ? PlayerState->GetPlayerController() : nullptr;
		if (!Controller || !PlayerState->GetUniqueId().IsValid() || !ResolveCharacterInCamp(Controller))
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
			return Finish(Result);
		}
		Candidate.ParticipantStableNetIds.Add(PlayerState->GetUniqueId()->ToString());
	}
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	Candidate.bAllActivePlayersPresent = Candidate.ParticipantCount > 0;
	UCatRunImprintService* Imprint = GetWorld()->GetSubsystem<UCatRunImprintService>();
	if (!Imprint || !Candidate.bAllActivePlayersPresent || !Imprint->SubmitImprintCandidate(Candidate))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	TArray<FCatCapturePlan> CapturePlans;
	if (!Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
		Candidate.ParticipantStableNetIds, true, CapturePlans))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	MulticastCampfirePlaybackRequested(RequestId);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 篝火网络表现流程：NetMulticast 已由引擎把服务器确认的 RequestId 分发到该 Actor 的相关连接；每个收到调用的进程只广播一次既有本地委托，不写 ACK、ready、补播队列或持久状态。
void ACatCampHubActor::MulticastCampfirePlaybackRequested_Implementation(const FGuid RequestId)
{
	OnCampfirePlaybackRequested.Broadcast(RequestId);
}

// 营地只读判断流程：复用唯一范围解析并仅返回是否命中；不广播篝火、不恢复身体也不修改任何 Revision。
bool ACatCampHubActor::IsControllerInCamp(AController* Controller) const
{
	return ResolveCharacterInCamp(Controller) != nullptr;
}

// 营地范围流程：读取当前 Controller Pawn，验证 authority、显式配置和与固定 Actor 的世界距离；客户端不能提交自报位置。
ACatCharacter* ACatCampHubActor::ResolveCharacterInCamp(AController* Controller) const
{
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	return HasAuthority() && Settings && Settings->IsRuntimeReady() && Character
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation())
			<= FMath::Square(Settings->InteractionRadiusCentimeters)
		? Character : nullptr;
}
