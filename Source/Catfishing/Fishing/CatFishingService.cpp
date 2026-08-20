#include "Fishing/CatFishingService.h"

#include "Framework/Core/CatStableNetId.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/PlayerState.h"
#include "Integration/Fishing/CatFishingBoundarySubsystem.h"
#include "Logging/CatLog.h"
#include "Math/RandomStream.h"
#include "Social/CatSocialService.h"

// 落点解析流程：先取钓手身上的漂并要求它进了运行目录、射程与精准度都是正数；再把朝向压到水平面归一化，
// 沿这个方向推出一个射程，最后叠一个半径由精准度决定的圆内随机偏移。任何一步不成立都返回 false 并保持输出为零向量。
bool UCatFishingService::TryResolveFloatCastLocation(const ACatCharacter& Fisher, const FGuid& RequestId,
	FVector& OutCastLocation)
{
	OutCastLocation = FVector::ZeroVector;
	const UCatEquipmentComponent* Equipment = Fisher.GetEquipmentComponent();
	const FName FloatDefinitionId = Equipment ? Equipment->GetSnapshot().FloatDefinitionId : NAME_None;
	const UCatEquipmentDefinition* FloatDefinition = FloatDefinitionId.IsNone()
		? nullptr : GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(FloatDefinitionId);
	if (!FloatDefinition || FloatDefinition->Kind != ECatEquipmentKind::Float
		|| !FMath::IsFinite(FloatDefinition->FloatCastRangeMeters) || FloatDefinition->FloatCastRangeMeters <= 0.0
		|| !FMath::IsFinite(FloatDefinition->FloatAccuracyOffsetRadiusMeters)
		|| FloatDefinition->FloatAccuracyOffsetRadiusMeters <= 0.0)
	{
		return false;
	}
	const double CastRangeMeters = FloatDefinition->FloatCastRangeMeters;
	const double AccuracyOffsetRadiusMeters = FloatDefinition->FloatAccuracyOffsetRadiusMeters;
	FVector CastDirection = Fisher.GetActorForwardVector();
	CastDirection.Z = 0.0;
	if (!CastDirection.Normalize())
	{
		// 朝向退化成纯竖直（猫被摆成朝天或朝地）时没有水平方向可用，算不出落点，宁可不抛也不随便挑一个方向。
		return false;
	}
	FRandomStream OffsetStream(static_cast<int32>(GetTypeHash(RequestId)));
	// 在圆内均匀取点要对半径开方，否则点会挤到圆心；这里的偏移代表"漂没落在瞄的地方"，均匀分布才符合"越不准偏得越开"。
	const double OffsetRadius = AccuracyOffsetRadiusMeters * 100.0
		* FMath::Sqrt(static_cast<double>(OffsetStream.GetFraction()));
	const double OffsetAngleRadians = static_cast<double>(OffsetStream.GetFraction()) * 2.0 * UE_DOUBLE_PI;
	const FVector Offset(OffsetRadius * FMath::Cos(OffsetAngleRadians),
		OffsetRadius * FMath::Sin(OffsetAngleRadians), 0.0);
	OutCastLocation = Fisher.GetActorLocation() + CastDirection * (CastRangeMeters * 100.0) + Offset;
	return true;
}

namespace
{
	// 错误映射流程：Boundary 错误只描述协调合同，Service 返回值仍沿用既有 DomainCommandError 语义。
	ECatDomainCommandError MapBoundaryErrorToDomain(const ECatFishingBoundaryError Error)
	{
		switch (Error)
		{
		case ECatFishingBoundaryError::None:
			return ECatDomainCommandError::None;
		case ECatFishingBoundaryError::InvalidIdentity:
			return ECatDomainCommandError::InvalidIdentity;
		case ECatFishingBoundaryError::InvalidOrder:
		case ECatFishingBoundaryError::AttemptClosed:
			return ECatDomainCommandError::InvalidPhase;
		case ECatFishingBoundaryError::RevisionConflict:
			return ECatDomainCommandError::RevisionConflict;
		case ECatFishingBoundaryError::PermissionDenied:
			return ECatDomainCommandError::PermissionDenied;
		case ECatFishingBoundaryError::CapacityExceeded:
			return ECatDomainCommandError::CapacityExceeded;
		case ECatFishingBoundaryError::AlreadySettled:
			return ECatDomainCommandError::AlreadyResolved;
		case ECatFishingBoundaryError::CancelledBeforeCommit:
		case ECatFishingBoundaryError::TimedOutBeforeCommit:
			return ECatDomainCommandError::Cancelled;
		case ECatFishingBoundaryError::OperationNotFound:
		case ECatFishingBoundaryError::ResultExpired:
			return ECatDomainCommandError::NotFound;
		case ECatFishingBoundaryError::PolicyUndecided:
			return ECatDomainCommandError::PolicyUndecided;
		case ECatFishingBoundaryError::DependencyUnavailable:
			return ECatDomainCommandError::DependencyUnavailable;
		case ECatFishingBoundaryError::UnsupportedSchema:
		case ECatFishingBoundaryError::InvalidAttempt:
		case ECatFishingBoundaryError::InvalidRequest:
		case ECatFishingBoundaryError::PayloadMismatch:
		case ECatFishingBoundaryError::CursorGap:
		default:
			return ECatDomainCommandError::InvalidPayload;
		}
	}
}
// 创建条件流程：仅 authority Game World 持有会话索引；客户端不能创建平行 StateTree。
bool UCatFishingService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭并终止所有会话，再清弱映射；随后交还 WorldSubsystem 生命周期。
void UCatFishingService::Deinitialize()
{
	CloseCommandsAndTerminateAll();
	Sessions.Reset();
	SessionFisherById.Reset();
	ActiveSessionByFisher.Reset();
	StartResultByBoundaryOperation.Reset();
	StartResultByAttempt.Reset();
	Super::Deinitialize();
}

// 会话创建流程：Service 保留 RPC 入口、单活跃槽位和 Session 注册；Start/Cast 的身份、只读采集和 encounter 冻结交给
// Boundary，避免旧路径在 Start 中直接抽鱼。
FCatFishingStartResult UCatFishingService::StartFishingSession(AController* FisherController, const FGuid RequestId)
{
	FCatFishingStartResult Result;
	Result.RequestId = RequestId;
	CompactSessions();

	UCatFishingBoundarySubsystem* Boundary = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	if (!Boundary)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	const FCatFishingBoundaryStartResult StartResult = Boundary->Start(FisherController, RequestId);
	if (StartResult.Header.Disposition != ECatFishingBoundaryDisposition::Committed)
	{
		Result.Error = MapBoundaryErrorToDomain(StartResult.Header.Error);
		return Result;
	}

	// 重放保护：Start 已接受后的 Service 终态先按 Attempt 返回，避免重试时被当前命令、会话或 Pawn 生命周期改写。
	const FGuid AttemptCacheKey = StartResult.Context.AttemptId.Value;
	if (StartResult.Header.bReplay)
	{
		if (const FCatFishingStartResult* CachedByAttempt = StartResultByAttempt.Find(AttemptCacheKey))
		{
			return *CachedByAttempt;
		}
	}
	// 这个 lambda 是"Start 已经受理、但会话最终没建起来"这一类失败的唯一终点（成功路径走下面那个 Finish），
	// 所以 attempt 的收口写在这里，而不是靠每条出口各自记得调一次。attempt 是 Start 那一刻在 Boundary 里开的，
	// 没有会话来接手它，它就永远停在半开状态：AttemptId 对 Bait、Fight 这些后半程协议仍然合法，可谁都不会再推进它、
	// 也没人能终止它，于是按失败次数一路堆到 World 反初始化。
	// 之前这条约定确实是靠各出口自觉执行的，五条里漏了三条（撞上已有会话、取不到身体、Cast 被拒）——
	// 收进唯一终点之后就不存在"下一个人忘了加"这回事了。
	const auto FinishAttempt = [this, AttemptCacheKey, Boundary, AttemptId = StartResult.Context.AttemptId](
		const FCatFishingStartResult& TerminalResult)
	{
		Boundary->CloseAttempt(AttemptId);
		if (AttemptCacheKey.IsValid())
		{
			StartResultByAttempt.Add(AttemptCacheKey, TerminalResult);
		}
		return TerminalResult;
	};

	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return FinishAttempt(Result);
	}

	const FString StableNetId = StartResult.Context.PrincipalId.CanonicalValue;
	if (ActiveSessionByFisher.Contains(StableNetId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return FinishAttempt(Result);
	}

	ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
	if (!Character)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return FinishAttempt(Result);
	}
	FVector CastWorldLocation = FVector::ZeroVector;
	if (!TryResolveFloatCastLocation(*Character, RequestId, CastWorldLocation))
	{
		// 没有可用的漂就没有落点，也就没有 D₀；这里 fail-closed 而不是退回"落在猫脚下"，
		// 否则遛鱼会从 0 米开局，等于把一条产品规则悄悄换成一个占位值。
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_cast_float_unavailable RequestId=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return FinishAttempt(Result);
	}
	FCatFishingCastAcceptedRequest CastRequest;
	CastRequest.RequestId = RequestId;
	CastRequest.AttemptId = StartResult.Context.AttemptId;
	CastRequest.CastWorldLocation = CastWorldLocation;
	CastRequest.ExpectedRunRevision = StartResult.Context.RunRevision;
	const FCatFishingBoundaryCastResult CastResult = Boundary->CastAccepted(FisherController, CastRequest);
	if (CastResult.Header.Disposition != ECatFishingBoundaryDisposition::Committed)
	{
		Result.Error = MapBoundaryErrorToDomain(CastResult.Header.Error);
		return FinishAttempt(Result);
	}

	const FString OperationCacheKey = CastResult.Header.Operation.ToCacheKey();
	if (const FCatFishingStartResult* Cached = StartResultByBoundaryOperation.Find(OperationCacheKey))
	{
		StartResultByAttempt.Add(AttemptCacheKey, *Cached);
		return *Cached;
	}
	const auto Finish = [this, &OperationCacheKey, AttemptCacheKey](const FCatFishingStartResult& TerminalResult)
	{
		StartResultByBoundaryOperation.Add(OperationCacheKey, TerminalResult);
		StartResultByAttempt.Add(AttemptCacheKey, TerminalResult);
		return TerminalResult;
	};

	// 下面三条出口都在 Cast 已经提交之后：CastAccepted 那一刻已经把 operation 写成 Committed，并把鱼种、重量、水域和
	// D₀ 冻结成一份 EncounterSpec，这份规格本来是要交给马上要建的会话去消费的。会话没建起来，它就没有主人了——而
	// Boundary 侧的 AttemptId 对 Bait、Fight 这些后半程协议仍然合法，只要不收口，Journal 里就留着一个谁都不会再推进、
	// 也没人能终止的半开 attempt，按失败次数一路堆到 World 反初始化。所以这里沿用 Cast 之前那两条出口的同一个约定：
	// 没造出会话就把 attempt 收掉。收口只挡这个 Attempt 上的新 operation，已接受的 Start/Cast 结果照样能被重放读到，
	// 网络重试仍然拿回同一个终态。
	if (ActiveSessionByFisher.Contains(StableNetId))
	{
		Boundary->CloseAttempt(StartResult.Context.AttemptId);
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}

	UCatFishDefinition* FishDefinition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(
		CastResult.EncounterSpec.FishDefinitionId);
	if (!FishDefinition)
	{
		Boundary->CloseAttempt(StartResult.Context.AttemptId);
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}

	ACatFishingSession* Session = GetWorld()->SpawnActor<ACatFishingSession>();
	if (!Session || !Session->InitializeSession(FisherController, Character, FishDefinition,
		StartResult.Context.FisherGuardContainerId, CastResult.EncounterSpec))
	{
		if (Session)
		{
			Session->Destroy();
		}
		Boundary->CloseAttempt(StartResult.Context.AttemptId);
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}

	const FGuid SessionId = Session->GetSnapshot().FishingSessionId;
	Sessions.Add(SessionId, Session);
	SessionFisherById.Add(SessionId, StableNetId);
	ActiveSessionByFisher.Add(StableNetId, SessionId);
	Result.bStarted = true;
	Result.FishingSessionId = SessionId;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_session_started RequestId=%s SessionId=%s Fish=%s Region=%s Giant=%s WeightKg=%.3f BiteIntervalSeconds=%.3f"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *SessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*CastResult.EncounterSpec.FishDefinitionId.ToString(), *CastResult.EncounterSpec.WaterRegion.RegionId.ToString(),
		CastResult.EncounterSpec.bGiant ? TEXT("true") : TEXT("false"), CastResult.EncounterSpec.FishWeightKilograms,
		CastResult.EncounterSpec.BiteIntervalSeconds);
	if (CastResult.EncounterSpec.bGiant)
	{
		if (UCatSocialService* Social = GetWorld()->GetSubsystem<UCatSocialService>())
		{
			Social->BroadcastGiantFishingPrompt(FisherController, SessionId);
		}
	}
	return Finish(Result);
}
// 协作转发流程：先清理终态或失效弱引用并定位真实 Session，未找到返回 NotFound；找到后由会话统一校验
// Giant/HookedFight/Revision，以及请求者仍是 Active Controller、持有当前 Character、未倒地且力量/体力为正，任何拒绝都
// 发生在参与集合写入前。
FCatDomainCommandResult UCatFishingService::SubmitFightAssist(const FGuid FishingSessionId,
	AController* AssistingController, const FGuid RequestId, const int64 ExpectedRevision)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		return Session->SubmitFightAssist(AssistingController, RequestId, ExpectedRevision);
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// 遛鱼意图转发流程：先清理终态弱引用，按 Controller 的服务器身份查单活跃会话索引；找到就转给会话（会话再复核身份与阶段），找不到返回 NotFound。
FCatDomainCommandResult UCatFishingService::SubmitFightIntent(AController* FisherController, const ECatFishingFightIntent Intent)
{
	CompactSessions();
	const FString StableNetId = CatResolveStableNetId(FisherController);
	const FGuid SessionId = ActiveSessionByFisher.FindRef(StableNetId);
	if (ACatFishingSession* Session = Sessions.FindRef(SessionId).Get())
	{
		return Session->SubmitFightIntent(FisherController, Intent);
	}
	FCatDomainCommandResult Result;
	Result.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// 抢抄转发流程：只定位 Session 并转发；首个合法胜者与 FishInstance 创建完全在 Session→Items Compare-and-Commit 中决定。
FCatScoopResult UCatFishingService::RequestScoop(const FGuid FishingSessionId, AController* ScoopingController,
	const FCatScoopCommand& Command)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		const FCatScoopResult Result = Session->RequestScoop(ScoopingController, Command);
		CompactSessions();
		return Result;
	}
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// Character 中断流程：遍历存活弱引用，精确命中钓手或协作者后终止；Resolved 会话自行保持终态。
void UCatFishingService::TerminateSessionsForCharacter(const ACatCharacter* Character)
{
	CompactSessions();
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get(); Session && Session->InvolvesCharacter(Character))
		{
			Session->TerminateSession(TEXT("Character unavailable"));
		}
	}
	CompactSessions();
}

// Teardown 流程：永久关闭新入口，并让每个存活会话进入 Terminated；不等待旧半场或创建补偿鱼。
void UCatFishingService::CloseCommandsAndTerminateAll()
{
	bCommandsOpen = false;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get())
		{
			Session->TerminateSession(TEXT("Run teardown"));
		}
	}
}

// 弱索引压缩流程：移除已销毁或 Resolved/Terminated 会话，并用会话到身份反向键释放精确单活跃槽位；开始终态缓存保留供网络重放。
void UCatFishingService::CompactSessions()
{
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		ACatFishingSession* Session = It.Value().Get();
		if (!Session || Session->IsTerminal())
		{
			const FGuid SessionId = It.Key();
			const FString StableNetId = SessionFisherById.FindRef(SessionId);
			if (!StableNetId.IsEmpty() && ActiveSessionByFisher.FindRef(StableNetId) == SessionId)
			{
				ActiveSessionByFisher.Remove(StableNetId);
			}
			SessionFisherById.Remove(SessionId);
			It.RemoveCurrent();
		}
	}
}
