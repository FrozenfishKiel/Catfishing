#include "Catfishing.h"

#include "Framework/Game/CatGameplayTypes.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Logging/CatLog.h"

#if !UE_BUILD_SHIPPING
namespace
{
	// NetMode 格式化流程：把引擎网络模式转成稳定日志文本，方便房主端和客户端按同一字段对账。
	FString FormatRunEnvironmentSocialDebugNetMode(const ENetMode NetMode)
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

	// 调试指令结果日志流程：所有昼夜跳转类指令统一输出同一组公开 Run 字段，方便房主端和客户端对比命令前后 Revision、天数和阶段。
	void LogRunEnvironmentSocialDebugCommandResult(UWorld* World, const TCHAR* EventName, const TCHAR* CommandName,
		const bool bAccepted, const FCatRunPublicState& RunState)
	{
		UE_LOG(LogCatRun, Display,
			TEXT("Event=%s Command=%s World=%s NetMode=%s Accepted=%s RunId=%s Revision=%lld EnvRevision=%lld Day=%d Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d EndReason=%s"),
			EventName, CommandName,
			World ? *World->GetName() : TEXT("None"),
			World ? *FormatRunEnvironmentSocialDebugNetMode(World->GetNetMode()) : TEXT("Unknown"),
			bAccepted ? TEXT("true") : TEXT("false"),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Environment.SourceRunRevision, RunState.Phase.DayIndex,
			*UEnum::GetValueAsString(RunState.Phase.Phase),
			RunState.Phase.bHasDeadline ? TEXT("true") : TEXT("false"),
			RunState.Phase.bFishingAllowed ? TEXT("true") : TEXT("false"),
			RunState.Phase.bQuotaOpen ? TEXT("true") : TEXT("false"),
			RunState.QuotaProgress, RunState.QuotaTarget,
			*UEnum::GetValueAsString(RunState.EndReason));
	}

	// 跳夜晚指令入口流程：只拒绝多余参数和非 authority World，然后请求 GameMode 用正式额度事件进入普通夜晚；它不会提交夜晚 ready 或推进下一天。
	void SkipRunEnvironmentSocialToNightForWorld(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty())
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_night_rejected Reason=InvalidArguments Usage=\"cat.RunEnvironmentSocial.SkipToNight\""));
			return;
		}

		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!World || !GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_skip_to_night_rejected Reason=AuthorityGameModeUnavailable World=%s NetMode=%s"),
				World ? *World->GetName() : TEXT("None"),
				World ? *FormatRunEnvironmentSocialDebugNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}

		const bool bAccepted = GameMode->ApplyDebugSkipToNight();
		LogRunEnvironmentSocialDebugCommandResult(World,
			TEXT("run_environment_social_debug_skip_to_night_command"),
			TEXT("cat.RunEnvironmentSocial.SkipToNight"), bAccepted, GameMode->GetRunPublicState());
	}

	// 跳天指令入口流程：只拒绝多余参数和非 authority World，然后把请求交给 GameMode 的正式流程加速入口；本文件不提交额度、不提交 ready、不安排阶段轮询。
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
				World ? *FormatRunEnvironmentSocialDebugNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}

		const bool bAccepted = GameMode->ApplyDebugSkipToNextDay();
		LogRunEnvironmentSocialDebugCommandResult(World,
			TEXT("run_environment_social_debug_skip_to_next_day_command"),
			TEXT("cat.RunEnvironmentSocial.SkipToNextDay"), bAccepted, GameMode->GetRunPublicState());
	}

	// 强制下一天指令入口流程：只在非 Shipping 的服务器 authority 上调用显式作弊救援；它只处理失败夜继续测试或普通夜全员 ready 卡住，不让客户端或 UI 本地改天数。
	void ForceRunEnvironmentSocialNextDayForWorld(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty())
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=InvalidArguments Usage=\"cat.RunEnvironmentSocial.ForceNextDay\""));
			return;
		}

		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!World || !GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_force_next_day_rejected Reason=AuthorityGameModeUnavailable World=%s NetMode=%s"),
				World ? *World->GetName() : TEXT("None"),
				World ? *FormatRunEnvironmentSocialDebugNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}

		const bool bAccepted = GameMode->ApplyDebugForceNextDay();
		LogRunEnvironmentSocialDebugCommandResult(World,
			TEXT("run_environment_social_debug_force_next_day_command"),
			TEXT("cat.RunEnvironmentSocial.ForceNextDay"), bAccepted, GameMode->GetRunPublicState());
	}

	/** 非 Shipping 构建里的跳到普通夜晚调试指令；它只补足当前白天额度并发送正式 QuotaReached，不提交 ready、不递增天数。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialSkipToNight(
		TEXT("cat.RunEnvironmentSocial.SkipToNight"),
		TEXT("服务器调试：用正式额度贡献把当前 DayActive 推进到普通夜晚；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SkipRunEnvironmentSocialToNightForWorld),
		ECVF_Cheat);

	/** 非 Shipping 构建里的跳到下一天调试指令；它只把请求交给服务器 GameMode，加速正式额度与夜晚 ready 流程，不直接改 DayIndex、Phase 或客户端状态。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialSkipToNextDay(
		TEXT("cat.RunEnvironmentSocial.SkipToNextDay"),
		TEXT("服务器调试：请求 GameMode 加速正式额度与夜晚 ready 流程跳到下一天；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SkipRunEnvironmentSocialToNextDayForWorld),
		ECVF_Cheat);

	/** 非 Shipping 构建里的强制下一天作弊指令；它保留正式成功/失败/拆局规则，只在失败夜继续测试或普通夜 ready 卡住时把服务器 StateTree 重启到下一次 DayActive。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialForceNextDay(
		TEXT("cat.RunEnvironmentSocial.ForceNextDay"),
		TEXT("服务器调试作弊：从失败夜继续测试，或在普通夜全员 ready 卡住时强制重启 ST_RunFlow 到下一次 DayActive；Shipping 不存在。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ForceRunEnvironmentSocialNextDayForWorld),
		ECVF_Cheat);
}
#endif
