#include "Catfishing.h"

#include "Framework/Game/CatGameplayTypes.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Logging/CatLog.h"

#if !UE_BUILD_SHIPPING
namespace
{
	// NetMode 格式化流程：把引擎网络模式转成稳定日志文本，方便房主端和客户端按同一字段对账。
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
				World ? *FormatSkipToNextDayNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}

		const bool bAccepted = GameMode->ApplyDebugSkipToNextDay();
		const FCatRunPublicState& RunState = GameMode->GetRunPublicState();
		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_skip_to_next_day_command World=%s NetMode=%s Accepted=%s RunId=%s Revision=%lld EnvRevision=%lld Day=%d Phase=%s HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d EndReason=%s"),
			*World->GetName(), *FormatSkipToNextDayNetMode(World->GetNetMode()),
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

	/** 非 Shipping 构建里的跳到下一天调试指令；它只把请求交给服务器 GameMode，加速正式额度与夜晚 ready 流程，不直接改 DayIndex、Phase 或客户端状态。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialSkipToNextDay(
		TEXT("cat.RunEnvironmentSocial.SkipToNextDay"),
		TEXT("服务器调试：请求 GameMode 加速正式额度与夜晚 ready 流程跳到下一天；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SkipRunEnvironmentSocialToNextDayForWorld),
		ECVF_Cheat);
}
#endif
