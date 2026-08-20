#include "Camp/CatCampHubActor.h"

#include "Framework/Core/CatStableNetId.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
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

// 休息流程：先用玩家身份和 RequestId 重放首次终态，再检查当前固定营地范围；首次成功后离开营地的网络重试不会被当前坐标改写。
FCatDomainCommandResult ACatCampHubActor::RequestRest(AController* RequestingController, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString StableNetId = CatResolveStableNetId(RequestingController);
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = FString::Printf(TEXT("%s|Rest|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = RestTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		RestTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	ACatCharacter* Character = ResolveCharacterInCamp(RequestingController);
	UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	if (Conditions)
	{
		return Finish(Conditions->RequestCampRest(RequestingController, RequestId, true));
	}
	Result.Error = ECatDomainCommandError::PolicyUndecided;
	return Finish(Result);
}

// 救援流程：先用救援者身份、目标身份与 RequestId 重放首次终态，再从两个 Character 读服务器位置并要求救援者可行动、目
// 标已倒地、距离不超营地显式交互范围；TeleportTo 成功后才提交 CarriedToCamp。
FCatDomainCommandResult ACatCampHubActor::RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const APlayerState* HelpingPlayerState = HelpingController ? HelpingController->PlayerState : nullptr;
	const APlayerState* TargetPlayerState = TargetCharacter ? TargetCharacter->GetPlayerState() : nullptr;
	ACatCharacter* HelpingCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatConditionComponent* HelpingConditions = HelpingCharacter ? HelpingCharacter->GetConditionComponent() : nullptr;
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	if (!HasAuthority() || !HelpingPlayerState || !HelpingPlayerState->GetUniqueId().IsValid()
		|| !TargetPlayerState || !TargetPlayerState->GetUniqueId().IsValid()
		|| !HelpingCharacter || !HelpingConditions || HelpingConditions->GetSnapshot().bDowned
		|| !TargetCharacter || TargetCharacter->GetWorld() != GetWorld() || !RequestId.IsValid()
		|| !RescuePoint || !TargetCharacter->GetConditionComponent() || !Settings || !Settings->IsRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = FString::Printf(TEXT("%s|Rescue|%s"),
		*CatResolveStableNetId(HelpingPlayerState), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	const FString PayloadSignature = FString::Printf(TEXT("Target=%s"), *CatResolveStableNetId(TargetPlayerState));
	switch (CatQueryTerminalReplay(RescueTerminalCache, RescuePayloadByKey, CacheKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &CacheKey, &PayloadSignature](const FCatDomainCommandResult& TerminalResult)
	{
		RescueTerminalCache.Add(CacheKey, TerminalResult);
		RescuePayloadByKey.Add(CacheKey, PayloadSignature);
		return TerminalResult;
	};
	if (!TargetCharacter->GetConditionComponent()->GetSnapshot().bDowned
		|| FVector::DistSquared(HelpingCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->InteractionRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	if (!TargetCharacter->TeleportTo(RescuePoint->GetComponentLocation(), RescuePoint->GetComponentRotation(), false, true))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result = TargetCharacter->GetConditionComponent()->CompleteCarryToCamp(HelpingController, RequestId, true);
	return Finish(Result);
}

// 入缸流程：先用服务器身份、鱼 ID、双 Revision 和固定鱼缸建立重放边界；命中同载荷缓存后不再读取鱼护，因为首次成功已经
// 把鱼移走。首次请求才验证本人在营地、鱼存在且允许展示，然后只调用 Items 原子转移入口。
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
	const FString StableNetId = CatResolveStableNetId(PlayerState);
	const FGuid SourceContainerId = Character->GetPersonalFishGuardId();
	const FGuid TargetContainerId = SharedFishTank->GetTankContainerId();
	if (!RequestId.IsValid() || !FishInstanceId.IsValid() || !SourceContainerId.IsValid() || !TargetContainerId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = FString::Printf(TEXT("%s|TankTransfer|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	const FString PayloadSignature = FString::Printf(
		TEXT("Fish=%s|Source=%s|Target=%s|GuardRevision=%lld|TankRevision=%lld"),
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		*TargetContainerId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedGuardRevision, ExpectedTankRevision);
	switch (CatQueryTerminalReplay(TankTransferTerminalCache, TankTransferPayloadByKey, CacheKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &CacheKey, &PayloadSignature](const FCatDomainCommandResult& TerminalResult)
	{
		TankTransferTerminalCache.Add(CacheKey, TerminalResult);
		TankTransferPayloadByKey.Add(CacheKey, PayloadSignature);
		return TerminalResult;
	};
	FCatContainerSnapshot GuardSnapshot;
	if (!Items->TryGetContainerSnapshot(SourceContainerId, GuardSnapshot))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
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
		return Finish(Result);
	}
	FCatFishTransferCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedGuardRevision;
	Command.Context.StableNetId = StableNetId;
	Command.FishInstanceId = FishInstanceId;
	Command.SourceContainerId = SourceContainerId;
	Command.TargetContainerId = TargetContainerId;
	Command.ExpectedTargetRevision = ExpectedTankRevision;
	return Finish(Items->TransferOwnedFish(Command));
}

// 篝火回看流程：先由服务器 UniqueId 与 RequestId 重放首次终态，再拒绝不在固定营地的请求者。随后按 Run 阶段裁决：普通
// 夜和两种结算夜都允许点亮篝火并给出回看入口；白天、NotStarted、Ending/Ended 以及读不到 GameState 时一律返回
// InvalidPhase，因此白天不存在篝火回看入口。只有结算夜且显式配置了封面事件才额外提交相册封面候选；围坐者只取当晚真正
// 处在营地范围内的玩家，缺 Controller、缺稳定身份或人不在营地的玩家被跳过而不是让整条命令失败，所以少人、有人中途离开
// 和单人局都能成立，一个围坐者都凑不出时才 fail-closed。封面候选仍先建齐全部 Planned 记录再逐人投递，任何预检或落盘失
// 败都缓存失败且不广播，不会留下“部分围坐者没有计划”的成功回放，离线造成的未投递由同一记录重试。全部必需事实成立后只
// 广播一次可跳过的表现意图并缓存成功，不写 next-day ready，也不等待客户端播放完成。
// 「先献祭仪式、后点亮篝火」这条顺序约束本轮不接：献祭窗口正在从白天迁到夜晚，迁移落地前这里读不到可信的“今晚献祭已收
// 束”事实，因此当前只按夜晚阶段放行，不校验献祭是否已完成。
FCatDomainCommandResult ACatCampHubActor::RequestCampfirePlayback(AController* RequestingController, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString StableNetId = CatResolveStableNetId(RequestingController);
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
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
	const ECatRunPhase Phase = GameState ? GameState->GetRunPublicState().Phase.Phase : ECatRunPhase::NotStarted;
	const bool bSettlementNight = Phase == ECatRunPhase::FailureSettlementNight
		|| Phase == ECatRunPhase::SuccessSettlementNight;
	if (!bSettlementNight && Phase != ECatRunPhase::NormalNight)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	if (bSettlementNight && Settings && !Settings->CampfireCoverEventId.IsNone())
	{
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
				continue;
			}
			// 候选预检拒绝重名参与者，所以这里按稳定身份去重，而不是把 PlayerArray 原样铺开。
			Candidate.ParticipantStableNetIds.AddUnique(CatResolveStableNetId(PlayerState));
		}
		Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
		UCatRunImprintService* Imprint = GetWorld()->GetSubsystem<UCatRunImprintService>();
		if (!Imprint || Candidate.ParticipantCount <= 0 || !Imprint->SubmitImprintCandidate(Candidate))
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
	}
	OnCampfirePlaybackRequested.Broadcast(RequestId);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
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
