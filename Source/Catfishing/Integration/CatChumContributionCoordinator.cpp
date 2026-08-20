#include "Integration/CatChumContributionCoordinator.h"

#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Logging/CatLog.h"
#include "UObject/Class.h"

namespace
{
	// 落点可达判定流程：遍历本 World 的水域几何，只要存在一片水域同时包含落点和投掷者所在位置就通过。
	// 命中即返回，不追求"唯一水域"——这里要的是"人和窝在同一片水里"，不是像 WaterQuery 那样解析出一个确定的区域身份，
	// 因此几片水域重叠不构成歧义。没有配置任何水域时必然返回 false，投窝会被判成 InvalidPayload 而不是静默放行。
	bool IsChumDropReachable(UWorld* World, const FVector& DropLocation, const FVector& ThrowerLocation)
	{
		if (!World)
		{
			return false;
		}
		for (TActorIterator<ACatWaterRegion> It(World); It; ++It)
		{
			if (It->ContainsWorldPoint(DropLocation) && It->ContainsWorldPoint(ThrowerLocation))
			{
				return true;
			}
		}
		return false;
	}
}

// 创建条件流程：只在服务器 Game World 建立这条链；客户端不能本地推进投窝。
bool UCatChumContributionCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 幂等键组合流程：身份在前、RequestId 在后，中间夹一个固定的用途段，使这条链的键空间和别的写口互不重叠。
FString UCatChumContributionCoordinator::MakeTerminalKey(const FString& StableNetId, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|PlayerChum|%s"), *StableNetId, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 投窝链流程：先固定载荷签名并查首次终态；确认是首次之后校验落点、窝料定义与可达性，再预留耗材、写入窝点、提交消耗。
// 顺序是"先预留后写窝"：窝点拒绝时释放预留，库里什么都不少；窝点写入成功后才把预留转成真实消耗，
// 这一步真失败也不回滚窝点——池已经加进去了，把耗材还回去等于凭空多出一份窝料，只记 Error 留痕。
FCatAggregationResult UCatChumContributionCoordinator::ContributePlayerChum(const FCatChumContributionCommand& Command,
	ACatCharacter* ThrowerCharacter)
{
	FCatAggregationResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty())
	{
		// 这两项缺一个就构造不出幂等键，只能当场拒绝且不进缓存；重试会重新走一遍同样的拒绝，不会产生副作用。
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatItems, Warning, TEXT("Event=player_chum_invalid_request RequestId=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return Result;
	}

	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId, Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("EquipmentRevision=%lld|ChumRevision=%lld|Chum=%s|Drop=%s"),
		Command.ExpectedEquipmentRevision, Command.Context.ExpectedRevision, *Command.ChumDefinitionId.ToString(),
		*Command.DropLocation.ToCompactString());
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature, Result,
		[](FCatAggregationResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		UE_LOG(LogCatItems, Warning, TEXT("Event=player_chum_payload_mismatch RequestId=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		// 生产入口 ServerContributeChum 没有回执 RPC，重放对客户端唯一的可见效果就是"什么都没再发生一次"；日志是它留下的唯一痕迹。
		UE_LOG(LogCatItems, Log, TEXT("Event=player_chum_replayed RequestId=%s Error=%s Revision=%lld"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		// 首次受理时模板不碰 Result，上面填好的 RequestId 原样留着继续用。
		break;
	}

	UWorld* World = GetWorld();
	UCatEquipmentComponent* Equipment = ThrowerCharacter ? ThrowerCharacter->GetEquipmentComponent() : nullptr;
	UCatEquipmentDefinition* ChumDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Command.ChumDefinitionId);
	UCatChumSpotSubsystem* ChumSpots = World ? World->GetSubsystem<UCatChumSpotSubsystem>() : nullptr;
	if (Command.DropLocation.ContainsNaN() || !ChumSpots || !ThrowerCharacter || !Equipment || !ChumDefinition
		|| ChumDefinition->Kind != ECatEquipmentKind::Chum || !ChumDefinition->ChumContribution.IsValidContribution()
		|| !IsChumDropReachable(World, Command.DropLocation, ThrowerCharacter->GetActorLocation()))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		FCatAggregationCommand Aggregation;
		Aggregation.Context = Command.Context;
		Aggregation.DropLocation = Command.DropLocation;
		Aggregation.Contribution = ChumDefinition->ChumContribution;
		Aggregation.Source = ECatAggregationSource::PlayerChum;
		Result.Command.Error = ChumSpots->ValidateChum(Aggregation);
		if (Result.Command.Error == ECatDomainCommandError::None)
		{
			const FCatDomainCommandResult ReserveResult = Equipment->ReserveRunConsumableFromAuthority(
				Command.Context.RequestId, Command.ExpectedEquipmentRevision, Command.ChumDefinitionId);
			if (!ReserveResult.bCommitted)
			{
				Result.Command.Error = ReserveResult.Error;
				Result.Command.Revision = Command.Context.ExpectedRevision;
			}
			else
			{
				Result = ChumSpots->ContributeChum(Aggregation);
				if (Result.Command.bCommitted)
				{
					const FCatDomainCommandResult ConsumeResult = Equipment->CommitReservedRunConsumableFromAuthority(
						Command.Context.RequestId, Command.ExpectedEquipmentRevision, Command.ChumDefinitionId);
					if (!ConsumeResult.bCommitted)
					{
						UE_LOG(LogCatItems, Error,
							TEXT("Event=player_chum_reserved_consume_failed RequestId=%s Error=%s EquipmentRevision=%lld"),
							*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
							*UEnum::GetValueAsString(ConsumeResult.Error), ConsumeResult.Revision);
					}
				}
				else
				{
					Equipment->ReleaseRunConsumableReservationFromAuthority(
						Command.Context.RequestId, Command.ExpectedEquipmentRevision, Command.ChumDefinitionId);
				}
			}
		}
	}
	TerminalCache.Add(TerminalKey, Result);
	TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
	UE_LOG(LogCatItems, Log,
		TEXT("Event=player_chum_terminal RequestId=%s Definition=%s Drop=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *Command.ChumDefinitionId.ToString(),
		*Command.DropLocation.ToCompactString(), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}
