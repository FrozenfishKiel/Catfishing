#include "Run/Debug/CatRunEnvironmentDebugSubsystem.h"

#include "Environment/CatChumFieldReplicationComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DefaultValueHelper.h"

#if !UE_BUILD_SHIPPING
// Run/Environment/Social 的常驻屏幕调试开关；它只决定本地 Tick 是否读取公开快照并输出文本，不写任何 Run、Environment、Social、Wet 或表现状态。
static TAutoConsoleVariable<int32> CVarCatRunEnvironmentSocialDebug(
	TEXT("cat.RunEnvironmentSocial.Debug"), 0,
	TEXT("Run/Environment/Social 只读调试面板：0=关闭（默认）；1=屏幕显示当前 Day/Phase/Revision/Weather/Ready/Chum 信息，并在 Revision 变化时写日志。"),
	ECVF_Default);

namespace
{
	// NetMode 格式化流程：把引擎枚举转成日志和屏幕都容易对照的短文本，便于房主端和客户端人工比对。
	FString FormatNetMode(const ENetMode NetMode)
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

	// 布尔值格式化流程：统一使用 true/false，避免日志里同时出现 Yes/No、1/0 等多种口径。
	const TCHAR* FormatBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
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

	// 屏幕行生成流程：从当前 GameState 复制快照一次性展开关键字段；缺 GameState 时显式输出 unavailable，避免人工验收误以为空白就是通过。
	TArray<FString> BuildRunEnvironmentSocialDebugLines(UWorld* World, APlayerController* Controller)
	{
		TArray<FString> Lines;
		const FString WorldName = World ? World->GetName() : FString(TEXT("None"));
		const FString NetMode = World ? FormatNetMode(World->GetNetMode()) : FString(TEXT("Unknown"));
		Lines.Add(FString::Printf(TEXT("RunEnvSocial Debug  World=%s NetMode=%s"), *WorldName, *NetMode));

		const ACatfishingGameState* GameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
		if (!GameState)
		{
			Lines.Add(TEXT("GameState unavailable"));
			return Lines;
		}

		const FCatRunPublicState& RunState = GameState->GetRunPublicState();
		const double ServerNow = GameState->GetServerWorldTimeSeconds();
		const double DeadlineRemaining = RunState.Phase.bHasDeadline
			? FMath::Max(0.0, RunState.Phase.DeadlineServerTimeSeconds - ServerNow) : 0.0;
		const bool bEnvironmentRevisionMatches = RunState.Environment.SourceRunRevision == RunState.Revision;

		int32 PlayerCount = 0;
		int32 ReadyPlayerCount = 0;
		bool bLocalReady = false;
		CountVisibleReadyPlayers(*GameState, Controller, PlayerCount, ReadyPlayerCount, bLocalReady);

		int32 ChumFieldCount = 0;
		int32 NaturalChumFieldCount = 0;
		CountPublicChumFields(*GameState, ChumFieldCount, NaturalChumFieldCount);

		Lines.Add(FString::Printf(TEXT("RunId=%s  Rev=%lld  EnvRev=%lld  EnvMatch=%s"),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Environment.SourceRunRevision, FormatBool(bEnvironmentRevisionMatches)));
		Lines.Add(FString::Printf(TEXT("Day=%d  Phase=%s  End=%s  Teardown=%s"),
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
			*UEnum::GetValueAsString(RunState.EndReason), FormatBool(RunState.bTeardownComplete)));
		Lines.Add(FString::Printf(TEXT("Deadline=%s %.1fs  Fishing=%s  QuotaOpen=%s  Quota=%d/%d"),
			FormatBool(RunState.Phase.bHasDeadline), DeadlineRemaining,
			FormatBool(RunState.Phase.bFishingAllowed), FormatBool(RunState.Phase.bQuotaOpen),
			RunState.QuotaProgress, RunState.QuotaTarget));
		Lines.Add(FString::Printf(TEXT("Weather=%s  TimeOfDay=%s  Event=%s  HasEvent=%s"),
			*UEnum::GetValueAsString(RunState.Environment.Weather),
			*UEnum::GetValueAsString(RunState.Environment.TimeOfDay),
			RunState.Environment.ActiveEventId.IsNone() ? TEXT("--") : *RunState.Environment.ActiveEventId.ToString(),
			FormatBool(RunState.Environment.bHasActiveEvent)));
		Lines.Add(FString::Printf(TEXT("Ready=%d/%d  LocalReady=%s  ChumFields=%d  NaturalChum=%d"),
			ReadyPlayerCount, PlayerCount, FormatBool(bLocalReady), ChumFieldCount, NaturalChumFieldCount));
		return Lines;
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
		const double ServerNow = GameState->GetServerWorldTimeSeconds();
		const double DeadlineRemaining = RunState.Phase.bHasDeadline
			? FMath::Max(0.0, RunState.Phase.DeadlineServerTimeSeconds - ServerNow) : 0.0;
		const bool bEnvironmentRevisionMatches = RunState.Environment.SourceRunRevision == RunState.Revision;

		int32 PlayerCount = 0;
		int32 ReadyPlayerCount = 0;
		bool bLocalReady = false;
		CountVisibleReadyPlayers(*GameState, Controller, PlayerCount, ReadyPlayerCount, bLocalReady);

		int32 ChumFieldCount = 0;
		int32 NaturalChumFieldCount = 0;
		CountPublicChumFields(*GameState, ChumFieldCount, NaturalChumFieldCount);

		UE_LOG(LogCatRun, Display,
			TEXT("Event=run_environment_social_debug_snapshot Trigger=%s World=%s NetMode=%s RunId=%s Revision=%lld Day=%d Phase=%s End=%s HasDeadline=%s DeadlineRemaining=%.3f FishingAllowed=%s QuotaOpen=%s QuotaProgress=%d QuotaTarget=%d Weather=%s TimeOfDay=%s HasEvent=%s ActiveEvent=%s EnvRevision=%lld EnvRevisionMatch=%s ReadyPlayers=%d PlayerCount=%d LocalReady=%s ChumFields=%d NaturalChumFields=%d TeardownComplete=%s"),
			Trigger, *World->GetName(), *FormatNetMode(World->GetNetMode()),
			*RunState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunState.Revision,
			RunState.Phase.DayIndex, *UEnum::GetValueAsString(RunState.Phase.Phase),
			*UEnum::GetValueAsString(RunState.EndReason), FormatBool(RunState.Phase.bHasDeadline),
			DeadlineRemaining, FormatBool(RunState.Phase.bFishingAllowed), FormatBool(RunState.Phase.bQuotaOpen),
			RunState.QuotaProgress, RunState.QuotaTarget, *UEnum::GetValueAsString(RunState.Environment.Weather),
			*UEnum::GetValueAsString(RunState.Environment.TimeOfDay),
			FormatBool(RunState.Environment.bHasActiveEvent),
			RunState.Environment.ActiveEventId.IsNone() ? TEXT("--") : *RunState.Environment.ActiveEventId.ToString(),
			RunState.Environment.SourceRunRevision, FormatBool(bEnvironmentRevisionMatches),
			ReadyPlayerCount, PlayerCount, FormatBool(bLocalReady), ChumFieldCount, NaturalChumFieldCount,
			FormatBool(RunState.bTeardownComplete));
	}

	// 屏幕输出流程：使用稳定 key 持续刷新几行短文本；命令关闭后旧文本自然过期，不额外写 UI 状态。
	void PushRunEnvironmentSocialScreenLines(const UObject& Owner, const TArray<FString>& Lines, const float TimeToDisplay)
	{
		if (!GEngine)
		{
			return;
		}
		const uint64 BaseKey = static_cast<uint64>(Owner.GetUniqueID()) * 32ULL;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			GEngine->AddOnScreenDebugMessage(BaseKey + static_cast<uint64>(Index), TimeToDisplay,
				Index == 0 ? FColor::Yellow : FColor::Cyan, Lines[Index]);
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
			PushRunEnvironmentSocialScreenLines(*Owner, BuildRunEnvironmentSocialDebugLines(World, Controller), 6.0f);
		}
	}

	// 白天长度调试流程：先解析唯一秒数参数，再要求当前 World 拥有 authority GameMode；客户端或非法参数只写拒绝日志，成功时由 GameMode 重设当前白天时间窗口并立刻 dump 同一份公开快照。
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
			PushRunEnvironmentSocialScreenLines(*Owner, BuildRunEnvironmentSocialDebugLines(World, Controller), 6.0f);
		}
	}

	/** 非 Shipping 构建里的 RunEnvironmentSocial 一次性快照指令；只读输出，不触发 StateTree、天气或社交流程。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialDump(
		TEXT("cat.RunEnvironmentSocial.Dump"),
		TEXT("输出一次 Run/Environment/Social 调试快照到日志，并在本机屏幕显示 6 秒。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpRunEnvironmentSocialForWorld),
		ECVF_Cheat);

	/** 非 Shipping 构建里的当前白天长度调试指令；只改当前白天时间窗口相关服务器事实和 timer，再通过 GameState 复制给所有客户端。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdRunEnvironmentSocialDayLength(
		TEXT("cat.RunEnvironmentSocial.DayLength"),
		TEXT("重设当前 DayActive 从现在开始持续的秒数，例如 cat.RunEnvironmentSocial.DayLength 60；只能在服务器/ListenServer/Standalone 生效。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetRunEnvironmentSocialDayLengthForWorld),
		ECVF_Cheat);
}
#endif

// 初始化流程：只交还父类建立 WorldSubsystem 生命周期；Debug 开关以及 Dump/DayLength 命令由静态 Console 注册承担，不在这里绑定领域事件。
void UCatRunEnvironmentDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

// 反初始化流程：本子系统没有委托、Timer 或领域对象所有权，只按引擎顺序释放父类资源。
void UCatRunEnvironmentDebugSubsystem::Deinitialize()
{
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

// Tick 调试流程：在游戏线程读取 Debug CVar，开关关闭时完全空转；打开时只读当前本机快照刷新屏幕，并在 Revision 变化时写一条结构化日志。
// 这个 Tick 不提交 StateTree、天气、社交、Wet 或表现状态，CVar 只控制诊断输出是否显示。
void UCatRunEnvironmentDebugSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
#if !UE_BUILD_SHIPPING
	if (CVarCatRunEnvironmentSocialDebug.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = FindDebugController(World);
	PushRunEnvironmentSocialScreenLines(*this, BuildRunEnvironmentSocialDebugLines(World, Controller), 0.5f);

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
