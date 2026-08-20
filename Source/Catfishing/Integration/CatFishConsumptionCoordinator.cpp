#include "Integration/CatFishConsumptionCoordinator.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "UObject/Class.h"

// 创建条件流程：只在服务器 Game World 建立这条链；客户端不能本地推进进食。
bool UCatFishConsumptionCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 幂等键组合流程：身份、用途、源容器、RequestId 四段拼成一条键，和 Items 自己那份终态键形状一致，只是用途段不同。
FString UCatFishConsumptionCoordinator::MakeTerminalKey(const FString& StableNetId, const FGuid& ContainerId,
	const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|PlayerFishConsume|%s|%s"), *StableNetId,
		*ContainerId.ToString(EGuidFormats::DigitsWithHyphens), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 进食链流程：重放判定排在所有可变状态读取之前，然后依次做依赖、容器、身体、空间和鱼定义 preflight，
// 最后走 Items 不可逆移除与 Condition 结算。重放必须排在最前面的原因是：鱼一旦被吃掉就不在容器里了，
// 先读当前容器再判重放的话，一次网络重试会看到"鱼不见了"而报 NotFound，把一次成功说成失败。
FCatFishConsumeResult UCatFishConsumptionCoordinator::ConsumeFishFromContainer(const FCatFishConsumeCommand& Command,
	ACatCharacter* EatingCharacter)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const auto Reject = [&Command, &Result](const ECatDomainCommandError Error, const TCHAR* Reason)
	{
		Result.Command.Error = Error;
		UE_LOG(LogCatItems, Log, TEXT("Event=fish_consume_command_rejected RequestId=%s FishInstanceId=%s Reason=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Reason);
	};
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty())
	{
		// 这两项缺一个就构造不出幂等键，只能当场拒绝且不进缓存；重试会重新走一遍同样的拒绝，不会产生副作用。
		Reject(ECatDomainCommandError::InvalidPayload, TEXT("InvalidRequestOrIdentity"));
		return Result;
	}

	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId, Command.SourceContainerId,
		Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Fish=%s|ExpectedRevision=%lld"),
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Command.Context.ExpectedRevision);
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature, Result,
		[](FCatFishConsumeResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Reject(ECatDomainCommandError::InvalidPayload, TEXT("PayloadMismatch"));
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		// 生产入口 ServerConsumeFish 没有回执 RPC，重放对客户端唯一的可见效果就是"什么都没再发生一次"；日志是它留下的唯一痕迹。
		UE_LOG(LogCatItems, Log, TEXT("Event=fish_consume_replayed RequestId=%s Error=%s Revision=%lld"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		// 首次受理时模板不碰 Result，上面填好的 RequestId 原样留着继续用。
		break;
	}

	UWorld* World = GetWorld();
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatConditionComponent* Conditions = EatingCharacter ? EatingCharacter->GetConditionComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	FCatContainerSnapshot Source;
	ECatContainerKind SourceKind = ECatContainerKind::Unknown;
	AActor* SourceHost = nullptr;
	if (!Items || !Conditions)
	{
		Reject(ECatDomainCommandError::DependencyUnavailable, TEXT("DependencyUnavailable"));
	}
	else if (!Items->TryGetContainerSnapshot(Command.SourceContainerId, Source))
	{
		Reject(ECatDomainCommandError::NotFound, TEXT("SourceContainerNotFound"));
	}
	else if (Conditions->GetSnapshot().bDowned)
	{
		Reject(ECatDomainCommandError::InvalidPhase, TEXT("EaterDowned"));
	}
	else if (!Items->TryGetContainerHost(Command.SourceContainerId, SourceKind, SourceHost))
	{
		Reject(ECatDomainCommandError::NotFound, TEXT("SourceHostNotFound"));
	}
	// 共享缸是全队共用的固定营地设施，谁都能开，所以它唯一的准入是"人真的走到了缸边上"；
	// 个人鱼护挂在自己身上，不存在这个距离问题，Items 会按主人身份直接拒绝别人的鱼护。
	else if (SourceKind == ECatContainerKind::SharedFishTank
		&& (!CampSettings || !CampSettings->IsRuntimeReady() || !SourceHost
			|| FVector::DistSquared(EatingCharacter->GetActorLocation(), SourceHost->GetActorLocation())
				> FMath::Square(CampSettings->InteractionRadiusCentimeters)))
	{
		Reject(ECatDomainCommandError::PermissionDenied, TEXT("SharedTankOutOfRangeOrCampNotReady"));
	}
	else
	{
		const FCatFishInstance* Fish = Source.Fish.FindByPredicate([&Command](const FCatFishInstance& Candidate)
		{
			return Candidate.FishInstanceId == Command.FishInstanceId;
		});
		UCatFishDefinition* Definition = Fish
			? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId) : nullptr;
		if (!Definition)
		{
			Reject(ECatDomainCommandError::NotFound, TEXT("FishOrDefinitionNotFound"));
		}
		else if (const ECatDomainCommandError BodyPreflight = Conditions->ValidateFishConsumption(Definition);
			BodyPreflight != ECatDomainCommandError::None)
		{
			Reject(BodyPreflight, *UEnum::GetValueAsString(BodyPreflight));
		}
		else
		{
			Result = Items->ConsumeFish(Command);
			if (Result.Command.bCommitted)
			{
				Conditions->ConsumeCommittedFish(Command.Context.RequestId, Definition);
			}
		}
	}
	TerminalCache.Add(TerminalKey, Result);
	TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
	UE_LOG(LogCatItems, Log,
		TEXT("Event=fish_consume_terminal RequestId=%s FishInstanceId=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}
