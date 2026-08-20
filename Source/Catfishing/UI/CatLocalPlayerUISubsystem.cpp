#include "UI/CatLocalPlayerUISubsystem.h"

#include "AbilitySystemComponent.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "UI/CatCommandPanelWidget.h"
#include "UI/CatSurvivalWidget.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatFishTankActor.h"
#include "ShopEconomy/CatShopEconomySettings.h"

namespace
{
	// Online 解析流程：LocalPlayerSubsystem 可能在无窗口自动化或早期生命周期中没有 Viewport/GameInstance；这里统一把
	// 缺宿主收口为空，让 UI 保持 fail-closed 而不是解引用崩溃。
	UCatOnlineSubsystem* ResolveOnlineSubsystem(const ULocalPlayer* LocalPlayer)
	{
		UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	}
}
// 初始化流程：先订阅唯一 Online 快照与 GameInstance 占有变化广播（后者覆盖远端客户端，见 HandlePawnControllerChanged 声明）；
// 再弱绑定当前 Controller 的 Pawn notifier、装配现行 Character Survival 投影，最后保留阶段 B Online 白盒创建链。
void UCatLocalPlayerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UCatOnlineSubsystem* Online = ResolveOnlineSubsystem(GetLocalPlayer()))
	{
		OnlineSnapshotHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineSnapshotChanged);
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr)
	{
		GameInstance->GetOnPawnControllerChanged().AddUniqueDynamic(this, &ThisClass::HandlePawnControllerChanged);
	}
	BindController(LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr);
	RefreshOnlineWidgetForCurrentController();
}

// 销毁流程：先断开旧 Controller Pawn notifier，再从 ASC 移除属性 delegate 与 Survival View；随后移除 Online View、快照订阅
// 与 GameInstance 占有变化订阅，最后交还父类生命周期。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	UnbindController();
	DetachSurvivalPawn();
	RemoveOnlineWidget();
	if (UCatOnlineSubsystem* Online = ResolveOnlineSubsystem(GetLocalPlayer()))
	{
		Online->OnSnapshotChanged.Remove(OnlineSnapshotHandle);
	}
	OnlineSnapshotHandle.Reset();
	if (UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		GameInstance->GetOnPawnControllerChanged().RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
	}
	Super::Deinitialize();
}

// Controller 变化流程：先解绑旧 Controller notifier、旧 Pawn ASC delegate 与两类 View，再让父类消费新 Controller；最
// 后只弱绑定 NewController 并从其当前 Pawn 冷启动新投影。
void UCatLocalPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	UnbindController();
	DetachSurvivalPawn();
	RemoveOnlineWidget();
	Super::PlayerControllerChanged(NewController);
	BindController(NewController);
	RefreshOnlineWidgetForCurrentController();
}

// 快照消费流程：每次事实变化都重新调和 TravelWidget；直达 Lake 会移除联机白盒，正式 Session/旅行或返回 Frontend 时可按新快照重新创建并刷新。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshOnlineWidgetForCurrentController();
}

// 动作转交流程：每个 View 意图只调用 Online 的一个公开入口；opaque FGuid 只包装回原句柄类型，UI 不解析平台结果或自行补偿失败。
void UCatLocalPlayerUISubsystem::HandleActionRequested(const ECatOnlineUIAction Action, const FGuid OpaqueHandle)
{
	UCatOnlineSubsystem* Online = ResolveOnlineSubsystem(GetLocalPlayer());
	if (!Online)
	{
		return;
	}

	FCatOnlineResult Result;
	switch (Action)
	{
	case ECatOnlineUIAction::Host:
		Result = Online->RequestCreateSession();
		break;
	case ECatOnlineUIAction::Find:
		Result = Online->RequestFindSessions();
		break;
	case ECatOnlineUIAction::Join:
	{
		FCatSessionSearchHandle Handle;
		Handle.Value = OpaqueHandle;
		Result = Online->RequestJoinSession(Handle);
		break;
	}
	case ECatOnlineUIAction::AcceptInvite:
	{
		FCatSessionInviteHandle Handle;
		Handle.Value = OpaqueHandle;
		Result = Online->RequestAcceptInvite(Handle);
		break;
	}
	case ECatOnlineUIAction::Leave:
		Result = Online->RequestLeave();
		break;
	default:
		return;
	}

	const UWorld* World = GetWorld();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_action Action=%s RequestId=%s World=%s NetMode=%d Result=%s Error=%s"),
		*UEnum::GetValueAsString(Action),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		Result.bAccepted ? TEXT("accepted") : TEXT("rejected"),
		*UEnum::GetValueAsString(Result.Error));
	if (OnlineWidget)
	{
		OnlineWidget->Configure(Online->GetSnapshot());
	}
}

// Widget 调和流程：
// 1. 读取当前 LocalPlayer Controller 与 Online 完整快照；任一宿主不可用时移除旧 View，等待生命周期再次触发。
// 2. 快照精确表示 Lake、NoSession、无角色且无活动操作时移除 TravelWidget，让直达玩法地图的 PIE 不再被联机白盒遮挡。
// 3. 其余状态沿用正式 UI：缺实例时创建并绑定一次动作委托，随后用同一快照配置；正式 Lake Host/Client 因有会话角色仍保留 Leave。
void UCatLocalPlayerUISubsystem::RefreshOnlineWidgetForCurrentController()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	const UCatOnlineSubsystem* Online = ResolveOnlineSubsystem(LocalPlayer);
	if (!Controller || !Online)
	{
		RemoveOnlineWidget();
		return;
	}

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	const bool bDirectLakeWithoutSession = Snapshot.WorldState == ECatOnlineWorldState::Lake
		&& Snapshot.SessionState == ECatOnlineSessionState::NoSession
		&& Snapshot.SessionRole == ECatOnlineSessionRole::None
		&& Snapshot.ActiveOperation == ECatOnlineOperation::None;
	if (bDirectLakeWithoutSession)
	{
		RemoveOnlineWidget();
		return;
	}

	if (!OnlineWidget)
	{
		OnlineWidget = CreateWidget<UCatTravelWidget>(Controller, UCatTravelWidget::StaticClass());
		if (!OnlineWidget)
		{
			return;
		}
		ActionHandle = OnlineWidget->OnActionRequested.AddUObject(this, &ThisClass::HandleActionRequested);
		OnlineWidget->AddToViewport();
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_widget_created World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
	}
	OnlineWidget->Configure(Snapshot);
}

// Widget 移除流程：存在实例时先解绑动作广播，再移出视口并清 UObject 引用；空分支不制造虚假 removed 日志。
void UCatLocalPlayerUISubsystem::RemoveOnlineWidget()
{
	if (!OnlineWidget)
	{
		return;
	}
	OnlineWidget->OnActionRequested.Remove(ActionHandle);
	ActionHandle.Reset();
	OnlineWidget->RemoveFromParent();
	OnlineWidget = nullptr;
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_widget_removed World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
}

// Controller 绑定流程：保存弱引用并在真实 FPawnChangedSignature 上绑定一参数 NewPawn 回调；随后直接消费当前 Pawn，覆
// 盖绑定发生前已经完成占有的冷启动情况。
void UCatLocalPlayerUISubsystem::BindController(APlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}
	BoundPlayerController = Controller;
	PawnChangedHandle = Controller->GetOnNewPawnNotifier().AddUObject(this, &ThisClass::HandleControllerPawnChanged);
	AttachSurvivalPawn(Cast<ACatCharacter>(Controller->GetPawn()));
}

// Controller 解绑流程：旧对象仍存活时从同一个 notifier 精确 Remove；若 World 已销毁，弱引用失效后只 Reset 本地句柄，不延长 Controller 生命周期。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	if (APlayerController* Controller = BoundPlayerController.Get(); Controller && PawnChangedHandle.IsValid())
	{
		Controller->GetOnNewPawnNotifier().Remove(PawnChangedHandle);
	}
	PawnChangedHandle.Reset();
	BoundPlayerController.Reset();
}

// Pawn 变化流程：无论新 Pawn 类型如何都先解绑旧 ASC 与 View；只有现行 Pawn 是 ACatCharacter 时才进入 Survival 装配，
// Frontend 的空 Pawn 自然保持无玩法 View。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	DetachSurvivalPawn();
	AttachSurvivalPawn(Cast<ACatCharacter>(NewPawn));
}

// GameInstance 占有变化流程：
// 1. 只关心与本 LocalPlayer 的 Controller 相关的变化。变化的 Controller 不是本机的：仅当该 Pawn 正是当前已装配的身体
//    （它的 ASC 就是 BoundSurvivalASC）时拆掉 View——这是"本机身体被解除占有/改配给别人"的复制形态；其它玩家的占有变化
//    与本机 HUD 无关，直接忽略。
// 2. 变化指向本机 Controller 且该身体已装配：幂等短路。监听服务器上本机占有会先走 notifier 装配一次，随后这条
//    GameInstance 广播再到，不能重复建 View。
// 3. 其余情况按"本机换了身体"处理：先完整拆旧再装新。AttachSurvivalPawn 自带 Controller->GetPawn()==Character 校验，
//    在客户端可用——引擎在广播前已通过 OnRep_Controller 把 Pawn 与 Controller 互指补齐。
void UCatLocalPlayerUISubsystem::HandlePawnControllerChanged(APawn* Pawn, AController* NewController)
{
	APlayerController* OwnController = BoundPlayerController.Get();
	ACatCharacter* Character = Cast<ACatCharacter>(Pawn);
	const bool bPawnIsBoundBody = Character && BoundSurvivalASC.IsValid()
		&& Character->GetAbilitySystemComponent() == BoundSurvivalASC.Get();
	if (!OwnController || NewController != OwnController)
	{
		if (bPawnIsBoundBody)
		{
			DetachSurvivalPawn();
		}
		return;
	}
	if (bPawnIsBoundBody)
	{
		return;
	}
	DetachSurvivalPawn();
	AttachSurvivalPawn(Character);
}

// Survival 装配流程：先校验 View gate、World 和当前 Pawn/ASC，创建纯 Render Widget 后才保存弱引用；随后成对订阅三属
// 性、Condition、Equipment 及 GameState 的 Run/Help，最后统一重读首份完整投影。
void UCatLocalPlayerUISubsystem::AttachSurvivalPawn(ACatCharacter* Character)
{
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (!Settings->IsLakeStatusViewEnabled() || !Character || Character->GetWorld() != GetWorld())
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller || Controller->GetPawn() != Character)
	{
		return;
	}
	UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return;
	}

	SurvivalWidget = CreateWidget<UCatSurvivalWidget>(Controller, UCatSurvivalWidget::StaticClass());
	if (!SurvivalWidget)
	{
		return;
	}
	BoundSurvivalASC = AbilitySystem;
	PoisonChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute())
		.AddUObject(this, &ThisClass::HandleSurvivalAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute())
		.AddUObject(this, &ThisClass::HandleSurvivalAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute())
		.AddUObject(this, &ThisClass::HandleSurvivalAttributeChanged);
	BoundCondition = Character->GetConditionComponent();
	BoundEquipment = Character->GetEquipmentComponent();
	BoundGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		ConditionChangedHandle = Conditions->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleGameplaySnapshotChanged);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		EquipmentChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleGameplaySnapshotChanged);
	}
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		RunChangedHandle = GameState->OnRunPublicStateChanged.AddUObject(this, &ThisClass::HandleGameplaySnapshotChanged);
		HelpChangedHandle = GameState->OnHelpSignalChanged.AddUObject(this, &ThisClass::HandleGameplaySnapshotChanged);
	}
	SurvivalWidget->AddToViewport(1);
	if (Settings->IsCommandPanelEnabled())
	{
		CommandPanel = CreateWidget<UCatCommandPanelWidget>(Controller, UCatCommandPanelWidget::StaticClass());
		if (CommandPanel)
		{
			CommandPanelActionHandle = CommandPanel->OnActionRequested.AddUObject(this, &ThisClass::HandleCommandPanelAction);
			CommandPanel->AddToViewport(2);
			// 面板要能用鼠标点，所以把输入模式切成 GameAndUI：光标常显、不锁在视口里；按住鼠标键在视口里拖动时引擎临时捕获鼠标，Look 仍然有效；
			// 键盘焦点一开始在视口，WASD 不受影响（点按钮后焦点会被 Slate 移到按钮上，HandleCommandPanelAction 负责还
			// 回去）。Detach 时成对切回 GameOnly。
			Controller->SetInputMode(FInputModeGameAndUI()
				.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)
				.SetHideCursorDuringCapture(true));
			Controller->SetShowMouseCursor(true);
			UE_LOG(LogCatUI, Log, TEXT("Event=ui_command_panel_created World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
		}
	}
	RefreshSurvivalView();
}

// Survival 解绑流程：先用相同 ASC/属性键移除三个 delegate，再从 Condition、Equipment 与 GameState 移除全部快照通知；
// 然后清句柄/弱引用并最后移出 View，阻断旧 World 迟到事件。
void UCatLocalPlayerUISubsystem::DetachSurvivalPawn()
{
	if (UAbilitySystemComponent* AbilitySystem = BoundSurvivalASC.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute()).Remove(PoisonChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		Conditions->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnSnapshotChanged.Remove(EquipmentChangedHandle);
	}
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		GameState->OnRunPublicStateChanged.Remove(RunChangedHandle);
		GameState->OnHelpSignalChanged.Remove(HelpChangedHandle);
	}
	PoisonChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	EquipmentChangedHandle.Reset();
	RunChangedHandle.Reset();
	HelpChangedHandle.Reset();
	BoundSurvivalASC.Reset();
	BoundCondition.Reset();
	BoundEquipment.Reset();
	BoundGameState.Reset();
	if (CommandPanel)
	{
		CommandPanel->OnActionRequested.Remove(CommandPanelActionHandle);
		CommandPanelActionHandle.Reset();
		CommandPanel->RemoveFromParent();
		CommandPanel = nullptr;
		LastCommandPanelFeedback.Reset();
		// 只有 Controller 还活着且仍被本子系统持有时才切回 GameOnly；PlayerControllerChanged/Deinitialize 路径在进来前已经 UnbindController，
		// 那时弱引用为空，旧 Controller 正在被替换或销毁，输入模式随它一起作废，不需要也无法恢复。
		if (APlayerController* Controller = BoundPlayerController.Get())
		{
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetShowMouseCursor(false);
		}
		// 日志串末尾的两个空格是故意留的：本机 Smart App Control 按二进制特征裁决新 DLL 能否加载（见 .harness/CODING_LESSONS.md），
		// 这份源码编出的 DLL 只有这个字面量形态通过了加载检查；改动它会换一个二进制哈希、需要重新碰运气，不影响任何行为。
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_command_panel_removed World=%s  "), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
	}
	if (SurvivalWidget)
	{
		SurvivalWidget->RemoveFromParent();
		SurvivalWidget = nullptr;
	}
}

// 属性变化流程：事件只是“Model 已变”信号，不用单项 NewValue 拼 UI；忽略载荷后重读三属性与 Condition/Equipment/Run/Help 的同一完整投影。
void UCatLocalPlayerUISubsystem::HandleSurvivalAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	RefreshSurvivalView();
}

// 玩法快照变化流程：无视具体来源与增量载荷，统一从当前 ASC/组件/GameState 重建整份 ViewState，避免多源事件顺序形成 UI 私有真相。
void UCatLocalPlayerUISubsystem::HandleGameplaySnapshotChanged()
{
	RefreshSurvivalView();
}

// ViewState 刷新流程：现取 Controller/Pawn 并验证 World/ASC 仍属同一绑定；然后读三属性，按弱引用可用性补入
// Condition、Equipment、Run 与 Help，最后只把局部 DTO 交给 Widget::Render。
void UCatLocalPlayerUISubsystem::RefreshSurvivalView()
{
	UAbilitySystemComponent* AbilitySystem = BoundSurvivalASC.Get();
	const APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	if (!SurvivalWidget || !Character || !AbilitySystem || Character->GetWorld() != GetWorld()
		|| Character->GetAbilitySystemComponent() != AbilitySystem)
	{
		return;
	}
	FCatSurvivalViewState ViewState;
	ViewState.Poison = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
	ViewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	ViewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (const UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		ViewState.Condition = Conditions->GetSnapshot();
	}
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		ViewState.Equipment = Equipment->GetSnapshot();
	}
	if (const ACatfishingGameState* GameState = BoundGameState.Get())
	{
		ViewState.Run = GameState->GetRunPublicState();
		ViewState.HelpSignal = GameState->GetLastHelpSignal();
	}
	SurvivalWidget->Render(ViewState);

	if (CommandPanel)
	{
		const FCatCommandPanelDispatchInput Input = GatherCommandPanelInput();
		FCatCommandPanelViewState PanelState;
		PanelState.Phase = Input.Run.Phase.Phase;
		PanelState.EndReason = Input.Run.EndReason;
		PanelState.RunRevision = Input.Run.Revision;
		PanelState.bFishingAllowed = Input.Run.Phase.bFishingAllowed;
		PanelState.bQuotaOpen = Input.Run.Phase.bQuotaOpen;
		PanelState.bDowned = Input.bDowned;
		PanelState.bWet = ViewState.Condition.bWet;
		PanelState.GuardFishCount = Input.Guard.Fish.Num();
		PanelState.bHasCamp = Input.Camp != nullptr;
		PanelState.bHasActiveFishingSession = Input.FishingSession != nullptr;
		PanelState.bHasRescueTarget = Input.RescueTarget != nullptr;
		PanelState.bHasTeamEquipment = Input.FirstTeamEquipmentInstanceId.IsValid();
		PanelState.bHasChum = !Input.FirstChumDefinitionId.IsNone();
		if (Input.FishingSession)
		{
			const FCatFishingSessionSnapshot& Fishing = Input.FishingSession->GetSnapshot();
			PanelState.FishingSessionLine = FString::Printf(
				TEXT("Fishing: %s  D=%.1fm L=%.1f/%.0fm  Fish=%s%s  FishStamina=%.1f/%.1f  Intent=%s  Outcome=%s  Perfect=%s"),
				*UEnum::GetValueAsString(Fishing.Phase), Fishing.FishDistanceMeters, Fishing.LineLengthMeters, Fishing.LineLengthMaxMeters,
				*UEnum::GetValueAsString(Fishing.FishSwimState), Fishing.bFishDeathStruggle ? TEXT("(STRUGGLE!)") : TEXT(""),
				Fishing.FishFightStaminaRemaining, Fishing.FishFightStaminaMax,
				*UEnum::GetValueAsString(Fishing.FisherIntent), *UEnum::GetValueAsString(Fishing.FightOutcome),
				Fishing.bPerfectHook ? TEXT("yes") : TEXT("no"));
		}
		PanelState.LastFeedback = LastCommandPanelFeedback;
		CommandPanel->Configure(PanelState);
	}
}

// 面板点击流程：现取一份分派输入（Controller、宿主、各快照版本都在此刻读），交给分派表发 RPC；反馈文本记到成员并写
// LogCatUI，最后整份重刷 View，让面板底部立刻显示这次发了什么。
// 服务器接受与否不在这里判断——RPC 无返回值，结果只能从服务器日志（LogCatRun/LogCatFishing/LogCatItems 等的 Event= 行）或随后复制回来的快照变化看出。
void UCatLocalPlayerUISubsystem::HandleCommandPanelAction(const ECatCommandPanelAction Action)
{
	const FCatCommandPanelDispatchInput Input = GatherCommandPanelInput();
	if (!Input.Controller)
	{
		LastCommandPanelFeedback = TEXT("no ACatfishingPlayerController bound; nothing sent");
	}
	else
	{
		LastCommandPanelFeedback = DispatchCommandPanelAction(Action, Input);
	}
	const UWorld* World = GetWorld();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_command_panel_action Action=%s World=%s NetMode=%d Feedback=%s"),
		*UEnum::GetValueAsString(Action),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		*LastCommandPanelFeedback);
	// Slate 在鼠标按下时会把键盘焦点移到被点的按钮上（SButton 默认 SupportsKeyboardFocus），之后 WASD 就进不了游戏视口；
	// 这里处理完点击就重新应用同一份 GameAndUI 输入模式——FInputModeGameAndUI::ApplyInputMode 在没有指定 WidgetToFocus 时会把焦点设回游戏视口。
	if (Input.Controller)
	{
		Input.Controller->SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)
			.SetHideCursorDuringCapture(true));
	}
	RefreshSurvivalView();
}

// 采集流程：Controller/Character 来自已绑定弱引用；Run/钱包版本与团队装备库读 GameState，倒地读 Condition，装备版本和
// 第一种窝料读 Equipment，鱼护读 Character 身上唯一的容器复制组件；窝点版本只在本机有窝点子系统（即本机是服务器）时读
// 得到；
// 营地、鱼缸、非终态钓鱼会话、倒地角色都取当前 World 里的第一个——这些 Actor 都 bReplicates，客户端能看到；Lake 当前
// 各只有一个，白盒阶段不做更精确的目标选择。
// 营地和鱼缸是关卡摆放的稳定目标，找到后记进弱引用，失效或跨 World 才重扫；钓鱼会话和倒地角色是动态的，而且同时存在多个
// 时"第一个"具体是谁会影响面板显示和命令目标，所以它们仍然每次重扫，不缓存。
FCatCommandPanelDispatchInput UCatLocalPlayerUISubsystem::GatherCommandPanelInput()
{
	FCatCommandPanelDispatchInput Input;
	Input.Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	Input.Character = Input.Controller ? Cast<ACatCharacter>(Input.Controller->GetPawn()) : nullptr;
	if (const ACatfishingGameState* GameState = BoundGameState.Get())
	{
		Input.Run = GameState->GetRunPublicState();
		Input.WalletRevision = GameState->GetShopEconomySnapshot().WalletRevision;
		const FCatTeamEquipmentLibrarySnapshot& Library = GameState->GetTeamEquipmentLibrary();
		Input.TeamLibraryRevision = Library.Revision;
		Input.FirstTeamEquipmentInstanceId = Library.Instances.IsEmpty() ? FGuid() : Library.Instances[0].InstanceId;
	}
	if (const UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		Input.bDowned = Conditions->GetSnapshot().bDowned;
	}
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Input.EquipmentRevision = Equipment->GetSnapshot().Revision;
		const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
		for (const FCatRunConsumableStack& Stack : Equipment->GetSnapshot().Consumables)
		{
			const UCatEquipmentDefinition* Definition = EquipmentSettings->FindRuntimeDefinition(Stack.DefinitionId);
			if (Stack.Quantity > 0 && Definition && Definition->Kind == ECatEquipmentKind::Chum)
			{
				Input.FirstChumDefinitionId = Stack.DefinitionId;
				break;
			}
		}
	}
	if (const UCatContainerReplicationComponent* Guard = Input.Character
		? Input.Character->FindComponentByClass<UCatContainerReplicationComponent>() : nullptr)
	{
		Input.Guard = Guard->GetSnapshot();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return Input;
	}
	if (const UCatChumSpotSubsystem* ChumSpots = World->GetSubsystem<UCatChumSpotSubsystem>())
	{
		Input.ChumRevision = ChumSpots->GetAggregationRevision();
	}
	ACatCampHubActor* Camp = CachedCamp.Get();
	if (!Camp || Camp->GetWorld() != World)
	{
		Camp = nullptr;
		for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
		{
			Camp = *It;
			break;
		}
		CachedCamp = Camp;
	}
	Input.Camp = Camp;
	ACatFishTankActor* FishTank = CachedFishTank.Get();
	if (!FishTank || FishTank->GetWorld() != World)
	{
		FishTank = nullptr;
		for (TActorIterator<ACatFishTankActor> It(World); It; ++It)
		{
			FishTank = *It;
			break;
		}
		CachedFishTank = FishTank;
	}
	if (const UCatContainerReplicationComponent* Tank = FishTank
		? FishTank->FindComponentByClass<UCatContainerReplicationComponent>() : nullptr)
	{
		Input.TankRevision = Tank->GetSnapshot().Revision;
	}
	for (TActorIterator<ACatFishingSession> It(World); It; ++It)
	{
		const ECatFishingPhase Phase = It->GetSnapshot().Phase;
		if (Phase != ECatFishingPhase::Resolved && Phase != ECatFishingPhase::Terminated)
		{
			Input.FishingSession = *It;
			break;
		}
	}
	for (TActorIterator<ACatCharacter> It(World); It; ++It)
	{
		const UCatConditionComponent* Conditions = It->GetConditionComponent();
		if (Conditions && Conditions->GetSnapshot().bDowned)
		{
			Input.RescueTarget = *It;
			break;
		}
	}
	return Input;
}

// 分派表流程：每个 case 先检查自己需要的目标/参数是否齐（不齐返回“skipped: 原因”，不发送），再在 Controller 非空时调
// 用对应 Server RPC，最后返回带参数的一行反馈。
// RequestId 每次点击新生成一个；各 ExpectedRevision 直接用 Input 里点击瞬间读到的版本。Controller 为空只发生在测试里，此时只描述不发送。
FString UCatLocalPlayerUISubsystem::DispatchCommandPanelAction(const ECatCommandPanelAction Action, const FCatCommandPanelDispatchInput& Input)
{
	ACatfishingPlayerController* PC = Input.Controller;
	const FGuid RequestId = FGuid::NewGuid();
	const FString RequestText = RequestId.ToString(EGuidFormats::DigitsWithHyphens);
	const TCHAR* Verb = PC ? TEXT("sent") : TEXT("described-only");
	const FGuid FirstFishId = Input.Guard.Fish.IsEmpty() ? FGuid() : Input.Guard.Fish[0].FishInstanceId;
	const FString FirstFishText = FirstFishId.ToString(EGuidFormats::DigitsWithHyphens);

	switch (Action)
	{
	case ECatCommandPanelAction::RunReady:
	case ECatCommandPanelAction::RunUnready:
	{
		const bool bReady = Action == ECatCommandPanelAction::RunReady;
		if (PC)
		{
			PC->ServerSetNextDayReady(RequestId, Input.Run.Revision, bReady);
		}
		return FString::Printf(TEXT("ServerSetNextDayReady %s bReady=%s ExpectedRevision=%lld RequestId=%s"),
			Verb, bReady ? TEXT("true") : TEXT("false"), Input.Run.Revision, *RequestText);
	}
	case ECatCommandPanelAction::SettlementComplete:
		if (PC)
		{
			PC->ServerRequestSettlementCompletion(RequestId, Input.Run.Revision);
		}
		return FString::Printf(TEXT("ServerRequestSettlementCompletion %s ExpectedRevision=%lld RequestId=%s"),
			Verb, Input.Run.Revision, *RequestText);
	case ECatCommandPanelAction::StartFishing:
		if (PC)
		{
			PC->ServerStartFishingSession(RequestId);
		}
		return FString::Printf(TEXT("ServerStartFishingSession %s RequestId=%s"), Verb, *RequestText);
	case ECatCommandPanelAction::Scoop:
	{
		if (!Input.FishingSession)
		{
			return TEXT("ServerRequestScoop skipped: no non-terminal ACatFishingSession in world");
		}
		const FCatFishingSessionSnapshot& Session = Input.FishingSession->GetSnapshot();
		FCatScoopCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = Session.Revision;
		Command.TargetGuardContainerId = Input.Guard.ContainerId;
		Command.ScoopWorldLocation = Input.Character ? Input.Character->GetActorLocation() : FVector::ZeroVector;
		if (PC)
		{
			PC->ServerRequestScoop(Session.FishingSessionId, Command);
		}
		return FString::Printf(TEXT("ServerRequestScoop %s Session=%s Phase=%s ExpectedRevision=%lld RequestId=%s"),
			Verb, *Session.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Session.Phase), Session.Revision, *RequestText);
	}
	case ECatCommandPanelAction::CampRest:
		if (!Input.Camp)
		{
			return TEXT("ServerRequestCampRest skipped: no ACatCampHubActor in world");
		}
		if (PC)
		{
			PC->ServerRequestCampRest(Input.Camp, RequestId);
		}
		return FString::Printf(TEXT("ServerRequestCampRest %s Camp=%s RequestId=%s"), Verb, *Input.Camp->GetName(), *RequestText);
	case ECatCommandPanelAction::TransferFirstFishToTank:
		if (!Input.Camp)
		{
			return TEXT("ServerTransferFishToTank skipped: no ACatCampHubActor in world");
		}
		if (!FirstFishId.IsValid())
		{
			return TEXT("ServerTransferFishToTank skipped: personal fish guard is empty");
		}
		if (PC)
		{
			PC->ServerTransferFishToTank(Input.Camp, RequestId, FirstFishId, Input.Guard.Revision, Input.TankRevision);
		}
		return FString::Printf(TEXT("ServerTransferFishToTank %s Fish=%s GuardRevision=%lld TankRevision=%lld RequestId=%s"),
			Verb, *FirstFishText, Input.Guard.Revision, Input.TankRevision, *RequestText);
	case ECatCommandPanelAction::CampfirePlayback:
		if (!Input.Camp)
		{
			return TEXT("ServerRequestCampfirePlayback skipped: no ACatCampHubActor in world");
		}
		if (PC)
		{
			PC->ServerRequestCampfirePlayback(Input.Camp, RequestId);
		}
		return FString::Printf(TEXT("ServerRequestCampfirePlayback %s Camp=%s RequestId=%s"), Verb, *Input.Camp->GetName(), *RequestText);
	case ECatCommandPanelAction::RescueDownedToCamp:
		if (!Input.Camp)
		{
			return TEXT("ServerRescueCharacterToCamp skipped: no ACatCampHubActor in world");
		}
		if (!Input.RescueTarget)
		{
			return TEXT("ServerRescueCharacterToCamp skipped: no downed ACatCharacter in world");
		}
		if (PC)
		{
			PC->ServerRescueCharacterToCamp(Input.Camp, Input.RescueTarget, RequestId);
		}
		return FString::Printf(TEXT("ServerRescueCharacterToCamp %s Target=%s RequestId=%s"), Verb, *Input.RescueTarget->GetName(), *RequestText);
	case ECatCommandPanelAction::SacrificeFirstFish:
	{
		if (!FirstFishId.IsValid())
		{
			return TEXT("ServerRequestSacrifice skipped: personal fish guard is empty");
		}
		FCatSacrificeCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = Input.Guard.Revision;
		Command.FishInstanceId = FirstFishId;
		Command.ContainerId = Input.Guard.ContainerId;
		Command.ExpectedRunRevision = Input.Run.Revision;
		if (PC)
		{
			PC->ServerRequestSacrifice(Command);
		}
		return FString::Printf(TEXT("ServerRequestSacrifice %s Fish=%s GuardRevision=%lld RunRevision=%lld RequestId=%s"),
			Verb, *FirstFishText, Input.Guard.Revision, Input.Run.Revision, *RequestText);
	}
	case ECatCommandPanelAction::ShopBuyFirstPaidEntry:
	{
		const FCatShopCatalogEntry* PaidEntry = GetDefault<UCatShopEconomySettings>()->CatalogEntries.FindByPredicate(
			[](const FCatShopCatalogEntry& Entry)
			{
				return Entry.Kind == ECatShopEntryKind::EquipmentGrant && Entry.UnitPrice > 0;
			});
		if (!PaidEntry)
		{
			return TEXT("ServerSubmitShopPurchase skipped: no paid EquipmentGrant entry in UCatShopEconomySettings catalog");
		}
		if (PC)
		{
			PC->ServerSubmitShopPurchase(PaidEntry->EntryId, RequestId, Input.WalletRevision);
		}
		return FString::Printf(TEXT("ServerSubmitShopPurchase %s Entry=%s Price=%d WalletRevision=%lld RequestId=%s"),
			Verb, *PaidEntry->EntryId.ToString(), PaidEntry->UnitPrice, Input.WalletRevision, *RequestText);
	}
	case ECatCommandPanelAction::ShopClaimFreeBait:
	{
		const FName EntryId = GetDefault<UCatShopEconomySettings>()->FreeOrdinaryBaitEntryId;
		if (EntryId.IsNone())
		{
			return TEXT("ServerClaimFreeShopEntry skipped: FreeOrdinaryBaitEntryId not configured");
		}
		if (PC)
		{
			PC->ServerClaimFreeShopEntry(EntryId, RequestId, Input.WalletRevision);
		}
		return FString::Printf(TEXT("ServerClaimFreeShopEntry %s Entry=%s WalletRevision=%lld RequestId=%s"),
			Verb, *EntryId.ToString(), Input.WalletRevision, *RequestText);
	}
	case ECatCommandPanelAction::SellFirstFish:
		if (!FirstFishId.IsValid())
		{
			return TEXT("ServerSellFish skipped: personal fish guard is empty");
		}
		if (PC)
		{
			PC->ServerSellFish(FirstFishId, Input.Guard.ContainerId, Input.Guard.Revision,
				ECatShopFishSaleSource::PersonalGuard, RequestId, Input.WalletRevision);
		}
		return FString::Printf(TEXT("ServerSellFish %s Fish=%s GuardRevision=%lld WalletRevision=%lld RequestId=%s"),
			Verb, *FirstFishText, Input.Guard.Revision, Input.WalletRevision, *RequestText);
	case ECatCommandPanelAction::ManualHelp:
	{
		const ECatHelpSignalKind Kind = Input.bDowned ? ECatHelpSignalKind::ManualDowned : ECatHelpSignalKind::ManualFishing;
		if (PC)
		{
			PC->ServerRequestManualHelp(RequestId, Kind);
		}
		return FString::Printf(TEXT("ServerRequestManualHelp %s Kind=%s RequestId=%s"), Verb, *UEnum::GetValueAsString(Kind), *RequestText);
	}
	case ECatCommandPanelAction::ShakeDry:
		if (PC)
		{
			PC->ServerCompleteShakeDry(RequestId);
		}
		return FString::Printf(TEXT("ServerCompleteShakeDry %s RequestId=%s"), Verb, *RequestText);
	case ECatCommandPanelAction::FieldSelfRecovery:
		if (PC)
		{
			PC->ServerRequestFieldSelfRecovery(RequestId);
		}
		return FString::Printf(TEXT("ServerRequestFieldSelfRecovery %s RequestId=%s"), Verb, *RequestText);
	case ECatCommandPanelAction::ConfigureStarterEquipment:
	{
		FName Rod, Bait, Float;
		if (!GetDefault<UCatEquipmentSettings>()->TryGetStarterLoadout(Rod, Bait, Float))
		{
			return TEXT("ServerConfigureEquipment skipped: starter loadout not configured in UCatEquipmentSettings");
		}
		if (PC)
		{
			PC->ServerConfigureEquipment(RequestId, Input.EquipmentRevision, Rod, Bait, Float);
		}
		return FString::Printf(TEXT("ServerConfigureEquipment %s Rod=%s Bait=%s Float=%s ExpectedRevision=%lld RequestId=%s"),
			Verb, *Rod.ToString(), *Bait.ToString(), *Float.ToString(), Input.EquipmentRevision, *RequestText);
	}
	case ECatCommandPanelAction::ShopBuyFirstChum:
	{
		// 按目录条目找"第一条指向 Chum 定义的付费耗材"；定义类别要查运行目录，目录条目自己不带类别。
		const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
		const FCatShopCatalogEntry* ChumEntry = GetDefault<UCatShopEconomySettings>()->CatalogEntries.FindByPredicate(
			[EquipmentSettings](const FCatShopCatalogEntry& Entry)
			{
				const UCatEquipmentDefinition* Definition = Entry.Kind == ECatShopEntryKind::RunConsumableGrant
					? EquipmentSettings->FindRuntimeDefinition(Entry.DefinitionId) : nullptr;
				return Definition && Definition->Kind == ECatEquipmentKind::Chum;
			});
		if (!ChumEntry)
		{
			return TEXT("ServerSubmitShopPurchase skipped: no RunConsumableGrant chum entry in UCatShopEconomySettings catalog");
		}
		if (PC)
		{
			PC->ServerSubmitShopPurchase(ChumEntry->EntryId, RequestId, Input.WalletRevision);
		}
		return FString::Printf(TEXT("ServerSubmitShopPurchase %s Entry=%s Price=%d WalletRevision=%lld RequestId=%s"),
			Verb, *ChumEntry->EntryId.ToString(), ChumEntry->UnitPrice, Input.WalletRevision, *RequestText);
	}
	case ECatCommandPanelAction::TakeFirstTeamEquipment:
		if (!Input.FirstTeamEquipmentInstanceId.IsValid())
		{
			return TEXT("ServerTakeTeamEquipment skipped: team equipment library is empty");
		}
		if (PC)
		{
			PC->ServerTakeTeamEquipment(Input.FirstTeamEquipmentInstanceId, RequestId, Input.TeamLibraryRevision,
				Input.EquipmentRevision);
		}
		return FString::Printf(TEXT("ServerTakeTeamEquipment %s Instance=%s LibraryRevision=%lld EquipmentRevision=%lld RequestId=%s"),
			Verb, *Input.FirstTeamEquipmentInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Input.TeamLibraryRevision,
			Input.EquipmentRevision, *RequestText);
	case ECatCommandPanelAction::ContributeChum:
	{
		if (Input.FirstChumDefinitionId.IsNone())
		{
			return TEXT("ServerContributeChum skipped: no chum in personal consumable stacks");
		}
		const FVector DropLocation = Input.Character ? Input.Character->GetActorLocation() : FVector::ZeroVector;
		if (PC)
		{
			PC->ServerContributeChum(DropLocation, RequestId, Input.EquipmentRevision, Input.ChumRevision,
				Input.FirstChumDefinitionId);
		}
		return FString::Printf(TEXT("ServerContributeChum %s Chum=%s Drop=%s EquipmentRevision=%lld ChumRevision=%lld RequestId=%s"),
			Verb, *Input.FirstChumDefinitionId.ToString(), *DropLocation.ToCompactString(), Input.EquipmentRevision,
			Input.ChumRevision, *RequestText);
	}
	case ECatCommandPanelAction::FightPull:
	case ECatCommandPanelAction::FightRelease:
	case ECatCommandPanelAction::FightNeutral:
	{
		// 遛鱼意图没有 RequestId 也不指定会话：服务器按本人身份找活跃会话，这里只负责把"现在按的是哪个"报上去。
		const ECatFishingFightIntent Intent = Action == ECatCommandPanelAction::FightPull ? ECatFishingFightIntent::Pull
			: Action == ECatCommandPanelAction::FightRelease ? ECatFishingFightIntent::Release : ECatFishingFightIntent::None;
		if (PC)
		{
			PC->ServerSetFishingFightIntent(Intent);
		}
		return FString::Printf(TEXT("ServerSetFishingFightIntent %s Intent=%s"), Verb, *UEnum::GetValueAsString(Intent));
	}
	}
	return FString();
}
