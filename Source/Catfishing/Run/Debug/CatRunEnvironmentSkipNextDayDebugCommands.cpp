#include "Catfishing.h"

#include "Framework/Game/CatGameplayTypes.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
	/** 跳天调试续步的等待间隔，表示 QuotaReached 事件交给 StateTree 后下一次检查夜晚阶段的服务器秒数。 */
	constexpr float CatDebugSkipToNextDayReadyDelaySeconds = 0.10f;

	/** 跳天调试续步的最大检查次数，限制 StateTree 没有进入 NormalNight 时最多等待约一秒后停止。 */
	constexpr int32 CatDebugSkipToNextDayReadyMaxAttempts = 10;

	/** 跳天完成确认的等待间隔，表示 ready 事件发出后下一次检查是否已经进入下一天白天的服务器秒数。 */
	constexpr float CatDebugSkipToNextDayAdvanceDelaySeconds = 0.10f;

	/** 跳天完成确认的最大检查次数，限制 StateTree 事件迟迟没有推进到下一天时最多等待约两秒后给出明确诊断。 */
	constexpr int32 CatDebugSkipToNextDayAdvanceMaxAttempts = 20;

	// NetMode 格式化流程：把引擎枚举转成人能直接对照的短文本，便于房主端和客户端用同一日志字段排查。
	FString FormatSkipToNextDayNetMode(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone:
			return TEXT("Standalone");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Client:
			return TEXT("Client");
		default:
			return TEXT("Unknown");
		}
	}

	// 布尔值格式化流程：日志统一输出 true/false，避免人工对账时混用 0/1、Yes/No 等不同口径。
	const TCHAR* FormatSkipToNextDayBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	// 跳天快照日志流程：从服务器 GameMode 的唯一 RunPublicState 读事实并一次性展开；它只记录正式写口完成后的同步结果，不写任何阶段、天气或表现状态。
	void LogSkipToNextDaySnapshot(UWorld& World, const ACatfishingGameModeBase& GameMode, const TCHAR* Trigger)
	{
		const FCatRunPublicState& RunState = GameMode.GetRunPublicState();
		const bool bEnvironmentRevisionMatches = RunState.Environment.SourceRunRevision == RunState.Revision;
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_snapshot Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld EnvRevision=%lld EnvRevisionMatch=%s Day=%d Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d EndReason=%s"),
			Trigger, *World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Environment.SourceRunRevision, FormatSkipToNextDayBool(bEnvironmentRevisionMatches),
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
			FormatSkipToNextDayBool(RunState.Phase.bHasDeadline),
			FormatSkipToNextDayBool(RunState.Phase.bFishingAllowed),
			FormatSkipToNextDayBool(RunState.Phase.bQuotaOpen),
			RunState.QuotaProgress, RunState.QuotaTarget,
			*UEnum::GetValueAsString(RunState.EndReason));
	}

	// 调试命令玩家选择流程：只从服务器当前可见 Controller 中挑第一名仍能走玩法命令 gate 的玩家；找不到时保持拒绝，避免伪造系统玩家命令或本地状态。
	APlayerController* FindFirstSkipToNextDayController(UWorld& World, const ACatfishingGameModeBase& GameMode)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && GameMode.CanAcceptGameplayCommand(Controller))
			{
				return Controller;
			}
		}
		return nullptr;
	}

	// 白天跳天提交流程：读取当前公开快照后只构造一条“补足额度”的正式 SubmitQuotaContribution 命令；这是指令对白天的唯一写入方式，不直接写 DayIndex 或 Phase，达标后的夜晚进入仍由 StateTree 消费 QuotaReached 事件完成。
	bool SubmitSkipToNextDayQuotaReached(UWorld& World, ACatfishingGameModeBase& GameMode,
		FGuid& OutRunId, int32& OutDayIndex)
	{
		const FCatRunPublicState& RunState = GameMode.GetRunPublicState();
		OutRunId = RunState.Phase.RunId;
		OutDayIndex = RunState.Phase.DayIndex;
		if (RunState.Phase.Phase != ECatRunPhase::DayActive || !RunState.Phase.bHasDeadline
			|| !RunState.Phase.bFishingAllowed || !RunState.Phase.bQuotaOpen || RunState.QuotaTarget <= 0)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=DayNotOpen World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d"),
				*World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
				FormatSkipToNextDayBool(RunState.Phase.bHasDeadline),
				FormatSkipToNextDayBool(RunState.Phase.bFishingAllowed),
				FormatSkipToNextDayBool(RunState.Phase.bQuotaOpen),
				RunState.QuotaProgress, RunState.QuotaTarget);
			return false;
		}

		const int64 RequiredContribution = static_cast<int64>(RunState.QuotaTarget) - RunState.QuotaProgress;
		if (RequiredContribution <= 0 || RequiredContribution > MAX_int32)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=InvalidRequiredContribution World=%s NetMode=%s RunId=%s Revision=%lld Day=%d QuotaProgress=%d QuotaTarget=%d RequiredContribution=%lld"),
				*World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				RunState.Phase.DayIndex, RunState.QuotaProgress, RunState.QuotaTarget, RequiredContribution);
			return false;
		}

		APlayerController* Controller = FindFirstSkipToNextDayController(World, GameMode);
		if (!Controller)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=NoActiveController World=%s NetMode=%s RunId=%s Revision=%lld Day=%d"),
				*World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				RunState.Phase.DayIndex);
			return false;
		}

		FCatQuotaContributionCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = RunState.Revision;
		Command.Contribution = static_cast<int32>(RequiredContribution);

		const FCatRunCommandResult Result = GameMode.SubmitQuotaContribution(Controller, Command);
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_quota_submitted World=%s NetMode=%s Controller=%s RequestId=%s Contribution=%d Committed=%s Error=%s ResultRevision=%lld ResultPhase=%s TransitionReason=%s"),
			*World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()), *GetNameSafe(Controller),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Command.Contribution,
			FormatSkipToNextDayBool(Result.bCommitted), *UEnum::GetValueAsString(Result.Error),
			Result.Revision, *UEnum::GetValueAsString(Result.Phase),
			*UEnum::GetValueAsString(Result.TransitionReason));
		return Result.bCommitted && Result.TransitionReason == ECatRunTransitionReason::QuotaReached;
	}

	// 跳天完成确认安排声明：ready 提交后等待 StateTree 下一帧真正进入新的 DayActive，而不是把事件入队误判为已经翻天。
	void ScheduleSkipToNextDayAdvanceConfirmation(UWorld& World, FGuid ExpectedRunId,
		int32 CompletedDayIndex, FString Trigger, int32 Attempt);

	// 夜晚 ready 提交流程：每名服务器可见且仍通过玩法命令 gate 的玩家都走正式 SubmitNextDayReady；每次提交前重读 Revision，避免第一名 ready 后后续玩家拿旧版本，本流程不直接设置下一天。
	bool SubmitSkipToNextDayReadyForVisiblePlayers(UWorld& World, ACatfishingGameModeBase& GameMode,
		const TCHAR* Trigger)
	{
		const FCatRunPublicState& InitialState = GameMode.GetRunPublicState();
		if (InitialState.Phase.Phase != ECatRunPhase::NormalNight)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=ReadyRequiresNormalNight Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
				Trigger, *World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
				*InitialState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), InitialState.Revision,
				InitialState.Phase.DayIndex, *UEnum::GetValueAsString(InitialState.Phase.Phase));
			return false;
		}

		int32 AttemptedCount = 0;
		int32 CommittedCount = 0;
		int32 RejectedCount = 0;
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller || !GameMode.CanAcceptGameplayCommand(Controller))
			{
				continue;
			}

			const FCatRunPublicState& CurrentState = GameMode.GetRunPublicState();
			if (CurrentState.Phase.Phase != ECatRunPhase::NormalNight)
			{
				break;
			}

			FCatNextDayReadyCommand Command;
			Command.Context.RequestId = FGuid::NewGuid();
			Command.Context.ExpectedRevision = CurrentState.Revision;
			Command.bReady = true;

			const FCatRunCommandResult Result = GameMode.SubmitNextDayReady(Controller, Command);
			++AttemptedCount;
			if (Result.bCommitted)
			{
				++CommittedCount;
			}
			else
			{
				++RejectedCount;
			}
			UE_LOG(LogCatRun, Display,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_ready_submitted Trigger=%s World=%s NetMode=%s Controller=%s RequestId=%s Committed=%s Error=%s ResultRevision=%lld ResultPhase=%s TransitionReason=%s"),
				Trigger, *World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()), *GetNameSafe(Controller),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				FormatSkipToNextDayBool(Result.bCommitted), *UEnum::GetValueAsString(Result.Error),
				Result.Revision, *UEnum::GetValueAsString(Result.Phase),
				*UEnum::GetValueAsString(Result.TransitionReason));
		}

		const FCatRunPublicState& FinalState = GameMode.GetRunPublicState();
		const FCatRunAuthorityDebugSnapshot AuthoritySnapshot = GameMode.GetAuthorityDebugSnapshotForDebug();
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_ready_summary Trigger=%s World=%s NetMode=%s Attempted=%d Committed=%d Rejected=%d Eligible=%d Ready=%d AllReadyEventSent=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
			Trigger, *World.GetName(), *FormatSkipToNextDayNetMode(World.GetNetMode()),
			AttemptedCount, CommittedCount, RejectedCount,
			AuthoritySnapshot.NightReadyEligibleCount, AuthoritySnapshot.NightReadyCount,
			FormatSkipToNextDayBool(AuthoritySnapshot.bAllEligibleReadyEventSent),
			*FinalState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), FinalState.Revision,
			FinalState.Phase.DayIndex, *UEnum::GetValueAsString(FinalState.Phase.Phase));
		return AttemptedCount > 0 && RejectedCount == 0
			&& (FinalState.Phase.Phase != ECatRunPhase::NormalNight
				|| AuthoritySnapshot.bAllEligibleReadyEventSent);
	}

	// 跳天完成判断流程：只把同一 Run 中天数变大且回到 DayActive 视为完成；夜晚、结算和 RunId 变化都不被伪装成普通翻天成功。
	bool HasReachedExpectedNextDay(const FCatRunPublicState& RunState, const FGuid& ExpectedRunId,
		const int32 CompletedDayIndex)
	{
		return RunState.Phase.RunId == ExpectedRunId
			&& RunState.Phase.Phase == ECatRunPhase::DayActive
			&& RunState.Phase.DayIndex > CompletedDayIndex;
	}

	// 跳天完成确认回调声明：由 one-shot timer 等待 StateTree 消费 ready 事件后检查公开 Phase。
	void HandleSkipToNextDayAdvanceElapsed(TWeakObjectPtr<UWorld> WorldPtr, FGuid ExpectedRunId,
		int32 CompletedDayIndex, FString Trigger, int32 Attempt);

	// 跳天完成确认安排流程：把当前 RunId 与完成的旧天数带到下一帧检查；它只读取 GameMode 快照和日志，不重发 ready 或直接写 Phase。
	void ScheduleSkipToNextDayAdvanceConfirmation(UWorld& World, const FGuid ExpectedRunId,
		const int32 CompletedDayIndex, const FString Trigger, const int32 Attempt)
	{
		FTimerHandle TimerHandle;
		World.GetTimerManager().SetTimer(TimerHandle,
			FTimerDelegate::CreateStatic(&HandleSkipToNextDayAdvanceElapsed, TWeakObjectPtr<UWorld>(&World),
				ExpectedRunId, CompletedDayIndex, Trigger, Attempt),
			CatDebugSkipToNextDayAdvanceDelaySeconds, false);
	}

	// 跳天完成确认流程：ready 事件已发后只等待公开状态回到下一天白天；超时会带上服务器私有 ready/StateTree 信息，方便区分“事件没转移”和“客户端没刷新”。
	void HandleSkipToNextDayAdvanceElapsed(TWeakObjectPtr<UWorld> WorldPtr, const FGuid ExpectedRunId,
		const int32 CompletedDayIndex, const FString Trigger, const int32 Attempt)
	{
		UWorld* World = WorldPtr.Get();
		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!World || !GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=AdvanceGameModeUnavailable Trigger=%s Attempt=%d"),
				*Trigger, Attempt);
			return;
		}

		const FCatRunPublicState& RunState = GameMode->GetRunPublicState();
		if (HasReachedExpectedNextDay(RunState, ExpectedRunId, CompletedDayIndex))
		{
			UE_LOG(LogCatRun, Display,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_advanced Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld PreviousDay=%d CurrentDay=%d Phase=%s Attempt=%d"),
				*Trigger, *World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				CompletedDayIndex, RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
			LogSkipToNextDaySnapshot(*World, *GameMode, TEXT("AdvanceConfirmed"));
			return;
		}
		if (RunState.Phase.RunId != ExpectedRunId)
		{
			UE_LOG(LogCatRun, Display,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_stale_advance_check Trigger=%s World=%s NetMode=%s ExpectedRunId=%s ActualRunId=%s ExpectedCompletedDay=%d ActualDay=%d Revision=%lld Phase=%s Attempt=%d"),
				*Trigger, *World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
				*ExpectedRunId.ToString(EGuidFormats::DigitsWithHyphens),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
				CompletedDayIndex, RunState.Phase.DayIndex, RunState.Revision,
				*UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
			return;
		}
		if (Attempt >= CatDebugSkipToNextDayAdvanceMaxAttempts)
		{
			const FCatRunAuthorityDebugSnapshot AuthoritySnapshot = GameMode->GetAuthorityDebugSnapshotForDebug();
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=NextDayNotReached Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld PreviousDay=%d CurrentDay=%d Phase=%s Eligible=%d Ready=%d AllReadyEventSent=%s StateTreeAssigned=%s StateTreeRunning=%s LastReason=%s LastError=%s Attempt=%d"),
				*Trigger, *World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				CompletedDayIndex, RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
				AuthoritySnapshot.NightReadyEligibleCount, AuthoritySnapshot.NightReadyCount,
				FormatSkipToNextDayBool(AuthoritySnapshot.bAllEligibleReadyEventSent),
				FormatSkipToNextDayBool(AuthoritySnapshot.bRunStateTreeAssigned),
				FormatSkipToNextDayBool(AuthoritySnapshot.bRunStateTreeRunning),
				*UEnum::GetValueAsString(AuthoritySnapshot.LastRunFlowResult.Reason),
				*UEnum::GetValueAsString(AuthoritySnapshot.LastRunFlowResult.Error), Attempt);
			return;
		}

		UE_LOG(LogCatRun, Log,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_waiting_for_advance Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld PreviousDay=%d CurrentDay=%d Phase=%s Attempt=%d"),
			*Trigger, *World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			CompletedDayIndex, RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
		ScheduleSkipToNextDayAdvanceConfirmation(*World, ExpectedRunId, CompletedDayIndex, Trigger, Attempt + 1);
	}

	// 白天跳天续步回调声明：由 one-shot timer 延后执行，真正提交 ready 前会重新核对 World、RunId 和 DayIndex。
	void HandleSkipToNextDayReadyElapsed(TWeakObjectPtr<UWorld> WorldPtr, FGuid ExpectedRunId,
		int32 ExpectedDayIndex, int32 Attempt);

	// 白天跳天续步安排流程：丢弃 TimerHandle 只表示不提供人工取消入口，TimerManager 仍持有本次 one-shot delegate 直到触发或 World 销毁；这里不保存额外玩法状态。
	void ScheduleSkipToNextDayReadyContinuation(UWorld& World, const FGuid ExpectedRunId,
		const int32 ExpectedDayIndex, const int32 Attempt)
	{
		FTimerHandle TimerHandle;
		World.GetTimerManager().SetTimer(TimerHandle,
			FTimerDelegate::CreateStatic(&HandleSkipToNextDayReadyElapsed, TWeakObjectPtr<UWorld>(&World),
				ExpectedRunId, ExpectedDayIndex, Attempt),
			CatDebugSkipToNextDayReadyDelaySeconds, false);
	}

	// 白天跳天续步执行流程：先确认 World 和 authority GameMode 仍有效，再用原 RunId/DayIndex 拦截迟到回调；只有看见 NormalNight 才提交正式 ready 写口，否则有限次数重试后暴露 StateTree 未进夜晚。
	void HandleSkipToNextDayReadyElapsed(TWeakObjectPtr<UWorld> WorldPtr, const FGuid ExpectedRunId,
		const int32 ExpectedDayIndex, const int32 Attempt)
	{
		UWorld* World = WorldPtr.Get();
		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!World || !GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=ContinuationGameModeUnavailable Attempt=%d"),
				Attempt);
			return;
		}

		const FCatRunPublicState& RunState = GameMode->GetRunPublicState();
		if (RunState.Phase.RunId != ExpectedRunId || RunState.Phase.DayIndex != ExpectedDayIndex)
		{
			UE_LOG(LogCatRun, Display,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_stale_continuation World=%s NetMode=%s ExpectedRunId=%s ActualRunId=%s ExpectedDay=%d ActualDay=%d Revision=%lld Phase=%s Attempt=%d"),
				*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
				*ExpectedRunId.ToString(EGuidFormats::DigitsWithHyphens),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens),
				ExpectedDayIndex, RunState.Phase.DayIndex, RunState.Revision,
				*UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
			return;
		}

		if (RunState.Phase.Phase == ECatRunPhase::NormalNight)
		{
			if (SubmitSkipToNextDayReadyForVisiblePlayers(*World, *GameMode, TEXT("Continuation")))
			{
				LogSkipToNextDaySnapshot(*World, *GameMode, TEXT("Continuation"));
				ScheduleSkipToNextDayAdvanceConfirmation(*World, ExpectedRunId, ExpectedDayIndex,
					TEXT("Continuation"), 1);
			}
			return;
		}

		if (Attempt >= CatDebugSkipToNextDayReadyMaxAttempts)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=NormalNightNotReached World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s Attempt=%d"),
				*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
				*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
				RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
			return;
		}

		UE_LOG(LogCatRun, Log,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_waiting_for_night World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s Attempt=%d"),
			*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase), Attempt);
		ScheduleSkipToNextDayReadyContinuation(*World, ExpectedRunId, ExpectedDayIndex, Attempt + 1);
	}

	// 跳天指令入口流程：拒绝任何参数和非 authority World；白天走“SubmitQuotaContribution 补额度 -> 等 StateTree 进 NormalNight -> SubmitNextDayReady 续步”，普通夜晚直接提交 ready，其余阶段只写拒绝日志；整个入口不直接改 DayIndex、Phase 或客户端状态。
	void SkipRunEnvironmentSocialToNextDayForWorld(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty())
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=InvalidArguments Usage=\"cat.RunEnvironmentSocial.SkipToNextDay\""));
			return;
		}

		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!World || !GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=AuthorityGameModeUnavailable World=%s NetMode=%s"),
				World ? *World->GetName() : TEXT("None"),
				World ? *FormatSkipToNextDayNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}

		const FCatRunPublicState& RunState = GameMode->GetRunPublicState();
		if (RunState.Phase.Phase == ECatRunPhase::DayActive)
		{
			FGuid ExpectedRunId;
			int32 ExpectedDayIndex = 0;
			if (SubmitSkipToNextDayQuotaReached(*World, *GameMode, ExpectedRunId, ExpectedDayIndex))
			{
				LogSkipToNextDaySnapshot(*World, *GameMode, TEXT("Command"));
				ScheduleSkipToNextDayReadyContinuation(*World, ExpectedRunId, ExpectedDayIndex, 1);
			}
			return;
		}

		if (RunState.Phase.Phase == ECatRunPhase::NormalNight)
		{
			const FGuid ExpectedRunId = RunState.Phase.RunId;
			const int32 CompletedDayIndex = RunState.Phase.DayIndex;
			const FCatRunAuthorityDebugSnapshot AuthoritySnapshot = GameMode->GetAuthorityDebugSnapshotForDebug();
			if (AuthoritySnapshot.bAllEligibleReadyEventSent)
			{
				UE_LOG(LogCatRun, Display,
					TEXT("Event=run_environment_social_debug_skip_to_next_day_waiting_for_advance Trigger=Command World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s Eligible=%d Ready=%d"),
					*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
					*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
					RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
					AuthoritySnapshot.NightReadyEligibleCount, AuthoritySnapshot.NightReadyCount);
				ScheduleSkipToNextDayAdvanceConfirmation(*World, ExpectedRunId, CompletedDayIndex,
					TEXT("CommandAlreadyReady"), 1);
				return;
			}
			if (SubmitSkipToNextDayReadyForVisiblePlayers(*World, *GameMode, TEXT("Command")))
			{
				LogSkipToNextDaySnapshot(*World, *GameMode, TEXT("Command"));
				ScheduleSkipToNextDayAdvanceConfirmation(*World, ExpectedRunId, CompletedDayIndex,
					TEXT("Command"), 1);
			}
			return;
		}

		UE_LOG(LogCatRun, Warning,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_rejected Reason=InvalidPhase World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s"),
			*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase));
	}

	/** 非 Shipping 构建里的跳到下一天调试指令；它只在 authority World 复用正式 SubmitQuotaContribution/SubmitNextDayReady 写口，不直接改 DayIndex、Phase 或客户端状态。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialSkipToNextDay(
		TEXT("cat.RunEnvironmentSocial.SkipToNextDay"),
		TEXT("服务器调试：通过正式额度和夜晚 ready 命令链跳到下一天；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SkipRunEnvironmentSocialToNextDayForWorld),
		ECVF_Cheat);
}
#endif
