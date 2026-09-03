#include "UI/HUD/CatHUDModel.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"
#include "UI/CatFishingViewBridge.h"

namespace
{
	/** HUD 等待客户端 GameState 的重试间隔；只影响 UI 订阅恢复速度，不改变 Run 复制频率或服务器时钟。 */
	constexpr float CatHUDRunGameStateBindingRetrySeconds = 0.20f;
}

// 绑定流程：校验本地玩家、Controller、Character 和 ASC，随后订阅 Run 快照、三项属性、Condition、Growth 和 Fishing 命令结果，最后发布首份 HUD 投影。
bool UCatHUDModel::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InCharacter || InController->GetPawn() != InCharacter)
	{
		return false;
	}
	UAbilitySystemComponent* AbilitySystem = InCharacter->GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return false;
	}
	FishingViewBridge = NewObject<UCatFishingViewBridge>(this);
	if (!FishingViewBridge)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundCharacter = InCharacter;
	BoundAbilitySystem = AbilitySystem;
	BoundCondition = InCharacter->GetConditionComponent();
	BoundGrowth = InCharacter->GetGrowthComponent();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		BoundFishingCommand = CatController->GetFishingCommandComponent();
	}
	PoisonChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute())
		.AddUObject(this, &ThisClass::HandleAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		ConditionChangedHandle = Condition->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleConditionChanged);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		GrowthChangedHandle = Growth->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleGrowthChanged);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.AddDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	FishingViewChangedHandle = FishingViewBridge->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleFishingViewStateChanged);
	RefreshRunGameStateBinding();
	RefreshFishingSessionBinding();
	Refresh();
	return true;
}

// 解绑流程：从原 Run、ASC、Condition、Growth、Fishing 命令和 Bridge 移除订阅，再清弱引用、最近结果和投影，防止跨 Pawn 显示旧状态。
void UCatHUDModel::Unbind()
{
	ClearRunGameStateBinding();
	if (UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute()).Remove(PoisonChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		Condition->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		Growth->OnSnapshotChanged.Remove(GrowthChangedHandle);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.RemoveDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	if (FishingViewBridge)
	{
		FishingViewBridge->OnViewStateChanged.Remove(FishingViewChangedHandle);
		FishingViewBridge->UnbindSession();
	}
	PoisonChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	GrowthChangedHandle.Reset();
	FishingViewChangedHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundCharacter.Reset();
	BoundAbilitySystem.Reset();
	BoundCondition.Reset();
	BoundGrowth.Reset();
	BoundFishingCommand.Reset();
	FishingViewBridge = nullptr;
	LastFishingCommandResult = FCatFishingCommandResult();
	bHasFishingCommandResult = false;
	ViewState = FCatHUDViewState();
}

// 刷新流程：读取 Run 天数、ASC 三项数值、Condition、Growth 和 FishingBridge 当前投影，再生成 HUD 文本与进度条比例并广播完整状态。
void UCatHUDModel::Refresh()
{
	FCatHUDViewState NewState;
	RefreshRunGameStateBinding();
	APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const AGameStateBase* GameStateBase = World ? World->GetGameState() : nullptr;
	const double ServerNowSeconds = GameStateBase ? GameStateBase->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0);
	if (const ACatfishingGameState* RunGameState = BoundRunGameState.Get())
	{
		NewState.DayIndex = FMath::Max(1, RunGameState->GetRunPublicState().Phase.DayIndex);
	}
	NewState.DayText = FText::FromString(FString::Printf(TEXT("第 %d 天"), NewState.DayIndex));
	if (const UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		NewState.Poison = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
		NewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
		NewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	}
	if (const ACatCharacter* Character = BoundCharacter.Get())
	{
		float FightStaminaBaseline = 0.0f;
		if (GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(
			Character->GetCatDefinitionId(), FightStaminaBaseline))
		{
			NewState.FightStaminaMaximum = FightStaminaBaseline;
			NewState.NormalizedFightStamina = FMath::Clamp(
				NewState.FightStamina / FightStaminaBaseline, 0.0f, 1.0f);
		}
	}
	if (const UCatConditionComponent* Condition = BoundCondition.Get())
	{
		NewState.Condition = Condition->GetSnapshot();
	}
	if (const UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		NewState.Growth = Growth->GetSnapshot();
	}
	if (FishingViewBridge && FishingViewBridge->GetBoundSession())
	{
		NewState.Fishing = FishingViewBridge->GetViewState();
		NewState.bHasFishingSession = true;
		NewState.bShowFishingState = true;
		NewState.NormalizedFishStamina = FMath::Clamp(
			static_cast<float>(NewState.Fishing.NormalizedFishStamina), 0.0f, 1.0f);
		NewState.LineLoadPercent = FMath::Clamp(NewState.Fishing.NormalizedLineLoad, 0.0f, 1.0f);
		const ECatFishingPhase Phase = NewState.Fishing.Phase;
		NewState.bShowBitePrompt = Phase == ECatFishingPhase::TrueBiteWindow;
		NewState.bShowHookCountdown = NewState.bShowBitePrompt
			&& NewState.Fishing.WindowEndsServerTime > ServerNowSeconds;
		NewState.bShowFightMeters = Phase == ECatFishingPhase::HookedFight
			|| Phase == ECatFishingPhase::NearShore
			|| Phase == ECatFishingPhase::AutoHauling
			|| Phase == ECatFishingPhase::ExhaustedReel;
		if (NewState.bShowHookCountdown)
		{
			const double WindowDuration = FMath::Max(
				NewState.Fishing.WindowEndsServerTime - NewState.Fishing.PhaseStartedServerTime, 0.01);
			const double RemainingSeconds = FMath::Max(NewState.Fishing.WindowEndsServerTime - ServerNowSeconds, 0.0);
			NewState.HookCountdownPercent = FMath::Clamp(
				static_cast<float>(RemainingSeconds / WindowDuration), 0.0f, 1.0f);
			NewState.HookCountdownText = FText::FromString(FString::Printf(TEXT("提竿倒计时 %.1f 秒"), RemainingSeconds));
		}
	}
	NewState.LastFishingCommandResult = LastFishingCommandResult;
	NewState.bHasFishingCommandResult = bHasFishingCommandResult;
	NewState.bShowHookSuccessFeedback = NewState.bHasFishingCommandResult
		&& NewState.LastFishingCommandResult.CommandType == ECatFishingCommandType::RequestHook
		&& NewState.LastFishingCommandResult.Error == ECatFishingCommandError::None;
	NewState.BitePromptText = FText::FromString(TEXT("鱼儿咬钩啦！提竿"));
	NewState.HookSuccessFeedbackText = FText::FromString(TEXT("提竿成功！"));
	NewState.CatStaminaText = NewState.FightStaminaMaximum > 0.0f
		? FText::FromString(FString::Printf(TEXT("玩家体力 %.0f / %.0f"),
			NewState.FightStamina, NewState.FightStaminaMaximum))
		: FText::FromString(FString::Printf(TEXT("玩家体力 %.0f"), NewState.FightStamina));
	NewState.FishStaminaText = FText::FromString(FString::Printf(
		TEXT("鱼体力 %.0f%%"), NewState.NormalizedFishStamina * 100.0f));
	if (NewState.HookCountdownText.IsEmpty())
	{
		NewState.HookCountdownText = FText::FromString(TEXT("提竿倒计时"));
	}
	switch (NewState.Fishing.Phase)
	{
	case ECatFishingPhase::CastFlight:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：抛竿中"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：正在落点"));
		break;
	case ECatFishingPhase::Waiting:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：等待咬钩"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：平稳"));
		break;
	case ECatFishingPhase::Probe:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：试探"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：轻微晃动"));
		break;
	case ECatFishingPhase::TrueBiteWindow:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：提竿判定"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：明显下沉"));
		break;
	case ECatFishingPhase::HookedFight:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：遛鱼中"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：已经中鱼"));
		break;
	case ECatFishingPhase::NearShore:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：近岸"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：准备收鱼"));
		break;
	case ECatFishingPhase::AutoHauling:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：自动回收"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：回线中"));
		break;
	case ECatFishingPhase::ExhaustedReel:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：鱼已疲劳"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：继续收线"));
		break;
	case ECatFishingPhase::Resolved:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：已结算"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：会话结束"));
		break;
	case ECatFishingPhase::Terminated:
		NewState.FishingStateText = FText::FromString(TEXT("钓鱼状态：已中止"));
		NewState.BobberFeedbackText = FText::FromString(TEXT("鱼漂反馈：会话中止"));
		break;
	default:
		NewState.FishingStateText = NewState.bHasFishingSession
			? FText::FromString(TEXT("钓鱼状态：准备"))
			: FText::FromString(TEXT("钓鱼状态：未开始"));
		NewState.BobberFeedbackText = NewState.bHasFishingSession
			? FText::FromString(TEXT("鱼漂反馈：等待反馈"))
			: FText::FromString(TEXT("鱼漂反馈：未入水"));
		break;
	}
	switch (NewState.Fishing.FishMotionIntent)
	{
	case ECatFishMotionIntent::CalmOrInward:
		NewState.FishStateText = FText::FromString(TEXT("鱼状态：回游或疲劳"));
		break;
	case ECatFishMotionIntent::StrugglingOutward:
		NewState.FishStateText = NewState.Fishing.bStrongConfrontation
			? FText::FromString(TEXT("鱼状态：强烈挣扎"))
			: FText::FromString(TEXT("鱼状态：向外挣扎"));
		break;
	case ECatFishMotionIntent::AutoHauling:
		NewState.FishStateText = FText::FromString(TEXT("鱼状态：可拖回"));
		break;
	default:
		NewState.FishStateText = NewState.bShowFightMeters
			? FText::FromString(TEXT("鱼状态：观察中"))
			: FText::FromString(TEXT("鱼状态：未进入遛鱼"));
		break;
	}
	NewState.CatStatusText = FText::FromString(FString::Printf(TEXT("猫状态：中毒 %.0f | 钓鱼力量 %.0f | 搏斗体力 %.0f | 成长总经验 %d，当前槽 %d，待选 %d"),
		NewState.Poison,
		NewState.FishingStrength,
		NewState.FightStamina,
		NewState.Growth.TotalExperience,
		NewState.Growth.ExperienceInCurrentSlot,
		NewState.Growth.PendingChoiceCount));
	NewState.FishingFeedbackText = NewState.bHasFishingSession
		? FText::FromString(TEXT("钓鱼反馈：正在钓鱼，等待会话更新"))
		: FText::FromString(TEXT("钓鱼反馈：当前没有进行中的钓鱼会话"));
	if (NewState.bHasFishingCommandResult)
	{
		NewState.FishingFeedbackText = FText::FromString(FString::Printf(TEXT("钓鱼反馈：最近命令 %s，版本 %lld"),
			*UEnum::GetValueAsString(NewState.LastFishingCommandResult.Error),
			NewState.LastFishingCommandResult.Revision));
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// ViewState 读取流程：返回最近 HUD 投影；调用方不能通过它访问 ASC 或会话对象。
const FCatHUDViewState& UCatHUDModel::GetViewState() const
{
	return ViewState;
}

// 销毁兜底流程：先复用 Unbind 路径清理委托、FishingBridge 和等待 Timer，再交给 UObject 释放自身引用；这不发布新的 HUD 投影。
void UCatHUDModel::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

// 属性变化流程：事件只表达事实变更，Model 统一重读三项 HUD 数值。
void UCatHUDModel::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	Refresh();
}

// Condition 变化流程：重读完整 HUD 事实，避免增量顺序形成 UI 私有状态。
void UCatHUDModel::HandleConditionChanged()
{
	Refresh();
}

// Growth 变化流程：重读完整 HUD 事实，让经验槽、待选次数和身体状态保持同帧投影。
void UCatHUDModel::HandleGrowthChanged()
{
	Refresh();
}

// Run GameState 绑定调和流程：先从当前 Controller 的 World 读取最新 GameState；若和已绑定对象不同则成对解除旧委托并绑定新委托；找不到时启动短重试，找到后立刻刷新并停重试。
bool UCatHUDModel::RefreshRunGameStateBinding()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	ACatfishingGameState* CurrentGameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	if (BoundRunGameState.Get() == CurrentGameState && CurrentGameState)
	{
		ClearRunGameStateBindingRetry();
		return true;
	}

	if (ACatfishingGameState* PreviousGameState = BoundRunGameState.Get())
	{
		PreviousGameState->OnRunPublicStateChanged.Remove(RunPublicStateChangedHandle);
	}
	RunPublicStateChangedHandle.Reset();
	BoundRunGameState = CurrentGameState;

	if (!CurrentGameState)
	{
		ScheduleRunGameStateBindingRetry();
		return false;
	}

	RunPublicStateChangedHandle = CurrentGameState->OnRunPublicStateChanged.AddUObject(
		this, &ThisClass::HandleRunPublicStateChanged);
	ClearRunGameStateBindingRetry();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_hud_run_gamestate_bound World=%s NetMode=%d Revision=%lld Day=%d Phase=%s"),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : INDEX_NONE,
		CurrentGameState->GetRunPublicState().Revision,
		CurrentGameState->GetRunPublicState().Phase.DayIndex,
		*UEnum::GetValueAsString(CurrentGameState->GetRunPublicState().Phase.Phase));
	return true;
}

// Run GameState 解绑流程：先停止等待 Timer，再从仍有效的 GameState 移除委托，最后清空弱引用和句柄；旧 World 已销毁时弱引用为空也保持幂等。
void UCatHUDModel::ClearRunGameStateBinding()
{
	ClearRunGameStateBindingRetry();
	if (ACatfishingGameState* RunGameState = BoundRunGameState.Get())
	{
		RunGameState->OnRunPublicStateChanged.Remove(RunPublicStateChangedHandle);
	}
	RunPublicStateChangedHandle.Reset();
	BoundRunGameState.Reset();
}

// Run GameState 等待安排流程：只在还有 Controller/World 且当前没有活跃重试 Timer 时注册本地轮询；轮询目的是等复制宿主出现，不读取或修改 Run 内容。
void UCatHUDModel::ScheduleRunGameStateBindingRetry()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (RunGameStateBindingRetryTimerHandle.IsValid()
		&& RunGameStateBindingRetryWorld.Get() == World
		&& World->GetTimerManager().IsTimerActive(RunGameStateBindingRetryTimerHandle))
	{
		return;
	}
	ClearRunGameStateBindingRetry();
	RunGameStateBindingRetryWorld = World;
	World->GetTimerManager().SetTimer(RunGameStateBindingRetryTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::HandleRunGameStateBindingRetry),
		CatHUDRunGameStateBindingRetrySeconds, true);
}

// Run GameState 等待清理流程：优先回到创建 Timer 的 World 清理，缺失时才用当前 Controller World 兜底；无论清理是否命中都让句柄和所属 World 失效。
void UCatHUDModel::ClearRunGameStateBindingRetry()
{
	UWorld* TimerWorld = RunGameStateBindingRetryWorld.Get();
	if (!TimerWorld)
	{
		APlayerController* Controller = BoundPlayerController.Get();
		TimerWorld = Controller ? Controller->GetWorld() : nullptr;
	}
	if (TimerWorld)
	{
		TimerWorld->GetTimerManager().ClearTimer(RunGameStateBindingRetryTimerHandle);
	}
	RunGameStateBindingRetryTimerHandle.Invalidate();
	RunGameStateBindingRetryWorld.Reset();
}

// Run GameState 重试流程：每次只尝试补齐委托绑定；绑定成功后重读 HUD 投影，让客户端晚到的第一份 Run 快照也能立刻显示在左上角。
void UCatHUDModel::HandleRunGameStateBindingRetry()
{
	if (RefreshRunGameStateBinding())
	{
		Refresh();
	}
}

// Run 快照变化流程：客户端 OnRep 或服务器本机写入到达后统一刷新 HUD；Model 不缓存第二份天数，只重新读取 GameState。
void UCatHUDModel::HandleRunPublicStateChanged()
{
	Refresh();
}

// Fishing 投影变化流程：Bridge 已保存最新会话 DTO，HUD 只重建展示文本。
void UCatHUDModel::HandleFishingViewStateChanged(const FCatFishingViewState& InViewState)
{
	(void)InViewState;
	Refresh();
}

// Fishing 结果流程：缓存最近命令终态，重新定位可能新建的 FishingSession，然后刷新 HUD 反馈。
void UCatHUDModel::HandleFishingCommandResult(const FCatFishingCommandResult& Result)
{
	LastFishingCommandResult = Result;
	bHasFishingCommandResult = true;
	RefreshFishingSessionBinding();
	Refresh();
}

// Session 调和流程：按当前 PlayerState 找客户端可见 FishingSession；会话变化时让 Bridge 重新绑定，找不到就清空会话投影。
void UCatHUDModel::RefreshFishingSessionBinding()
{
	if (!FishingViewBridge)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(
		Controller, PlayerState);
	if (FishingViewBridge->GetBoundSession() == Session)
	{
		return;
	}
	if (Session)
	{
		FishingViewBridge->BindSession(Session);
	}
	else
	{
		FishingViewBridge->UnbindSession();
	}
}
