#include "UI/HUD/CatHUDModel.h"

#include "FishContainers/CatFishContainerSettings.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "Camp/CatCampHubActor.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "FishContainers/CatFishTankActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UI/WorldInfo/CatFishTankWorldInfoComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "TimerManager.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "Engine/LocalPlayer.h"

namespace
{
	/** HUD 等待客户端 GameState 的重试间隔；只影响 UI 订阅恢复速度，不改变 Run 复制频率或服务器时钟。 */
	constexpr float CatHUDRunGameStateBindingRetrySeconds = 0.20f;
	constexpr float CatHUDFishingSessionBindingReconcileSeconds = 0.20f;

	/**
	 * 接上 GameState 之后多久以内到达的公开流水算既往账本。中途进局的第一次复制会一次性带来整本流水，
	 * 那是历史不是事件；正常成交离接线远得多，落不进这个窗口。
	 */
	constexpr double CatHUDPurchaseBroadcastSeedGraceSeconds = 1.0;

	// 商品名解析流程：实物读取库存定义；设施没有库存定义，按同一升级配置输出目标容量。
	// 摊位的展示覆盖仍由商店页绑定；其他缺配定义保留 ID，方便内容交付核对。
	FText MakePurchaseItemNameText(const FName DefinitionId, const FName EntryId)
	{
		const auto* Containers = GetDefault<UCatFishContainerSettings>();
		const int32 Tier = Containers->FindSharedFishTankUpgradeTierByDefinitionId(DefinitionId);
		if (Tier != INDEX_NONE)
			return FText::FromString(FString::Printf(TEXT("鱼缸容量升级至 %d 条"), Containers->GetSharedFishTankCapacityForTier(Tier)));
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		const UCatInventoryItemDefinition* Definition =
			(InventorySettings && !DefinitionId.IsNone()) ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
		const FText DefinitionNameText = Definition ? Definition->GetInventoryDisplayName() : FText();
		if (!DefinitionNameText.IsEmpty())
		{
			return DefinitionNameText;
		}
		return FText::FromName(DefinitionId.IsNone() ? EntryId : DefinitionId);
	}
}

// 绑定流程：校验本地玩家、Controller、Character 和 ASC，随后订阅身体属性、Condition、Growth 和 Fishing 命令结果；Run 快照按“先读一次当前 GameState，再订阅后续变化”的观察者口径接线，最后保证至少发布首份 HUD 投影。
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
	BoundAbilitySystem = AbilitySystem;
	BoundCondition = InCharacter->GetConditionComponent();
	BoundGrowth = InCharacter->GetGrowthComponent();
	BoundEquipment = InCharacter->GetEquipmentComponent();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		BoundFishingCommand = CatController->GetFishingCommandComponent();
	}
	YellowFightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	MaxFightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		ConditionChangedHandle = Condition->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleConditionChanged);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		GrowthChangedHandle = Growth->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleGrowthChanged);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		EquipmentSnapshotChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleEquipmentSnapshotChanged);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.AddDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	FishingViewChangedHandle = FishingViewBridge->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleFishingViewStateChanged);
	RefreshFishingSessionBinding();
	ScheduleFishingSessionBindingReconcile();
	if (!RefreshRunGameStateBinding())
	{
		Refresh();
	}
	return true;
}

// 解绑流程：从原 Run、ASC、Condition、Growth、Fishing 命令和 Bridge 移除订阅，再清弱引用、最近结果和投影，防止跨 Pawn 显示旧状态。
void UCatHUDModel::Unbind()
{
	ClearFishingSessionBindingReconcile();
	ClearRunGameStateBinding();
	if (UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()).Remove(YellowFightStaminaChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()).Remove(MaxFightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		Condition->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		Growth->OnSnapshotChanged.Remove(GrowthChangedHandle);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnSnapshotChanged.Remove(EquipmentSnapshotChangedHandle);
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
	YellowFightStaminaChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	MaxFightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	GrowthChangedHandle.Reset();
	EquipmentSnapshotChangedHandle.Reset();
	FishingViewChangedHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundAbilitySystem.Reset();
	BoundCondition.Reset();
	BoundGrowth.Reset();
	BoundEquipment.Reset();
	CachedTankInfo.Reset();
	BoundFishingCommand.Reset();
	FishingViewBridge = nullptr;
	LastFishingCommandResult = FCatFishingCommandResult();
	bHasFishingCommandResult = false;
	ViewState = FCatHUDViewState();
}

// 刷新流程：从已经绑定的 GameState 读取 Run 天数，再读取 ASC 身体数值、Condition、Growth 和 FishingBridge 当前投影，生成 HUD 文本与进度条比例并广播完整状态；它不负责寻找或订阅 GameState。
void UCatHUDModel::Refresh()
{
	FCatHUDViewState NewState;
	APlayerController* Controller = BoundPlayerController.Get();
	const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatPhysicalBodyComponent* Body = Character ? Character->GetPhysicalBodyComponent() : nullptr;
	if (const UCatPhysicsGrabComponent* Grab = Body ? Body->GetGrab() : nullptr)
	{
		NewState.bShowPhysicalControls = true;
		NewState.bLeftHandReaching = Grab->IsReaching(true);
		NewState.bRightHandReaching = Grab->IsReaching(false);
		NewState.bLeftHandGripped = Grab->IsGripping(true);
		NewState.bRightHandGripped = Grab->IsGripping(false);
		const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(Controller);
		NewState.bPrimaryRodOperator = Rod && Rod->IsPrimaryOperator(Controller->PlayerState);
		NewState.PhysicalControlText = FText::FromString(NewState.bPrimaryRodOperator
			? TEXT("主控 · 左键抛竿 / 收线 · 右键放线 · R 放竿")
			: TEXT("按住左 / 右键抓人或抓竿 · WASD 拉动 · 松键释放"));
		const auto HandLabel = [&](const bool bLeft, const bool bReaching, const bool bGripped)
		{
			if (bGripped && Grab->GetGripState(bLeft).bExplicitHold && NewState.bPrimaryRodOperator && Grab->GetGripTarget(bLeft) == Rod)
				return TEXT("持竿（R 放竿）");
			return bGripped ? TEXT("抓住（松键释放）") : (bReaching ? TEXT("伸手中") : TEXT("收回"));
		};
		NewState.PhysicalHandStateText = FText::FromString(FString::Printf(TEXT("左爪：%s    右爪：%s"),
			HandLabel(true, NewState.bLeftHandReaching, NewState.bLeftHandGripped),
			HandLabel(false, NewState.bRightHandReaching, NewState.bRightHandGripped)));
	}
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const AGameStateBase* GameStateBase = World ? World->GetGameState() : nullptr;
	const double ServerNowSeconds = GameStateBase ? GameStateBase->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0);
	if (const ACatfishingGameState* RunGameState = BoundRunGameState.Get())
	{
		const FCatRunPublicState& Run = RunGameState->GetRunPublicState();
		NewState.DayIndex = FMath::Max(1, Run.Phase.DayIndex);
		// 公款只有 GameState 上这一份，且已经复制给每个客户端；HUD 常驻位读它，不另存第二份余额。
		const FCatShopPublicEconomySnapshot& Economy = RunGameState->GetShopEconomySnapshot();
		NewState.bHasTeamWallet = true;
		NewState.TeamWalletBalance = Economy.Balance;
		NewState.TeamWalletRevision = Economy.WalletRevision;
		// 时段两个来源：白天段读 Environment 的时段轴，夜晚不在那条轴上（环境册 §3.1.1），由 Phase 直接给。
		const bool bRunReady = Run.Phase.RunId.IsValid() && Run.Phase.Phase != ECatRunPhase::NotStarted;
		const bool bDaytime = Run.Phase.Phase == ECatRunPhase::DayActive;
		const bool bNight = Run.Phase.Phase == ECatRunPhase::NormalNight
			|| Run.Phase.Phase == ECatRunPhase::FailureSettlementNight
			|| Run.Phase.Phase == ECatRunPhase::SuccessSettlementNight;
		if (bRunReady && bDaytime)
		{
			switch (Run.Environment.TimeOfDay)
			{
			case ECatEnvironmentTimeOfDay::Morning:
				NewState.TimeOfDayText = FText::FromString(TEXT("清晨"));
				NewState.bShowTimeOfDay = true;
				break;
			case ECatEnvironmentTimeOfDay::Day:
				NewState.TimeOfDayText = FText::FromString(TEXT("白天"));
				NewState.bShowTimeOfDay = true;
				break;
			case ECatEnvironmentTimeOfDay::Dusk:
				NewState.TimeOfDayText = FText::FromString(TEXT("黄昏"));
				NewState.bShowTimeOfDay = true;
				break;
			default:
				// 时段轴还没算出来（配置未就绪或刚进入白天）：宁可不显示，也不写「未知」占住那一格。
				break;
			}
		}
		else if (bRunReady && bNight)
		{
			NewState.TimeOfDayText = FText::FromString(TEXT("夜晚"));
			NewState.bShowTimeOfDay = true;
		}
		// 三个量的前两个：当日任务点数与缸内可献点数按 ui 表第 18 行白天常驻。
		NewState.bHasDailyOfferingTarget = bRunReady && Run.DailyOfferingTarget > 0;
		NewState.DailyOfferingTarget = NewState.bHasDailyOfferingTarget ? Run.DailyOfferingTarget : 0;
		NewState.bShowOfferingCounters = bRunReady && bDaytime;
		// 第三个量：世界进度平时隐藏，靠近神像（祭坛信息牌）或打开界面（这里）时才查看。
		NewState.bHasWorldProgress = bRunReady;
		NewState.WorldProgress = bRunReady ? Run.WorldProgress : 0;
		NewState.NormalizedWorldProgress = bRunReady ? FMath::Clamp(Run.WorldProgress / 100.0f, 0.0f, 1.0f) : 0.0f;
		NewState.bShowWorldProgress = IsAnyInterfacePageOpen();
	}
	NewState.DayText = FText::FromString(FString::Printf(TEXT("第 %d 天"), NewState.DayIndex));
	RefreshTankOfferingProjection(NewState);
	NewState.DailyOfferingTargetText = NewState.bHasDailyOfferingTarget
		? FText::FromString(FString::Printf(TEXT("今日任务 %d 点"), NewState.DailyOfferingTarget))
		: FText::FromString(TEXT("今日任务 未同步"));
	NewState.TankOfferableText = NewState.bHasTankOfferablePoints
		? FText::FromString(FString::Printf(TEXT("缸内可献 %d 点"), NewState.TankOfferablePoints))
		: FText::FromString(TEXT("缸内可献 未同步"));
	NewState.WorldProgressText = NewState.bHasWorldProgress
		? FText::FromString(FString::Printf(TEXT("世界进度 %d%%"), NewState.WorldProgress))
		: FText::FromString(TEXT("世界进度 未同步"));
	NewState.TeamWalletText = NewState.bHasTeamWallet
		? FText::FromString(FString::Printf(TEXT("团队公款 %d"), NewState.TeamWalletBalance))
		: FText::FromString(TEXT("团队公款 未同步"));
	NewState.PurchaseBroadcasts = PurchaseBroadcasts;
	if (!NewState.PurchaseBroadcasts.IsEmpty())
	{
		const FCatHUDPurchaseBroadcast& LatestBroadcast = NewState.PurchaseBroadcasts.Last();
		NewState.PurchaseBroadcastText = LatestBroadcast.BroadcastText;
		// 展示窗口到点后由 HUD Widget 的本地 Tick 收起；这里只负责给出「刚发生过」这一次判断。
		NewState.bShowPurchaseBroadcast = ServerNowSeconds - LatestBroadcast.AnnouncedServerTime
			<= CatHUDPurchaseBroadcastLimits::VisibleSeconds;
	}
	if (const UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		NewState.YellowFightStamina = AbilitySystem->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute());
		NewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
		NewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		NewState.FightStaminaMaximum = AbilitySystem->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
		if (NewState.FightStaminaMaximum > 0.0f)
		{
			NewState.NormalizedFightStamina = FMath::Clamp(
				NewState.FightStamina / NewState.FightStaminaMaximum, 0.0f, 1.0f);
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
	NewState.BitePromptText = FText::FromString(NewState.bShowBitePrompt
		&& ServerNowSeconds <= NewState.Fishing.PerfectWindowEndsServerTime
		? TEXT("就是现在！完美提竿") : TEXT("鱼儿咬钩啦！提竿"));
	NewState.HookSuccessFeedbackText = FText::FromString(TEXT("提竿成功！"));
	// 墓碑（2026-09-14）：删除绿零即濒死的口径；Knowledge/Design/设计修改记录.md
	// 2026-09-13 裁决②。绿黄渲染字段保持原义，提示和文本使用总量，不重复计算黄条。
	const float TotalStamina = NewState.FightStamina + NewState.YellowFightStamina;
	const float TotalCapacity = NewState.FightStaminaMaximum + NewState.YellowFightStamina;
	NewState.CatStaminaText = FText::FromString(FString::Printf(TEXT("玩家体力 %.0f / %.0f"), TotalStamina, TotalCapacity));
	NewState.bShowPersonalStamina = NewState.FightStaminaMaximum > 0
		&& (NewState.FightStamina < NewState.FightStaminaMaximum || NewState.YellowFightStamina > 0);
	NewState.bNearDeath = TotalCapacity > 0.0f
		&& TotalStamina / TotalCapacity <= CatHUDFightStaminaLimits::NearDeathStaminaFraction;
	NewState.NearDeathText = FText::FromString(TEXT("体力见底了！"));
	// 竿耐久（ui 表第 17 行）：值来自 Equipment 复制的钓鱼选择读模型，上限来自鱼竿定义片段。
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		const UCatInventoryItemDefinition* RodDefinition = (InventorySettings && !Loadout.RodDefinitionId.IsNone())
			? InventorySettings->FindRuntimeDefinition(Loadout.RodDefinitionId) : nullptr;
		const UCatEquipmentFragment_Rod* RodFragment = RodDefinition
			? RodDefinition->FindFragment<UCatEquipmentFragment_Rod>() : nullptr;
		const double MaximumDurability = RodFragment ? RodFragment->MaximumRodDurability : 0.0;
		NewState.bRodBroken = Loadout.bRodBroken;
		NewState.RodDurability = static_cast<float>(Loadout.RodDurability);
		NewState.RodDurabilityMaximum = static_cast<float>(MaximumDurability);
		NewState.bHasRodDurability = !Loadout.RodDefinitionId.IsNone()
			&& FMath::IsFinite(Loadout.RodDurability) && MaximumDurability > 0.0;
		if (NewState.bHasRodDurability)
		{
			NewState.NormalizedRodDurability = FMath::Clamp(
				NewState.RodDurability / NewState.RodDurabilityMaximum, 0.0f, 1.0f);
		}
	}
	// 「手持鱼竿时」两条都算：正在用爪子握着这根竿，或者竿已经抛出去、这一竿仍是本人的会话。
	const bool bHoldingRod = UCatFishingCameraComponent::FindHeldRodOperatedBy(Controller) != nullptr;
	NewState.bShowRodDurability = NewState.bHasRodDurability && (bHoldingRod || NewState.bHasFishingSession);
	// FString::Printf 的格式串必须是编译期字面量（UE 5.8 的 TCheckedFormatString 是 consteval），
	// 所以断裂与否在外层分支，不能用三元运算符选格式串。
	// 「（已断裂）」不是死文案，别当 09-12「断竿即报废」的遗留删掉。2026-09-13 核过两条可达路径：
	// ①报废会失败——UCatEquipmentComponent::RetireBrokenFishingRodFromAuthority 有多个 return false
	//   分支（记 equipment_broken_rod_retire_failed），失败时断竿留在库存格里；
	// ②存档恢复——CatSaveSubsystem.cpp:281 SetRodRuntimeStateFromAuthority 会把槽里的
	//   bRodBroken 原样写回活装备。同口径的另一处是 CatItemTooltipModel.cpp 的断竿 tooltip。
	NewState.RodDurabilityText = NewState.bHasRodDurability
		? FText::FromString(NewState.bRodBroken
			? FString::Printf(TEXT("竿耐久 %.0f / %.0f（已断裂）"), NewState.RodDurability, NewState.RodDurabilityMaximum)
			: FString::Printf(TEXT("竿耐久 %.0f / %.0f"), NewState.RodDurability, NewState.RodDurabilityMaximum))
		: FText::FromString(TEXT("竿耐久 未同步"));
	RefreshTeammateProjection(NewState);
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
	// 墓碑（2026-09-12）：这里原本每次投影都拼一行常驻「猫状态：中毒 x｜钓鱼力量 x｜搏斗体力 x｜成长总经验 x…」
	// 的开发期调试文本，与「无常驻状态条、成长信息只在需要的时刻显示」的口径相反（猫册 §7、数值成长页 §6），
	// 随它一起删掉的还有 bShowCatStatusDebugText 与 WBP 上的 CatStatusTextBlock 绑定。
	// 成长事实本身仍在投影里（Growth／Condition 快照、黄色体力、力量与体力），
	// 供按需出现的三条通道读取：吃鱼瞬时浮层、三选一卡面、主动查看面板。
	// 这里只给出「现在需要露面」的那一个判断：有待选的三选一。
	NewState.bHasPendingGrowthChoice = NewState.Growth.PendingChoiceCount > 0
		&& NewState.Growth.CurrentOffer.Num() > 0;
	NewState.FishingFeedbackText = NewState.bHasFishingSession
		? FText::FromString(TEXT("钓鱼反馈：正在钓鱼，等待会话更新"))
		: FText::FromString(TEXT("钓鱼反馈：当前没有进行中的钓鱼会话"));
	if (NewState.bHasFishingCommandResult)
	{
		NewState.FishingFeedbackText = NewState.LastFishingCommandResult.Error == ECatFishingCommandError::RodDeploymentLimitReached
			? FText::FromString(TEXT("场上鱼竿已达上限，请先收起一根。"))
			: FText::FromString(FString::Printf(TEXT("钓鱼反馈：最近命令 %s，版本 %lld"),
				*UEnum::GetValueAsString(NewState.LastFishingCommandResult.Error),
				NewState.LastFishingCommandResult.Revision));
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 缸内可献点数投影流程：营地宿主 → 显式关联的共享鱼缸 → 鱼缸自己复制的只读摘要组件。
// 这是祭坛信息牌用的同一条链（CatAltarWorldInfoComponent），HUD 不重算鱼的档位、不扫描世界配对鱼缸。
// 弱引用缓存只为省掉每次投影的 Actor 遍历；缓存失效（旅行、鱼缸销毁）后按同一条链重解析一次。
void UCatHUDModel::RefreshTankOfferingProjection(FCatHUDViewState& NewState)
{
	UWorld* World = BoundPlayerController.IsValid() ? BoundPlayerController->GetWorld() : nullptr;
	if (!World)
	{
		CachedTankInfo.Reset();
		return;
	}
	if (!CachedTankInfo.IsValid())
	{
		for (TActorIterator<ACatCampHubActor> CampIterator(World); CampIterator; ++CampIterator)
		{
			const ACatFishTankActor* SharedTank = CampIterator->ResolveSharedFishTank();
			if (UCatFishTankWorldInfoComponent* TankInfo = SharedTank
				? SharedTank->FindComponentByClass<UCatFishTankWorldInfoComponent>() : nullptr)
			{
				CachedTankInfo = TankInfo;
				break;
			}
		}
	}
	const UCatFishTankWorldInfoComponent* TankInfo = CachedTankInfo.Get();
	int32 Points = 0;
	int32 Count = 0;
	int32 Capacity = 0;
	NewState.bHasTankOfferablePoints = TankInfo && TankInfo->TryGetOfferingSummary(Points, Count, Capacity);
	NewState.TankOfferablePoints = NewState.bHasTankOfferablePoints ? Points : 0;
}

// 队友投影流程：遍历 GameState 的 PlayerArray，只读每名玩家已经复制到本机的事实。
// 方向取那只猫自己的移动意图（CharacterMovement 的加速度，对模拟代理也复制），投影到它自己的朝向上——
// 不读别人的控制旋转（模拟代理没有），也不把本机相机的前方当成别人的前方。
// 移动状态只给客观的三件事；2026-09-11 裁决①把 ρ 三档与六个群体动作整套作废后，
// 「齐步走／被拖行／原地打转／顶牛中」不再有事实来源，这里不造。
void UCatHUDModel::RefreshTeammateProjection(FCatHUDViewState& NewState)
{
	NewState.Teammates.Reset();
	NewState.bShowTeammates = false;
	const APlayerController* Controller = BoundPlayerController.Get();
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const AGameStateBase* TeamGameState = World ? World->GetGameState() : nullptr;
	if (!TeamGameState)
	{
		return;
	}
	const APlayerState* LocalPlayerState = Controller->PlayerState;
	for (APlayerState* PlayerState : TeamGameState->PlayerArray)
	{
		if (!PlayerState || PlayerState->IsOnlyASpectator())
		{
			continue;
		}
		FCatHUDTeammateState& Teammate = NewState.Teammates.AddDefaulted_GetRef();
		Teammate.PlayerId = PlayerState->GetPlayerId();
		Teammate.bIsLocalPlayer = PlayerState == LocalPlayerState;
		Teammate.DisplayNameText = FText::FromString(PlayerState->GetPlayerName());
		const ACatCharacter* TeammateCharacter = Cast<ACatCharacter>(PlayerState->GetPawn());
		if (!TeammateCharacter)
		{
			// 还没出生或本机尚未收到 Pawn：保留这一行（人确实在局里），但不编造方向和体力。
			Teammate.MovementStatusText = FText::FromString(TEXT("未同步"));
			continue;
		}
		if (const UCharacterMovementComponent* Movement = TeammateCharacter->GetCharacterMovement())
		{
			const FVector Acceleration = Movement->GetCurrentAcceleration();
			if (!Acceleration.IsNearlyZero())
			{
				const FVector PlanarIntent = FVector(Acceleration.X, Acceleration.Y, 0.0).GetSafeNormal();
				const FVector Forward = FVector::VectorPlaneProject(
					TeammateCharacter->GetActorForwardVector(), FVector::UpVector).GetSafeNormal();
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
				const double ForwardDot = FVector::DotProduct(PlanarIntent, Forward);
				const double RightDot = FVector::DotProduct(PlanarIntent, Right);
				if (!PlanarIntent.IsNearlyZero() && !Forward.IsNearlyZero())
				{
					Teammate.MoveDirection = FMath::Abs(ForwardDot) >= FMath::Abs(RightDot)
						? (ForwardDot >= 0.0 ? ECatHUDMoveDirection::Forward : ECatHUDMoveDirection::Backward)
						: (RightDot >= 0.0 ? ECatHUDMoveDirection::Right : ECatHUDMoveDirection::Left);
				}
			}
		}
		if (const UAbilitySystemComponent* TeammateAbilitySystem = TeammateCharacter->GetAbilitySystemComponent())
		{
			const float TeammateYellow = TeammateAbilitySystem->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute());
			const float TeammateStamina = TeammateAbilitySystem->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetFightStaminaAttribute()) + TeammateYellow;
			const float TeammateMaximum = TeammateAbilitySystem->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()) + TeammateYellow;
			Teammate.bHasStamina = FMath::IsFinite(TeammateMaximum) && TeammateMaximum > 0.0f
				&& FMath::IsFinite(TeammateStamina);
			if (Teammate.bHasStamina)
			{
				Teammate.NormalizedStamina = FMath::Clamp(TeammateStamina / TeammateMaximum, 0.0f, 1.0f);
				Teammate.bExhausted = TeammateStamina <= 0.0f;
				Teammate.bNearDeath = Teammate.NormalizedStamina <= CatHUDFightStaminaLimits::NearDeathStaminaFraction;
			}
		}
		if (const UCatConditionComponent* TeammateCondition = TeammateCharacter->GetConditionComponent())
		{
			Teammate.bDowned = TeammateCondition->GetSnapshot().bDowned;
		}
		Teammate.MovementStatusText = Teammate.bDowned ? FText::FromString(TEXT("倒地"))
			: Teammate.bExhausted ? FText::FromString(TEXT("体力耗尽"))
			: Teammate.MoveDirection != ECatHUDMoveDirection::None ? FText::FromString(TEXT("移动中"))
			: FText::FromString(TEXT("静止"));
	}
	NewState.Teammates.Sort([](const FCatHUDTeammateState& Left, const FCatHUDTeammateState& Right)
	{
		return Left.PlayerId < Right.PlayerId;
	});
	// 单人局不占屏幕：只有确实存在别的玩家时顶部队友条才露面。
	NewState.bShowTeammates = NewState.Teammates.ContainsByPredicate(
		[](const FCatHUDTeammateState& Teammate) { return !Teammate.bIsLocalPlayer; });
}

// 界面开合读取流程：只问 LocalPlayer UI 协调层「现在有没有开着页」，不在 HUD 里存第二份开合状态。
// 三个页共用一层模态输入锁，所以任一开着都算「打开了界面」。
bool UCatHUDModel::IsAnyInterfacePageOpen() const
{
	const ULocalPlayer* LocalPlayer = BoundLocalPlayer.Get();
	const UCatLocalPlayerUISubsystem* PlayerUI = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return PlayerUI && PlayerUI->IsAnyPlayerPageOpen();
}

// Equipment 变化流程：换竿、磨损写回与断竿都只是事实变更，Model 统一重读完整投影。
void UCatHUDModel::HandleEquipmentSnapshotChanged()
{
	Refresh();
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

// Run GameState 绑定调和流程：先从当前 Controller 的 World 读取最新 GameState；找不到时启动短重试，找到后按观察者模式先刷新一次 HUD 投影，再订阅后续 OnRep/服务器本机写入通知。
// Run 天数和团队公款/公开流水都挂在这一个 GameState 上，所以两条订阅在这里一起接、一起断，不各自维护一份宿主。
bool UCatHUDModel::RefreshRunGameStateBinding()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	ACatfishingGameState* CurrentGameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	// Run 快照和商店快照在这里成对接线，所以两个句柄都得成立才算已接上；只补一个会让余额和广播永远停在旧值。
	if (BoundRunGameState.Get() == CurrentGameState && CurrentGameState
		&& RunPublicStateChangedHandle.IsValid() && ShopEconomySnapshotChangedHandle.IsValid())
	{
		ClearRunGameStateBindingRetry();
		return true;
	}

	if (ACatfishingGameState* PreviousGameState = BoundRunGameState.Get())
	{
		PreviousGameState->OnRunPublicStateChanged.Remove(RunPublicStateChangedHandle);
		PreviousGameState->OnShopEconomySnapshotChanged.Remove(ShopEconomySnapshotChangedHandle);
	}
	RunPublicStateChangedHandle.Reset();
	ShopEconomySnapshotChangedHandle.Reset();
	// 换到另一份 GameState 就是换了一局公开流水；旧局播报过的整车 ID 在新账本里没有意义。
	ResetPurchaseBroadcastState();
	BoundRunGameState = CurrentGameState;

	if (!CurrentGameState)
	{
		ScheduleRunGameStateBindingRetry();
		return false;
	}

	ClearRunGameStateBindingRetry();
	// 公款和公开流水跟 Run 快照挂在同一个 GameState 上，所以复用同一次接线：
	// 先把已经复制到本机的既往成交折成广播，再刷新一次投影，最后订阅后续变化。
	RefreshPurchaseBroadcasts();
	Refresh();
	RunPublicStateChangedHandle = CurrentGameState->OnRunPublicStateChanged.AddUObject(
		this, &ThisClass::HandleRunPublicStateChanged);
	ShopEconomySnapshotChangedHandle = CurrentGameState->OnShopEconomySnapshotChanged.AddUObject(
		this, &ThisClass::HandleShopEconomySnapshotChanged);
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
		RunGameState->OnShopEconomySnapshotChanged.Remove(ShopEconomySnapshotChangedHandle);
	}
	RunPublicStateChangedHandle.Reset();
	ShopEconomySnapshotChangedHandle.Reset();
	ResetPurchaseBroadcastState();
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

// Run GameState 重试流程：每次只尝试补齐委托绑定；绑定函数会先重读 HUD 投影再订阅，让客户端晚到的第一份 Run 快照也能立刻显示在左上角。
void UCatHUDModel::HandleRunGameStateBindingRetry()
{
	RefreshRunGameStateBinding();
}

// Run 快照变化流程：客户端 OnRep 或服务器本机写入到达后统一刷新 HUD；Model 不缓存第二份天数，只重新读取 GameState。
void UCatHUDModel::HandleRunPublicStateChanged()
{
	Refresh();
}

// 商店快照变化流程：先把新成交折成全场广播队列，再重读完整投影；余额和广播都只来自这份复制事实。
void UCatHUDModel::HandleShopEconomySnapshotChanged()
{
	RefreshPurchaseBroadcasts();
	Refresh();
}

// 服务器按 CartId 提供一条完整成交事实；相邻同买家订单保持独立，售鱼同样发布收入。
void UCatHUDModel::RefreshPurchaseBroadcasts()
{
	const ACatfishingGameState* RunGameState = BoundRunGameState.Get();
	if (!RunGameState) return;
	const auto& Economy = RunGameState->GetShopEconomySnapshot();
	const double ServerNowSeconds = RunGameState->GetServerWorldTimeSeconds();
	if (!bHasPurchaseBroadcastSeedTime)
	{
		PurchaseBroadcastSeedServerTime = ServerNowSeconds;
		bHasPurchaseBroadcastSeedTime = true;
	}
	const bool bSeedOnly = ServerNowSeconds - PurchaseBroadcastSeedServerTime <= CatHUDPurchaseBroadcastSeedGraceSeconds;
	for (const auto& Cart : Economy.Transactions)
	{
		const FGuid CartId = Cart.CartId.IsValid() ? Cart.CartId : Cart.TransactionId;
		if ((!Cart.bPurchase && !Cart.bFishSale) || AnnouncedPurchaseCartIds.Contains(CartId)) continue;
		AnnouncedPurchaseCartIds.Add(CartId);
		if (bSeedOnly) continue;
		FCatHUDPurchaseBroadcast Broadcast;
		Broadcast.CartId = CartId;
		Broadcast.EntryCount = Cart.Items.Num();
		Broadcast.SpentCoins = FMath::Max(0, -Cart.WalletDelta);
		Broadcast.AnnouncedServerTime = ServerNowSeconds;
		Broadcast.BuyerNameText = Cart.ActorPlayerState ? FText::FromString(Cart.ActorPlayerState->GetPlayerName())
			: FText::FromString(TEXT("某只猫"));
		TArray<FString> Names;
		for (const auto& Item : Cart.Items)
		{
			Broadcast.ItemCount += Item.Quantity;
			Names.Add(FString::Printf(TEXT("%s ×%d"), *MakePurchaseItemNameText(Item.DefinitionId, NAME_None).ToString(), Item.Quantity));
		}
		// 旧复制结构或尚未迁移的 Blueprint 消费者保留首项字段；新服务总是提供完整 Items。
		if (Names.IsEmpty()) Names.Add(MakePurchaseItemNameText(Cart.DefinitionId, Cart.EntryId).ToString());
		Broadcast.ItemsText = FText::FromString(FString::Join(Names, TEXT("、")));
		Broadcast.BroadcastText = FText::FromString(Cart.bFishSale
			? FString::Printf(TEXT("%s 卖出 %s，公款收入 %d%s"), *Broadcast.BuyerNameText.ToString(), *Broadcast.ItemsText.ToString(),
				Cart.WalletDelta, Cart.bContainsGiantFish ? TEXT("，巨物进账！") : TEXT(""))
			: FString::Printf(TEXT("%s 买了 %s，花掉公款 %d"), *Broadcast.BuyerNameText.ToString(), *Broadcast.ItemsText.ToString(), Broadcast.SpentCoins));
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_hud_economy_broadcast World=%s NetMode=%d CartId=%s Player=%s Items=%d WalletDelta=%d Giant=%d"),
			*GetNameSafe(RunGameState->GetWorld()), RunGameState->GetNetMode(), *CartId.ToString(),
			*GetNameSafe(Cart.ActorPlayerState), Broadcast.ItemCount, Cart.WalletDelta, Cart.bContainsGiantFish);
		PurchaseBroadcasts.Add(MoveTemp(Broadcast));
	}
	if (PurchaseBroadcasts.Num() > CatHUDPurchaseBroadcastLimits::MaxKeptEntries)
		PurchaseBroadcasts.RemoveAt(0, PurchaseBroadcasts.Num() - CatHUDPurchaseBroadcastLimits::MaxKeptEntries);
}

// 全场购买广播收口流程：换 GameState、换 World 或解绑时清空本局播报记录；它不读也不写商店账本。
void UCatHUDModel::ResetPurchaseBroadcastState()
{
	AnnouncedPurchaseCartIds.Reset();
	PurchaseBroadcasts.Reset();
	PurchaseBroadcastSeedServerTime = 0.0;
	bHasPurchaseBroadcastSeedTime = false;
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
		// 普通抓握也刷新手部复制；不依赖钓鱼会话或命令回执。
		Refresh();
		return;
	}
	const FGuid PreviousSessionId = FishingViewBridge->GetViewState().FishingSessionId;
	if (Session)
	{
		FishingViewBridge->BindSession(Session);
	}
	else
	{
		FishingViewBridge->UnbindSession();
		Refresh();
	}
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_hud_fishing_session_binding World=%s NetMode=%d Authority=%d LocalRole=%d PlayerId=%d PreviousSessionId=%s SessionId=%s Result=%s"),
		*GetNameSafe(Controller ? Controller->GetWorld() : nullptr),
		Controller && Controller->GetWorld() ? static_cast<int32>(Controller->GetWorld()->GetNetMode()) : INDEX_NONE,
		Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : INDEX_NONE,
		PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE, *PreviousSessionId.ToString(),
		*FishingViewBridge->GetViewState().FishingSessionId.ToString(), Session ? TEXT("Bound") : TEXT("Unbound"));
}

void UCatHUDModel::ScheduleFishingSessionBindingReconcile()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	if (!World || (FishingSessionBindingReconcileWorld.Get() == World
		&& World->GetTimerManager().IsTimerActive(FishingSessionBindingReconcileTimerHandle))) return;
	ClearFishingSessionBindingReconcile();
	FishingSessionBindingReconcileWorld = World;
	World->GetTimerManager().SetTimer(FishingSessionBindingReconcileTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::RefreshFishingSessionBinding),
		CatHUDFishingSessionBindingReconcileSeconds, true);
}

void UCatHUDModel::ClearFishingSessionBindingReconcile()
{
	if (UWorld* World = FishingSessionBindingReconcileWorld.Get())
	{
		World->GetTimerManager().ClearTimer(FishingSessionBindingReconcileTimerHandle);
	}
	FishingSessionBindingReconcileTimerHandle.Invalidate();
	FishingSessionBindingReconcileWorld.Reset();
}
