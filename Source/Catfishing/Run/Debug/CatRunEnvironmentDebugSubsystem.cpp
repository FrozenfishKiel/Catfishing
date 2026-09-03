#include "Run/Debug/CatRunEnvironmentDebugSubsystem.h"

#include "Environment/CatChumFieldReplicationComponent.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "HAL/IConsoleManager.h"
#include "Logging/CatLog.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if !UE_BUILD_SHIPPING
// Run/Environment/Social 的常驻中文调试面板开关；它只控制本地 Slate 面板和日志观察，不写任何 Run、Environment、Social 或表现状态，也不生成第二套玩法时间。
static TAutoConsoleVariable<int32> CVarCatRunEnvironmentSocialDebug(
	TEXT("cat.RunEnvironmentSocial.Debug"), 0,
	TEXT("Run/Environment/Social 中文调试面板：0=关闭（默认）；1=打开左上角界面，显示服务器时间、白天进度、Run、Environment、Social、Chum 和服务器私有底层状态。"),
	ECVF_Default);

namespace
{
	/** 调试输出中的单行文本和颜色；常驻 Slate 面板使用文本，Dump 屏幕短消息继续使用颜色，不参与任何领域状态复制。 */
	struct FCatRunEnvironmentDebugPanelLine
	{
		/** 这一行要显示给人的中文文本；每帧从当前公开快照重新生成。 */
		FString Text;

		/** 这一行在一次性屏幕消息里的颜色；常驻 Slate 面板用文字前缀表达层级，避免多控件保存额外状态。 */
		FLinearColor Color = FLinearColor::White;
	};

	// 中文字体选择流程：开发调试面板优先使用 Windows 常见中文字体；找不到时回退引擎 DroidSansFallback，最后才使用 CoreStyle 默认字体并把字形缺口留给人工截图发现。
	FSlateFontInfo MakeRunEnvironmentDebugPanelFont()
	{
		static constexpr int32 FontSize = 13;
		auto MakeCompositeFontInfo = [](const FString& FontFile)
		{
			const TSharedRef<const FCompositeFont> CompositeFont = MakeShared<FStandaloneCompositeFont>(
				FName(TEXT("Regular")), FontFile, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			return FSlateFontInfo(CompositeFont, FontSize, FName(TEXT("Regular")));
		};
#if PLATFORM_WINDOWS
		// 开发验收目标平台是 Windows，系统字体比 Slate 默认 Roboto 更可靠地覆盖中文；这里只读文件是否存在，不把字体路径写入玩法配置或保存状态。
		const FString WindowsFontCandidates[] =
		{
			TEXT("C:/Windows/Fonts/msyh.ttc"),
			TEXT("C:/Windows/Fonts/simhei.ttf"),
			TEXT("C:/Windows/Fonts/simsun.ttc")
		};
		for (const FString& Candidate : WindowsFontCandidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				return MakeCompositeFontInfo(Candidate);
			}
		}
#endif
		// 非 Windows 或系统字体缺失时再使用引擎随带 fallback 字体；若连它也不存在，最后回到 CoreStyle，只保证面板出现，不承诺中文字形一定完整。
		const FString EngineFallbackFont = FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf");
		if (FPaths::FileExists(EngineFallbackFont))
		{
			return MakeCompositeFontInfo(EngineFallbackFont);
		}
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize);
	}

	// NetMode 格式化流程：把引擎网络模式翻成中文并保留原始名，方便房主端和客户端截图对照。
	FString FormatNetMode(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone:
			return TEXT("单机(Standalone)");
		case NM_DedicatedServer:
			return TEXT("专用服务器(DedicatedServer)");
		case NM_ListenServer:
			return TEXT("监听服务器(ListenServer)");
		case NM_Client:
			return TEXT("客户端(Client)");
		default:
			return TEXT("未知(Unknown)");
		}
	}

	// 日志布尔格式化流程：结构化日志继续使用 true/false，保持现有 grep 和双端对账字段稳定。
	const TCHAR* FormatBoolForLog(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	// 面板布尔格式化流程：人眼阅读用“是/否”，避免运行时界面继续混用英文状态词。
	const TCHAR* FormatBoolForPanel(const bool bValue)
	{
		return bValue ? TEXT("是") : TEXT("否");
	}

	// Run Phase 面板格式化流程：优先写中文语义，再保留代码枚举名，方便截图和源码互相定位。
	FString FormatRunPhaseForPanel(const ECatRunPhase Phase)
	{
		switch (Phase)
		{
		case ECatRunPhase::NotStarted:
			return TEXT("未启动(NotStarted)");
		case ECatRunPhase::DayActive:
			return TEXT("白天开放(DayActive)");
		case ECatRunPhase::NormalNight:
			return TEXT("普通夜晚(NormalNight)");
		case ECatRunPhase::FailureSettlementNight:
			return TEXT("失败结算夜(FailureSettlementNight)");
		case ECatRunPhase::SuccessSettlementNight:
			return TEXT("成功结算夜(SuccessSettlementNight)");
		case ECatRunPhase::Ending:
			return TEXT("收尾中(Ending)");
		case ECatRunPhase::Ended:
			return TEXT("已结束(Ended)");
		default:
			return TEXT("未知阶段");
		}
	}

	// 终局原因面板格式化流程：把公开 EndReason 翻成人话；None 表示本局仍在自然推进，不代表错误。
	FString FormatEndReasonForPanel(const ECatRunEndReason Reason)
	{
		switch (Reason)
		{
		case ECatRunEndReason::None:
			return TEXT("无");
		case ECatRunEndReason::QuotaFailed:
			return TEXT("额度失败");
		case ECatRunEndReason::Success:
			return TEXT("成功");
		case ECatRunEndReason::HostExit:
			return TEXT("房主退出");
		case ECatRunEndReason::StartupFailed:
			return TEXT("启动失败");
		default:
			return TEXT("未知原因");
		}
	}

	// 天气面板格式化流程：天气轴由 Environment 定义；Unknown 要显式显示，防止误以为是晴天。
	FString FormatWeatherForPanel(const ECatEnvironmentWeather Weather)
	{
		switch (Weather)
		{
		case ECatEnvironmentWeather::Unknown:
			return TEXT("未知");
		case ECatEnvironmentWeather::Clear:
			return TEXT("晴天(Clear)");
		case ECatEnvironmentWeather::Rain:
			return TEXT("雨天(Rain)");
		case ECatEnvironmentWeather::Fog:
			return TEXT("雾天(Fog)");
		default:
			return TEXT("未知天气");
		}
	}

	// 白天时段面板格式化流程：夜晚不会进入 TimeOfDay 轴，Unknown 要被看作“无白天时段事实”。
	FString FormatTimeOfDayForPanel(const ECatEnvironmentTimeOfDay TimeOfDay)
	{
		switch (TimeOfDay)
		{
		case ECatEnvironmentTimeOfDay::Unknown:
			return TEXT("未知/非白天");
		case ECatEnvironmentTimeOfDay::Morning:
			return TEXT("清晨(Morning)");
		case ECatEnvironmentTimeOfDay::Day:
			return TEXT("白天(Day)");
		case ECatEnvironmentTimeOfDay::Dusk:
			return TEXT("黄昏(Dusk)");
		default:
			return TEXT("未知时段");
		}
	}

	// 求助信号面板格式化流程：区分手动求助和巨鱼系统提示，Unknown 明确表示当前没有可用信号。
	FString FormatHelpSignalKindForPanel(const ECatHelpSignalKind Kind)
	{
		switch (Kind)
		{
		case ECatHelpSignalKind::Unknown:
			return TEXT("无/未知");
		case ECatHelpSignalKind::ManualFishing:
			return TEXT("手动钓鱼求助");
		case ECatHelpSignalKind::ManualDowned:
			return TEXT("手动倒地求助");
		case ECatHelpSignalKind::GiantFishSystem:
			return TEXT("巨鱼系统提示");
		default:
			return TEXT("未知求助");
		}
	}

	// 转移原因面板格式化流程：解释最近一次 StateTree 相关事件为什么发生，不把原因当成当前阶段。
	FString FormatTransitionReasonForPanel(const ECatRunTransitionReason Reason)
	{
		switch (Reason)
		{
		case ECatRunTransitionReason::None:
			return TEXT("无");
		case ECatRunTransitionReason::QuotaReached:
			return TEXT("额度达成(QuotaReached)");
		case ECatRunTransitionReason::QuotaFailed:
			return TEXT("额度失败(QuotaFailed)");
		case ECatRunTransitionReason::AllEligibleReady:
			return TEXT("全员准备(AllEligibleReady)");
		case ECatRunTransitionReason::SettlementComplete:
			return TEXT("结算完成(SettlementComplete)");
		case ECatRunTransitionReason::HostExit:
			return TEXT("房主退出(HostExit)");
		case ECatRunTransitionReason::NaturalEnd:
			return TEXT("自然结束(NaturalEnd)");
		default:
			return TEXT("未知原因");
		}
	}

	// Run 命令错误面板格式化流程：把服务器拒绝原因翻成中文，帮助人工判断是状态错误、Revision 冲突还是 StateTree 不可用。
	FString FormatRunCommandErrorForPanel(const ECatRunCommandError Error)
	{
		switch (Error)
		{
		case ECatRunCommandError::None:
			return TEXT("无");
		case ECatRunCommandError::PolicyUndecided:
			return TEXT("策略未裁决");
		case ECatRunCommandError::CommandsClosed:
			return TEXT("命令门关闭");
		case ECatRunCommandError::InvalidPhase:
			return TEXT("阶段不接受");
		case ECatRunCommandError::InvalidIdentity:
			return TEXT("身份无效");
		case ECatRunCommandError::InvalidPayload:
			return TEXT("参数无效");
		case ECatRunCommandError::RevisionConflict:
			return TEXT("Revision 冲突");
		case ECatRunCommandError::AlreadyResolved:
			return TEXT("已处理过");
		case ECatRunCommandError::StateTreeUnavailable:
			return TEXT("StateTree 不可用");
		case ECatRunCommandError::NotEligible:
			return TEXT("不在资格集合");
		case ECatRunCommandError::TeardownFailed:
			return TEXT("拆场失败");
		default:
			return TEXT("未知错误");
		}
	}

	// 通用领域命令错误面板格式化流程：用于本机 Social 偷鱼结果，和 Run 命令错误分开避免两个枚举混读。
	FString FormatDomainCommandErrorForPanel(const ECatDomainCommandError Error)
	{
		switch (Error)
		{
		case ECatDomainCommandError::None:
			return TEXT("无");
		case ECatDomainCommandError::InvalidPayload:
			return TEXT("参数无效");
		case ECatDomainCommandError::InvalidIdentity:
			return TEXT("身份无效");
		case ECatDomainCommandError::PolicyUndecided:
			return TEXT("策略未裁决");
		case ECatDomainCommandError::InvalidPhase:
			return TEXT("阶段不接受");
		case ECatDomainCommandError::NotFound:
			return TEXT("目标不存在");
		case ECatDomainCommandError::RevisionConflict:
			return TEXT("Revision 冲突");
		case ECatDomainCommandError::PermissionDenied:
			return TEXT("权限不足");
		case ECatDomainCommandError::CapacityExceeded:
			return TEXT("容量不足");
		case ECatDomainCommandError::AlreadyResolved:
			return TEXT("已处理过");
		case ECatDomainCommandError::Cancelled:
			return TEXT("已取消");
		case ECatDomainCommandError::DependencyUnavailable:
			return TEXT("依赖不可用");
		case ECatDomainCommandError::CommandsClosed:
			return TEXT("命令门关闭");
		default:
			return TEXT("未知错误");
		}
	}

	// 调试控制器选择流程：优先取当前 World 的第一个本地 Controller；没有本地 Controller 时回退第一个 Controller，保证服务器命令行也能输出 GameState 快照。
	APlayerController* FindDebugController(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* Controller = It->Get())
			{
				if (Controller->IsLocalController())
				{
					return Controller;
				}
			}
		}
		return World->GetFirstPlayerController();
	}

	// 局内玩家统计流程：从 GameState 的 PlayerArray 读取已复制 PlayerState，统计当前可见玩家和个人 ready；不读取 GameMode 私有资格集合。
	void CountVisibleReadyPlayers(const ACatfishingGameState& GameState, const APlayerController* Controller,
		int32& OutPlayers, int32& OutReadyPlayers, bool& bOutLocalReady)
	{
		OutPlayers = 0;
		OutReadyPlayers = 0;
		bOutLocalReady = false;
		for (APlayerState* PlayerState : GameState.PlayerArray)
		{
			const ACatfishingPlayerState* CatPlayerState = Cast<ACatfishingPlayerState>(PlayerState);
			if (!CatPlayerState)
			{
				continue;
			}
			++OutPlayers;
			if (CatPlayerState->IsReadyForNextDay())
			{
				++OutReadyPlayers;
			}
			if (Controller && Controller->PlayerState == CatPlayerState)
			{
				bOutLocalReady = CatPlayerState->IsReadyForNextDay();
			}
		}
	}

	// 公开窝点统计流程：只读 GameState 上的复制组件，区分自然事件生成的窝点数量，方便人工核对天气事件有没有重复提交。
	void CountPublicChumFields(const ACatfishingGameState& GameState, int32& OutFields, int32& OutNaturalFields)
	{
		OutFields = 0;
		OutNaturalFields = 0;
		if (const UCatChumFieldReplicationComponent* ChumReplication = GameState.GetChumFieldReplication())
		{
			for (const FCatChumFieldPublicItem& Field : ChumReplication->GetPublicFields())
			{
				++OutFields;
				if (Field.Source == ECatChumFieldSource::NaturalEvent)
				{
					++OutNaturalFields;
				}
			}
		}
	}

	// 健康判断流程：只用公开状态和可选服务器私有快照做诊断文本，不修复、不推进、不隐藏异常；没有服务器快照时只能评价公开复制态。
	FString BuildRunEnvironmentHealthText(const FCatRunPublicState& RunState,
		const FCatRunAuthorityDebugSnapshot* AuthoritySnapshot)
	{
		if (RunState.Environment.SourceRunRevision != RunState.Revision)
		{
			return TEXT("异常：Environment Revision 没有对齐当前 Run Revision，可能正在显示旧环境事实。");
		}
		if (RunState.Phase.Phase == ECatRunPhase::DayActive
			&& (!RunState.Phase.bFishingAllowed || !RunState.Phase.bQuotaOpen))
		{
			return TEXT("异常：公开阶段仍是白天，但钓鱼/额度已经关闭；通常表示 StateTree 没有进入夜晚。");
		}
		if (RunState.Phase.Phase == ECatRunPhase::DayActive
			&& (!RunState.Phase.bHasDeadline
				|| RunState.Phase.DeadlineServerTimeSeconds <= RunState.Phase.ServerTimeAnchorSeconds))
		{
			return TEXT("异常：白天阶段缺少有效截止时间，时间段无法继续计算。");
		}
		if (AuthoritySnapshot)
		{
			if (!AuthoritySnapshot->bRunStateTreeAssigned || !AuthoritySnapshot->bRunStateTreeRunning)
			{
				return TEXT("异常：服务器 StateTree 未配置或未运行，阶段事件无法被消费。");
			}
			if (!AuthoritySnapshot->bRunCommandsOpen
				&& RunState.Phase.Phase != ECatRunPhase::Ending && RunState.Phase.Phase != ECatRunPhase::Ended)
			{
				return TEXT("异常：Run 命令门关闭，但公开阶段还没有进入最终收口。");
			}
			return TEXT("未发现明显异常；继续看服务器时间、白天进度和 Revision 是否变化。");
		}
		return TEXT("公开复制态未发现明显异常；服务器私有 StateTree 和命令门未在本机检查，请同时看房主端面板。");
	}

	// 面板行生成流程：从当前 GameState 复制快照和房主/服务器本机可见的 Debug 快照展开中文字段；辅助命令拆成独立短行只是为了阅读和折行，不代表额外玩法状态。
	TArray<FCatRunEnvironmentDebugPanelLine> BuildRunEnvironmentSocialDebugLines(UWorld* World,
		APlayerController* Controller)
	{
		TArray<FCatRunEnvironmentDebugPanelLine> Lines;
		const FLinearColor TitleColor(1.0f, 0.86f, 0.24f, 1.0f);
		const FLinearColor SectionColor(0.80f, 1.0f, 0.95f, 1.0f);
		const FLinearColor TextColor(0.58f, 0.95f, 1.0f, 1.0f);
		const FLinearColor WarningColor(1.0f, 0.45f, 0.32f, 1.0f);
		const FString WorldName = World ? World->GetName() : FString(TEXT("无 World"));
		const FString NetMode = World ? FormatNetMode(World->GetNetMode()) : FString(TEXT("未知"));
		Lines.Add({ TEXT("【环境系统调试面板】Run / Environment / Social 只读状态"), TitleColor });
		Lines.Add({ FString::Printf(TEXT("世界：%s ｜ 网络模式：%s ｜ 开关：cat.RunEnvironmentSocial.Debug 0/1"),
			*WorldName, *NetMode), TextColor });
		Lines.Add({ TEXT("辅助指令：Dump=cat.RunEnvironmentSocial.Dump ｜ 改白天时长=cat.RunEnvironmentSocial.DayLength <秒>"),
			TextColor });
		Lines.Add({ TEXT("辅助指令：跳到下一天=cat.RunEnvironmentSocial.SkipToNextDay"), TextColor });

		const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
		if (!GameState)
		{
			Lines.Add({ TEXT("GameState：不可用，当前本机还没有收到一局公开状态。"), WarningColor });
			return Lines;
		}

		const FCatRunPublicState& RunState = GameState->GetRunPublicState();
		const FCatHelpSignalSnapshot& HelpSignal = GameState->GetLastHelpSignal();
		const double ServerNow = GameState->GetServerWorldTimeSeconds();
		const bool bHasValidDayClock = RunState.Phase.bHasDeadline
			&& FMath::IsFinite(ServerNow)
			&& FMath::IsFinite(RunState.Phase.ServerTimeAnchorSeconds)
			&& FMath::IsFinite(RunState.Phase.DeadlineServerTimeSeconds)
			&& RunState.Phase.DeadlineServerTimeSeconds > RunState.Phase.ServerTimeAnchorSeconds;
		const double DayLengthSeconds = bHasValidDayClock
			? RunState.Phase.DeadlineServerTimeSeconds - RunState.Phase.ServerTimeAnchorSeconds : 0.0;
		const double DayElapsedSeconds = bHasValidDayClock
			? FMath::Clamp(ServerNow - RunState.Phase.ServerTimeAnchorSeconds, 0.0, DayLengthSeconds) : 0.0;
		const double DeadlineRemaining = bHasValidDayClock
			? FMath::Max(0.0, RunState.Phase.DeadlineServerTimeSeconds - ServerNow) : 0.0;
		const double DayProgressPercent = DayLengthSeconds > 0.0
			? FMath::Clamp(DayElapsedSeconds / DayLengthSeconds * 100.0, 0.0, 100.0) : 0.0;
		const bool bEnvironmentRevisionMatches = RunState.Environment.SourceRunRevision == RunState.Revision;

		double MorningEndServerTimeSeconds = 0.0;
		double DuskStartServerTimeSeconds = 0.0;
		const UCatEnvironmentSettings* EnvironmentSettings = GetDefault<UCatEnvironmentSettings>();
		const bool bHasEnvironmentSettings = EnvironmentSettings != nullptr;
		const bool bEnvironmentRuntimeReady = bHasEnvironmentSettings && EnvironmentSettings->IsRuntimeReady();
		const bool bHasSegmentRefreshes = EnvironmentSettings
			&& EnvironmentSettings->TryResolveTimeOfDayRefreshTimes(
				RunState.Phase, MorningEndServerTimeSeconds, DuskStartServerTimeSeconds);

		int32 PlayerCount = 0;
		int32 ReadyPlayerCount = 0;
		bool bLocalReady = false;
		CountVisibleReadyPlayers(*GameState, Controller, PlayerCount, ReadyPlayerCount, bLocalReady);

		int32 ChumFieldCount = 0;
		int32 NaturalChumFieldCount = 0;
		CountPublicChumFields(*GameState, ChumFieldCount, NaturalChumFieldCount);

		FCatRunAuthorityDebugSnapshot AuthoritySnapshot;
		FCatRunAuthorityDebugSnapshot* AuthoritySnapshotPtr = nullptr;
		if (const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
		{
			AuthoritySnapshot = GameMode->GetAuthorityDebugSnapshotForDebug();
			AuthoritySnapshotPtr = AuthoritySnapshot.bHasAuthorityGameMode ? &AuthoritySnapshot : nullptr;
		}

		const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
		const FCatTheftResult TheftResult = CatController ? CatController->GetLastTheftResult() : FCatTheftResult();

		Lines.Add({ TEXT("—— 时间 / 昼夜 ——"), SectionColor });
		Lines.Add({ FString::Printf(TEXT("服务器时间：当前 %.2f 秒 ｜ 白天锚点 %.2f ｜ 白天截止 %.2f"),
			ServerNow, RunState.Phase.ServerTimeAnchorSeconds, RunState.Phase.DeadlineServerTimeSeconds), TextColor });
		Lines.Add({ bHasValidDayClock
			? FString::Printf(TEXT("白天进度：已过 %.2f / %.2f 秒 ｜ 剩余 %.2f 秒 ｜ 进度 %.1f%%"),
				DayElapsedSeconds, DayLengthSeconds, DeadlineRemaining, DayProgressPercent)
			: TEXT("白天进度：当前没有有效白天时钟；夜晚、未启动或异常白天都会显示这一行。"),
			bHasValidDayClock ? TextColor : WarningColor });
		Lines.Add({ bHasSegmentRefreshes
			? FString::Printf(TEXT("时段刷新点：清晨结束 %.2f 秒 ｜ 黄昏开始 %.2f 秒 ｜ 当前时段：%s"),
				MorningEndServerTimeSeconds, DuskStartServerTimeSeconds,
				*FormatTimeOfDayForPanel(RunState.Environment.TimeOfDay))
			: FString::Printf(TEXT("时段刷新点：不可用 ｜ 当前时段：%s"),
				*FormatTimeOfDayForPanel(RunState.Environment.TimeOfDay)),
			bHasSegmentRefreshes ? TextColor : WarningColor });

		Lines.Add({ TEXT("—— Run 状态 ——"), SectionColor });
		Lines.Add({ FString::Printf(TEXT("RunId：%s ｜ 第 %d 天 ｜ 阶段：%s ｜ Revision：%lld"),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Phase.DayIndex,
			*FormatRunPhaseForPanel(RunState.Phase.Phase), RunState.Revision), TextColor });
		Lines.Add({ FString::Printf(TEXT("Run 门禁：有截止 %s ｜ 可钓鱼 %s ｜ 额度开放 %s ｜ 额度 %d / %d ｜ 终局原因 %s"),
			FormatBoolForPanel(RunState.Phase.bHasDeadline), FormatBoolForPanel(RunState.Phase.bFishingAllowed),
			FormatBoolForPanel(RunState.Phase.bQuotaOpen), RunState.QuotaProgress, RunState.QuotaTarget,
			*FormatEndReasonForPanel(RunState.EndReason)), TextColor });

		Lines.Add({ TEXT("—— Environment 状态 ——"), SectionColor });
		Lines.Add({ bHasEnvironmentSettings
			? FString::Printf(TEXT("环境配置：Runtime 开启 %s ｜ 运行就绪 %s ｜ 配置天气 %s ｜ 清晨 %.2f ｜ 黄昏 %.2f"),
				FormatBoolForPanel(EnvironmentSettings->bEnableEnvironmentRuntime),
				FormatBoolForPanel(bEnvironmentRuntimeReady),
				*FormatWeatherForPanel(EnvironmentSettings->ConfiguredWeather),
				EnvironmentSettings->MorningEndFraction, EnvironmentSettings->DuskStartFraction)
			: TEXT("环境配置：不可用，无法判断天气和晨昏配置。"),
			bEnvironmentRuntimeReady ? TextColor : WarningColor });
		Lines.Add({ FString::Printf(TEXT("天气：%s ｜ 白天时段：%s ｜ 公共事件：%s ｜ 有事件 %s"),
			*FormatWeatherForPanel(RunState.Environment.Weather),
			*FormatTimeOfDayForPanel(RunState.Environment.TimeOfDay),
			RunState.Environment.ActiveEventId.IsNone() ? TEXT("无") : *RunState.Environment.ActiveEventId.ToString(),
			FormatBoolForPanel(RunState.Environment.bHasActiveEvent)), TextColor });
		Lines.Add({ FString::Printf(TEXT("环境 Revision：EnvRevision %lld ｜ RunRevision %lld ｜ 是否对齐 %s"),
			RunState.Environment.SourceRunRevision, RunState.Revision,
			FormatBoolForPanel(bEnvironmentRevisionMatches)),
			bEnvironmentRevisionMatches ? TextColor : WarningColor });

		Lines.Add({ TEXT("—— Social / Chum 状态 ——"), SectionColor });
		Lines.Add({ FString::Printf(TEXT("玩家：可见 %d ｜ 夜晚已准备 %d ｜ 本机准备 %s ｜ teardown 完成 %s"),
			PlayerCount, ReadyPlayerCount, FormatBoolForPanel(bLocalReady),
			FormatBoolForPanel(RunState.bTeardownComplete)), TextColor });
		Lines.Add({ FString::Printf(TEXT("求助：类型 %s ｜ Revision %lld ｜ 全局 %s ｜ 半径 %.1f cm ｜ SignalId %s"),
			*FormatHelpSignalKindForPanel(HelpSignal.Kind), HelpSignal.Revision,
			FormatBoolForPanel(HelpSignal.bGlobal), HelpSignal.RadiusCentimeters,
			*HelpSignal.SignalId.ToString(EGuidFormats::DigitsWithHyphens)), TextColor });
		Lines.Add({ FString::Printf(TEXT("求助位置：X %.1f ｜ Y %.1f ｜ Z %.1f"),
			HelpSignal.SourceLocation.X, HelpSignal.SourceLocation.Y, HelpSignal.SourceLocation.Z), TextColor });
		Lines.Add({ FString::Printf(TEXT("偷鱼结果：协议 %s ｜ 鱼 %s"),
			*TheftResult.TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
			*TheftResult.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens)), TextColor });
		Lines.Add({ FString::Printf(TEXT("偷鱼状态：错误 %s ｜ 窗口 %s ｜ 已追回 %s ｜ 已吃掉 %s"),
			*FormatDomainCommandErrorForPanel(TheftResult.Command.Error),
			FormatBoolForPanel(TheftResult.bRecoveryWindowOpen),
			FormatBoolForPanel(TheftResult.bReturned),
			FormatBoolForPanel(TheftResult.bConsumed)), TextColor });
		Lines.Add({ FString::Printf(TEXT("窝点：公开窝点 %d ｜ 自然事件窝点 %d"),
			ChumFieldCount, NaturalChumFieldCount), TextColor });

		Lines.Add({ TEXT("—— 服务器私有底层 ——"), SectionColor });
		if (AuthoritySnapshotPtr)
		{
			Lines.Add({ FString::Printf(TEXT("服务器私有：Run 命令门 %s ｜ StateTree 已配置 %s ｜ StateTree 运行中 %s ｜ 启动中 %s"),
				FormatBoolForPanel(AuthoritySnapshotPtr->bRunCommandsOpen),
				FormatBoolForPanel(AuthoritySnapshotPtr->bRunStateTreeAssigned),
				FormatBoolForPanel(AuthoritySnapshotPtr->bRunStateTreeRunning),
				FormatBoolForPanel(AuthoritySnapshotPtr->bRunStartupInProgress)), TextColor });
			Lines.Add({ FString::Printf(TEXT("服务器私有：夜晚资格 %d ｜ 夜晚 ready %d ｜ 已发全员 ready 事件 %s"),
				AuthoritySnapshotPtr->NightReadyEligibleCount, AuthoritySnapshotPtr->NightReadyCount,
				FormatBoolForPanel(AuthoritySnapshotPtr->bAllEligibleReadyEventSent)), TextColor });
			Lines.Add({ FString::Printf(TEXT("最近 StateTree 结果：应用 %s ｜ 原因 %s ｜ 错误 %s ｜ Revision %lld"),
				FormatBoolForPanel(AuthoritySnapshotPtr->LastRunFlowResult.bApplied),
				*FormatTransitionReasonForPanel(AuthoritySnapshotPtr->LastRunFlowResult.Reason),
				*FormatRunCommandErrorForPanel(AuthoritySnapshotPtr->LastRunFlowResult.Error),
				AuthoritySnapshotPtr->LastRunFlowResult.Revision), TextColor });
			Lines.Add({ FString::Printf(TEXT("最近 StateTree 阶段：%s -> %s"),
				*FormatRunPhaseForPanel(AuthoritySnapshotPtr->LastRunFlowResult.PreviousPhase),
				*FormatRunPhaseForPanel(AuthoritySnapshotPtr->LastRunFlowResult.CurrentPhase)), TextColor });
		}
		else
		{
			Lines.Add({ TEXT("服务器私有：本机不是服务器，只显示 GameState 复制结果；房主端打开面板可看 StateTree 和命令门。"), WarningColor });
		}

		const FString HealthText = BuildRunEnvironmentHealthText(RunState, AuthoritySnapshotPtr);
		Lines.Add({ FString::Printf(TEXT("健康判断：%s"), *HealthText),
			HealthText.StartsWith(TEXT("异常")) ? WarningColor : FLinearColor(0.52f, 1.0f, 0.52f, 1.0f) });
		return Lines;
	}

	// Slate 面板文本生成流程：复用 Dump 的行生成函数，再用换行拼成一个 TextBlock 可自动换行的字符串；手工短行和 Slate 自动折行只影响可读性，不缓存玩法快照。
	FString BuildRunEnvironmentSocialDebugText(UWorld* World, APlayerController* Controller)
	{
		const TArray<FCatRunEnvironmentDebugPanelLine> Lines = BuildRunEnvironmentSocialDebugLines(World, Controller);
		FString Text;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Index > 0)
			{
				Text += LINE_TERMINATOR;
			}
			Text += Lines[Index].Text;
		}
		return Text;
	}

	// 结构化日志流程：每次 Dump 或 Revision 变化只写一条可 grep 的长日志，方便房主端和客户端用同一事件名对账。
	void LogRunEnvironmentSocialSnapshot(UWorld* World, APlayerController* Controller, const TCHAR* Trigger)
	{
		const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
		if (!World || !GameState)
		{
			UE_LOG(LogCatRun, Warning, TEXT("Event=run_environment_social_debug_unavailable Trigger=%s World=%s"),
				Trigger, World ? *World->GetName() : TEXT("None"));
			return;
		}

		const FCatRunPublicState& RunState = GameState->GetRunPublicState();
		const FCatHelpSignalSnapshot& HelpSignal = GameState->GetLastHelpSignal();
		const double ServerNow = GameState->GetServerWorldTimeSeconds();
		const bool bHasValidDayClock = RunState.Phase.bHasDeadline
			&& RunState.Phase.DeadlineServerTimeSeconds > RunState.Phase.ServerTimeAnchorSeconds;
		const double DayLengthSeconds = bHasValidDayClock
			? RunState.Phase.DeadlineServerTimeSeconds - RunState.Phase.ServerTimeAnchorSeconds : 0.0;
		const double DayElapsedSeconds = bHasValidDayClock
			? FMath::Clamp(ServerNow - RunState.Phase.ServerTimeAnchorSeconds, 0.0, DayLengthSeconds) : 0.0;
		const double DeadlineRemaining = bHasValidDayClock
			? FMath::Max(0.0, RunState.Phase.DeadlineServerTimeSeconds - ServerNow) : 0.0;
		const double DayProgressPercent = DayLengthSeconds > 0.0
			? FMath::Clamp(DayElapsedSeconds / DayLengthSeconds * 100.0, 0.0, 100.0) : 0.0;
		const bool bEnvironmentRevisionMatches = RunState.Environment.SourceRunRevision == RunState.Revision;

		int32 PlayerCount = 0;
		int32 ReadyPlayerCount = 0;
		bool bLocalReady = false;
		CountVisibleReadyPlayers(*GameState, Controller, PlayerCount, ReadyPlayerCount, bLocalReady);

		int32 ChumFieldCount = 0;
		int32 NaturalChumFieldCount = 0;
		CountPublicChumFields(*GameState, ChumFieldCount, NaturalChumFieldCount);

		const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
		const FCatTheftResult TheftResult = CatController ? CatController->GetLastTheftResult() : FCatTheftResult();

		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_snapshot Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s End=%s ServerNow=%.3f Anchor=%.3f Deadline=%.3f DayElapsed=%.3f DayLength=%.3f DeadlineRemaining=%.3f DayProgress=%.3f HasDeadline=%s FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d Weather=%s TimeOfDay=%s HasEvent=%s ActiveEvent=%s EnvRevision=%lld EnvRevisionMatch=%s ReadyPlayers=%d PlayerCount=%d LocalReady=%s HelpKind=%s HelpRevision=%lld HelpGlobal=%s HelpRadius=%.3f HelpSignalId=%s HelpX=%.3f HelpY=%.3f HelpZ=%.3f TheftProtocolId=%s TheftFishId=%s TheftError=%s TheftWindow=%s TheftReturned=%s TheftConsumed=%s ChumFields=%d NaturalChumFields=%d TeardownComplete=%s"),
			Trigger, *World->GetName(), *FormatNetMode(World->GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
			*UEnum::GetValueAsString(RunState.EndReason), ServerNow, RunState.Phase.ServerTimeAnchorSeconds,
			RunState.Phase.DeadlineServerTimeSeconds, DayElapsedSeconds, DayLengthSeconds, DeadlineRemaining,
			DayProgressPercent, FormatBoolForLog(RunState.Phase.bHasDeadline),
			FormatBoolForLog(RunState.Phase.bFishingAllowed), FormatBoolForLog(RunState.Phase.bQuotaOpen),
			RunState.QuotaProgress, RunState.QuotaTarget, *UEnum::GetValueAsString(RunState.Environment.Weather),
			*UEnum::GetValueAsString(RunState.Environment.TimeOfDay),
			FormatBoolForLog(RunState.Environment.bHasActiveEvent),
			RunState.Environment.ActiveEventId.IsNone() ? TEXT("--") : *RunState.Environment.ActiveEventId.ToString(),
			RunState.Environment.SourceRunRevision, FormatBoolForLog(bEnvironmentRevisionMatches),
			ReadyPlayerCount, PlayerCount, FormatBoolForLog(bLocalReady),
			*UEnum::GetValueAsString(HelpSignal.Kind), HelpSignal.Revision,
			FormatBoolForLog(HelpSignal.bGlobal), HelpSignal.RadiusCentimeters,
			*HelpSignal.SignalId.ToString(EGuidFormats::DigitsWithHyphens),
			HelpSignal.SourceLocation.X, HelpSignal.SourceLocation.Y, HelpSignal.SourceLocation.Z,
			*TheftResult.TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
			*TheftResult.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(TheftResult.Command.Error),
			FormatBoolForLog(TheftResult.bRecoveryWindowOpen),
			FormatBoolForLog(TheftResult.bReturned),
			FormatBoolForLog(TheftResult.bConsumed),
			ChumFieldCount, NaturalChumFieldCount, FormatBoolForLog(RunState.bTeardownComplete));
	}

	// 一次性屏幕输出流程：Dump 沿用短暂屏幕消息，但文字与常驻面板共用同一份中文行；消息过期后不留下 UI 或玩法状态。
	void PushRunEnvironmentSocialScreenLines(const UObject& Owner,
		const TArray<FCatRunEnvironmentDebugPanelLine>& Lines, const float TimeToDisplay)
	{
		if (!GEngine)
		{
			return;
		}
		const uint64 BaseKey = static_cast<uint64>(Owner.GetUniqueID()) * 64ULL;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			GEngine->AddOnScreenDebugMessage(BaseKey + static_cast<uint64>(Index), TimeToDisplay,
				Lines[Index].Color.ToFColor(true), Lines[Index].Text);
		}
	}

	// 手动 Dump 指令流程：读取当前 World 的同一份公开状态，同时输出到日志和屏幕；它不打开常驻面板，也不改任何领域状态。
	void DumpRunEnvironmentSocialForWorld(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		APlayerController* Controller = FindDebugController(World);
		LogRunEnvironmentSocialSnapshot(World, Controller, TEXT("DumpCommand"));
		if (World)
		{
			UObject* Owner = Controller ? static_cast<UObject*>(Controller) : static_cast<UObject*>(World);
			PushRunEnvironmentSocialScreenLines(*Owner,
				BuildRunEnvironmentSocialDebugLines(World, Controller), 6.0f);
		}
	}

	// 白天长度调试流程：先解析唯一秒数参数，再要求当前 World 拥有 authority GameMode；客户端或非法参数只写拒绝日志。成功后只走 GameMode 的调试时钟写口，重设当前白天窗口、重排 timer、发布同一份 RunPublicState，并立刻 dump 新快照。
	void SetRunEnvironmentSocialDayLengthForWorld(const TArray<FString>& Args, UWorld* World)
	{
		double NewDayLengthSeconds = 0.0;
		if (Args.Num() != 1 || !FDefaultValueHelper::ParseDouble(Args[0], NewDayLengthSeconds))
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_day_length_rejected Reason=InvalidArguments Usage=\"cat.RunEnvironmentSocial.DayLength <Seconds>\""));
			return;
		}

		ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
		if (!GameMode)
		{
			UE_LOG(LogCatRun, Warning,
				TEXT("Event=run_environment_social_debug_day_length_rejected Reason=AuthorityGameModeUnavailable Seconds=%.3f World=%s NetMode=%s"),
				NewDayLengthSeconds, World ? *World->GetName() : TEXT("None"),
				World ? *FormatNetMode(World->GetNetMode()) : TEXT("Unknown"));
			return;
		}
		if (!GameMode->ApplyDebugDayLengthSeconds(NewDayLengthSeconds))
		{
			return;
		}

		APlayerController* Controller = FindDebugController(World);
		LogRunEnvironmentSocialSnapshot(World, Controller, TEXT("DayLengthCommand"));
		UObject* Owner = Controller ? static_cast<UObject*>(Controller) : static_cast<UObject*>(World);
		if (Owner)
		{
			PushRunEnvironmentSocialScreenLines(*Owner,
				BuildRunEnvironmentSocialDebugLines(World, Controller), 6.0f);
		}
	}

	/** 非 Shipping 构建里的 RunEnvironmentSocial 一次性快照指令；只读输出，不触发 StateTree、天气或社交流程。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialDump(
		TEXT("cat.RunEnvironmentSocial.Dump"),
		TEXT("输出一次 Run/Environment/Social 中文调试快照到日志，并在本机屏幕显示 6 秒。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpRunEnvironmentSocialForWorld),
		ECVF_Cheat);

	/** 非 Shipping 构建里的当前白天长度调试指令；它只请求服务器正式 Run 状态重排当前白天窗口和 timer，再通过同一份 GameState 复制给所有客户端。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialDayLength(
		TEXT("cat.RunEnvironmentSocial.DayLength"),
		TEXT("重设当前 DayActive 从现在开始持续的秒数，例如 cat.RunEnvironmentSocial.DayLength 60；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetRunEnvironmentSocialDayLengthForWorld),
		ECVF_Cheat);

}
#endif

// 初始化流程：只交还父类建立 WorldSubsystem 生命周期；Slate 面板等 Debug 开关打开后再创建，避免常态持有视口对象。
void UCatRunEnvironmentDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

// 反初始化流程：先从 GameViewport 移除本地调试面板，再释放父类资源；正式领域对象、复制状态和 timer 仍由各自系统收口。
void UCatRunEnvironmentDebugSubsystem::Deinitialize()
{
	DestroyDebugPanelWidget();
	Super::Deinitialize();
}

// 创建条件流程：非 Shipping 的游戏 World 才创建调试观察者；Shipping 和非游戏 World 返回 false，避免把诊断入口带进正式构建。
bool UCatRunEnvironmentDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
#else
	return false;
#endif
}

// 统计 ID 流程：只把本调试 Tick 归入普通 Tickable 统计组，不声明新的 Gameplay 计时域。
TStatId UCatRunEnvironmentDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCatRunEnvironmentDebugSubsystem, STATGROUP_Tickables);
}

// 面板生命周期刷新流程：每帧先看 Debug 开关；关闭时移除已有面板，打开时懒创建，保证 Console 开关和视口显示保持一致。
void UCatRunEnvironmentDebugSubsystem::RefreshDebugPanelLifecycle()
{
#if !UE_BUILD_SHIPPING
	// 面板是否存在完全由 CVar 驱动；关闭时销毁 Slate 引用，避免 World 切换后旧面板继续显示上一局状态。
	if (CVarCatRunEnvironmentSocialDebug.GetValueOnGameThread() == 0)
	{
		DestroyDebugPanelWidget();
		return;
	}

	if (!DebugPanelWidget.IsValid())
	{
		CreateDebugPanelWidget();
	}
#endif
}

// 面板创建流程：
// 1. 确认当前 World 有可挂接的 GameViewport，专用服务器或视口未就绪时保持空操作，下帧继续尝试。
// 2. 创建左上角半透明 Slate 面板，并使用中文字体候选，避免 Slate 默认字体在中文环境下变成方块。
// 3. 外层槽位横向填满并保留右侧边距，只是给 TextBlock 正确测宽和自动折行，不保存任何面板状态。
// 4. 面板设置为 HitTestInvisible，只显示调试文字、不抢玩家输入，并记录实际挂接视口供销毁时配对解绑。
void UCatRunEnvironmentDebugSubsystem::CreateDebugPanelWidget()
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
	if (DebugPanelWidget.IsValid() || !GameViewport)
	{
		return;
	}

	DebugPanelWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))
		[
			SNew(SBox)
			.MaxDesiredWidth(1180.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(10.0f))
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f))
				[
					SAssignNew(DebugPanelTextBlock, STextBlock)
					.Font(MakeRunEnvironmentDebugPanelFont())
					.ColorAndOpacity(FLinearColor(0.78f, 0.96f, 1.0f, 1.0f))
					.AutoWrapText(true)
					.LineHeightPercentage(1.05f)
					.Text(FText::FromString(TEXT("环境系统调试面板：等待 GameState...")))
				]
			]
		];

	GameViewport->AddViewportWidgetContent(DebugPanelWidget.ToSharedRef(), 1000);
	DebugPanelViewport = GameViewport;
#endif
}

// 面板销毁流程：如果原挂接视口仍存在就从同一个 GameViewport 移除 Slate 实例；视口已销毁时直接清引用，避免多客户端 PIE 或 World 切换后悬挂旧文本。
void UCatRunEnvironmentDebugSubsystem::DestroyDebugPanelWidget()
{
#if !UE_BUILD_SHIPPING
	if (DebugPanelWidget.IsValid())
	{
		if (UGameViewportClient* GameViewport = DebugPanelViewport.Get())
		{
			GameViewport->RemoveViewportWidgetContent(DebugPanelWidget.ToSharedRef());
		}
	}
	DebugPanelViewport.Reset();
	DebugPanelTextBlock.Reset();
	DebugPanelWidget.Reset();
#endif
}

// 面板文本刷新流程：每帧重新读取当前公开快照；只有服务器本机能补到私有快照，客户端面板不会伪造服务器底层状态。
void UCatRunEnvironmentDebugSubsystem::RefreshDebugPanelText(UWorld* World, APlayerController* Controller)
{
#if !UE_BUILD_SHIPPING
	if (DebugPanelTextBlock.IsValid())
	{
		DebugPanelTextBlock->SetText(FText::FromString(BuildRunEnvironmentSocialDebugText(World, Controller)));
	}
#endif
}

// Tick 调试流程：开关关闭时移除面板并清空上次日志 Revision；打开时刷新中文面板，并只在 Run Revision 变化时写结构化日志。
void UCatRunEnvironmentDebugSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
#if !UE_BUILD_SHIPPING
	RefreshDebugPanelLifecycle();
	// CVar 在游戏线程读取，目的只是决定本地观察是否启用；关闭时重置日志游标，下一次重新打开会重新输出当前 Revision 的基线快照。
	if (CVarCatRunEnvironmentSocialDebug.GetValueOnGameThread() == 0)
	{
		LastLoggedRevision = INDEX_NONE;
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = FindDebugController(World);
	RefreshDebugPanelText(World, Controller);

	const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	if (GameState)
	{
		const int64 CurrentRevision = GameState->GetRunPublicState().Revision;
		if (CurrentRevision != LastLoggedRevision)
		{
			LastLoggedRevision = CurrentRevision;
			LogRunEnvironmentSocialSnapshot(World, Controller, TEXT("RevisionChanged"));
		}
	}
#endif
}
