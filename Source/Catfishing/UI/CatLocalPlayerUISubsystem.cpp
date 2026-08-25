#include "UI/CatLocalPlayerUISubsystem.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Profile/CatProfileSubsystem.h"
#include "TimerManager.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/CatLakeReachWidget.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"

// 初始化流程：先订阅唯一 Online 快照，再弱绑定当前 Controller 的 Pawn notifier，并仅在显式 gate 开启时尝试装配 LakeReach 根，最后调和 Frontend Travel View。
void UCatLocalPlayerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UCatOnlineSubsystem* Online = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>())
	{
		OnlineSnapshotHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineSnapshotChanged);
	}
	BindController(GetLocalPlayer()->GetPlayerController(GetWorld()));
	RefreshOnlineWidgetForCurrentController();
}

// 销毁流程：先在旧 Controller 仍可用时恢复菜单输入并解绑 Lake 根，再断开 Pawn notifier；随后移除 Online View/订阅，最后交还父类生命周期。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	if (UCatOnlineSubsystem* Online = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>())
	{
		Online->OnSnapshotChanged.Remove(OnlineSnapshotHandle);
	}
	OnlineSnapshotHandle.Reset();
	Super::Deinitialize();
}

// Controller 变化流程：先用旧 Controller 完成菜单恢复和全部 Lake 解绑，再移除旧 notifier；父类切换完成后只弱绑定 NewController，并按显式 gate 从其当前 Pawn 冷启动 Lake 根与 Online View。
void UCatLocalPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	Super::PlayerControllerChanged(NewController);
	BindController(NewController);
	RefreshOnlineWidgetForCurrentController();
}

// 菜单切换流程：先要求当前根 View 与 Controller 仍属于同一 LocalPlayer；打开时保存鼠标状态，随后写唯一菜单状态、应用成对 InputMode 并重建完整 View。
void UCatLocalPlayerUISubsystem::ToggleLakeMenu()
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (!LakeReachWidget || !Controller || Controller != GetLocalPlayer()->GetPlayerController(GetWorld()))
	{
		return;
	}
	const bool bOpen = !bLakeMenuOpen;
	if (bOpen)
	{
		bPreviousMouseCursorVisible = Controller->bShowMouseCursor;
	}
	bLakeMenuOpen = bOpen;
	ApplyLakeMenuInputMode(bLakeMenuOpen);
	RefreshLakeView();
}

// 状态查询流程：直接返回协调器维护的唯一布尔值；不读取 Widget 可见性、焦点或 Controller 鼠标来拼出第二份状态。
bool UCatLocalPlayerUISubsystem::IsLakeMenuOpen() const
{
	return bLakeMenuOpen;
}

// 快照消费流程：每次事实变化都重新调和 Frontend TravelWidget，并刷新 Lake 菜单里的离局可用性；View 不从旧事件参数拼接状态。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshOnlineWidgetForCurrentController();
	RefreshLakeView();
}

// 动作转交流程：每个 View 意图只调用 Online 的一个公开入口；opaque FGuid 只包装回原句柄类型，UI 不解析平台结果或自行补偿失败。
void UCatLocalPlayerUISubsystem::HandleActionRequested(const ECatOnlineUIAction Action, const FGuid OpaqueHandle)
{
	UCatOnlineSubsystem* Online = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>();
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
// 2. 只在 Frontend 或已经提交去 Lake 的前端旅行态保留 TravelWidget；任何 Lake 状态都移除它，避免玩法里露出 Host/Find/Join 白盒。
// 3. 允许显示时创建并绑定一次动作委托，随后用同一快照配置；Lake 不再由 Frontend 面板承接，显式 UIReach gate 开启时才会出现菜单离局入口。
void UCatLocalPlayerUISubsystem::RefreshOnlineWidgetForCurrentController()
{
	APlayerController* Controller = GetLocalPlayer()->GetPlayerController(GetWorld());
	const UCatOnlineSubsystem* Online = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UCatOnlineSubsystem>();
	if (!Controller || !Online)
	{
		RemoveOnlineWidget();
		return;
	}

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	if (!ShouldShowOnlineTravelWidget(Snapshot))
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

// Frontend 面板判断流程：只承认前台和从前台出发去 Lake 的旅行等待；到达 Lake、离开 Lake 或异常地图时都让玩法/错误收口界面接管。
bool UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot)
{
	return Snapshot.WorldState == ECatOnlineWorldState::Frontend
		|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake;
}

// Lake 离局判断流程：沿用原 Leave gate 的会话身份与空闲条件；默认没有可见菜单，只有显式 UIReach 根启用时才会消费这个结果。
bool UCatLocalPlayerUISubsystem::CanRequestOnlineLeaveFromLake(const FCatOnlineSnapshot& Snapshot)
{
	return Snapshot.WorldState == ECatOnlineWorldState::Lake
		&& Snapshot.SessionRole != ECatOnlineSessionRole::None
		&& Snapshot.ActiveOperation == ECatOnlineOperation::None;
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

// Controller 绑定流程：保存弱引用并在真实 FPawnChangedSignature 上绑定一参数 NewPawn 回调；随后直接消费当前 Pawn，覆盖绑定发生前已经完成占有的冷启动情况。
void UCatLocalPlayerUISubsystem::BindController(APlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}
	BoundPlayerController = Controller;
	PawnChangedHandle = Controller->GetOnNewPawnNotifier().AddUObject(this, &ThisClass::HandleControllerPawnChanged);
	AttachLakePawn(Cast<ACatCharacter>(Controller->GetPawn()));
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

// Pawn 变化流程：无论新 Pawn 类型如何都先恢复旧输入并解绑完整 Lake 根；只有现行 Pawn 是 ACatCharacter 时才重新装配，Frontend 空 Pawn 自然保持无玩法 View。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	DetachLakePawn();
	AttachLakePawn(Cast<ACatCharacter>(NewPawn));
}

// Lake 装配流程：
// 1. 先验证显式 View gate、World、Controller/Pawn 与 ASC，默认玩家路径会在这里直接返回且不创建白盒 UI。
// 2. 创建唯一根 View 和 FishingBridge，再保存身体、GameState、鱼护、Profile 与命令结果源的弱引用。
// 3. 对每个来源成对订阅完整变化通知，安装唯一菜单 Context，并监听本 World 的复制 Session 生成。
// 4. 根 View 进入视口后定位当前 Session，最后一次性发布首份完整 UIReach 投影。
void UCatLocalPlayerUISubsystem::AttachLakePawn(ACatCharacter* Character)
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

	LakeReachWidget = CreateWidget<UCatLakeReachWidget>(Controller, UCatLakeReachWidget::StaticClass());
	FishingViewBridge = NewObject<UCatFishingViewBridge>(this);
	if (!LakeReachWidget || !FishingViewBridge)
	{
		LakeReachWidget = nullptr;
		FishingViewBridge = nullptr;
		return;
	}
	BoundLakeASC = AbilitySystem;
	PoisonChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	BoundCondition = Character->GetConditionComponent();
	BoundGrowth = Character->GetGrowthComponent();
	BoundEquipment = Character->GetEquipmentComponent();
	BoundLakeWorld = Character->GetWorld();
	BoundGameState = BoundLakeWorld.IsValid() ? BoundLakeWorld->GetGameState<ACatfishingGameState>() : nullptr;
	BoundPersonalFishGuard = Character->FindComponentByClass<UCatContainerReplicationComponent>();
	BoundProfile = GetLocalPlayer()->GetSubsystem<UCatProfileSubsystem>();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller))
	{
		BoundFishingCommand = CatController->GetFishingCommandComponent();
	}
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		ConditionChangedHandle = Conditions->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		GrowthChangedHandle = Growth->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		EquipmentChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		RunChangedHandle = GameState->OnRunPublicStateChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
		HelpChangedHandle = GameState->OnHelpSignalChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		FishGuardChangedHandle = FishGuard->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		FishCollectionChangedHandle = Profile->OnFishCollectionChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.AddDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	FishingViewChangedHandle = FishingViewBridge->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleFishingViewStateChanged);
	LakeMenuCloseHandle = LakeReachWidget->OnCloseRequested.AddUObject(
		this, &ThisClass::HandleLakeMenuCloseRequested);
	LakeMenuLeaveHandle = LakeReachWidget->OnLeaveRequested.AddUObject(
		this, &ThisClass::HandleLakeLeaveRequested);
	if (UWorld* World = BoundLakeWorld.Get())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleWorldActorSpawned));
	}
	InstallMenuInput(Controller);
	LakeReachWidget->AddToViewport(1);
	RefreshFishingSessionBinding();
	RefreshLakeView();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_reach_attached World=%s Controller=%s RootCount=1"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"), *GetNameSafe(Controller));
}

// Lake 解绑流程：
// 1. 若菜单打开，先用旧 Controller 恢复 GameOnly 与原鼠标状态，再移除菜单 Action/Context。
// 2. 从 ASC、Condition、Equipment、GameState、鱼护、Profile、命令组件、FishingBridge、会话生命周期观察与 World 精确移除全部委托。
// 3. 清句柄、弱引用、反馈缓存和唯一状态，最后移出根 View，阻断旧 World 的迟到刷新。
void UCatLocalPlayerUISubsystem::DetachLakePawn()
{
	if (bLakeMenuOpen)
	{
		bLakeMenuOpen = false;
		ApplyLakeMenuInputMode(false);
	}
	RemoveMenuInput();
	if (UAbilitySystemComponent* AbilitySystem = BoundLakeASC.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute()).Remove(PoisonChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		Conditions->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		Growth->OnSnapshotChanged.Remove(GrowthChangedHandle);
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
	if (UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		FishGuard->OnSnapshotChanged.Remove(FishGuardChangedHandle);
	}
	if (UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		Profile->OnFishCollectionChanged.Remove(FishCollectionChangedHandle);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.RemoveDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	StopObservingFishingSessionLifecycle();
	if (FishingViewBridge)
	{
		FishingViewBridge->OnViewStateChanged.Remove(FishingViewChangedHandle);
		FishingViewBridge->UnbindSession();
	}
	if (UWorld* World = BoundLakeWorld.Get(); World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	if (LakeReachWidget)
	{
		LakeReachWidget->OnCloseRequested.Remove(LakeMenuCloseHandle);
		LakeReachWidget->OnLeaveRequested.Remove(LakeMenuLeaveHandle);
	}
	PoisonChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	GrowthChangedHandle.Reset();
	EquipmentChangedHandle.Reset();
	RunChangedHandle.Reset();
	HelpChangedHandle.Reset();
	FishGuardChangedHandle.Reset();
	FishCollectionChangedHandle.Reset();
	FishingViewChangedHandle.Reset();
	ActorSpawnedHandle.Reset();
	LakeMenuCloseHandle.Reset();
	LakeMenuLeaveHandle.Reset();
	BoundLakeASC.Reset();
	BoundCondition.Reset();
	BoundGrowth.Reset();
	BoundEquipment.Reset();
	BoundGameState.Reset();
	BoundLakeWorld.Reset();
	BoundPersonalFishGuard.Reset();
	BoundProfile.Reset();
	BoundFishingCommand.Reset();
	FishingViewBridge = nullptr;
	LastFishingCommandResult = FCatFishingCommandResult();
	bHasFishingCommandResult = false;
	bPreviousMouseCursorVisible = false;
	if (LakeReachWidget)
	{
		LakeReachWidget->RemoveFromParent();
		LakeReachWidget = nullptr;
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_reach_detached World=%s RootCount=0"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
	}
}

// 属性变化流程：事件只表示某个 ASC 事实已变；忽略单项载荷后重读所有身体与其他来源，避免增量顺序形成 UI 私有状态。
void UCatLocalPlayerUISubsystem::HandleLakeAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	RefreshLakeView();
}

// 快照变化流程：Condition、Equipment、GameState、鱼护或 Profile 都只提供“需要重读”信号；统一重建整份 DTO，不缓存各来源增量。
void UCatLocalPlayerUISubsystem::HandleLakeSnapshotChanged()
{
	RefreshLakeView();
}

// Fishing 投影流程：Bridge 已经用完整 Session Snapshot 更新自身；忽略事件载荷后重建统一 ViewState，避免根 View 持有第二套 Fishing Model。
void UCatLocalPlayerUISubsystem::HandleFishingViewStateChanged(const FCatFishingViewState& ViewState)
{
	(void)ViewState;
	RefreshLakeView();
}

// 命令结果流程：复制一条只用于展示的结构化终态，标记反馈可见；再定位可能由该命令创建的 Session，最后刷新统一根 View。
void UCatLocalPlayerUISubsystem::HandleFishingCommandResult(const FCatFishingCommandResult& Result)
{
	LastFishingCommandResult = Result;
	bHasFishingCommandResult = true;
	RefreshFishingSessionBinding();
	RefreshLakeView();
}

// Actor 生成流程：只响应当前绑定 World 的 FishingSession，并把重新定位推迟到下一帧，让网络初始复制先填充 FisherPlayerState；旧 World 的迟到事件不会刷新新根。
void UCatLocalPlayerUISubsystem::HandleWorldActorSpawned(AActor* SpawnedActor)
{
	UWorld* World = BoundLakeWorld.Get();
	if (!Cast<ACatFishingSession>(SpawnedActor) || !World || SpawnedActor->GetWorld() != World)
	{
		return;
	}
	World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshFishingSessionBinding);
}

// Session 调和流程：
// 1. 按当前 Controller 的公开 PlayerState 查找唯一非终态复制会话，Bridge 查找不会返回 terminal Session。
// 2. 若当前 Bridge 仍绑定一个 terminal 但未 EndPlay 的会话，继续保留它，让终态复制窗口能被 UI 展示。
// 3. 只有发现新非终态候选、或当前非终态会话确实消失时，才通过统一 helper 切换 Bridge 与生命周期观察。
void UCatLocalPlayerUISubsystem::RefreshFishingSessionBinding()
{
	if (!FishingViewBridge)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(
		BoundLakeWorld.Get(), PlayerState);
	ACatFishingSession* CurrentSession = FishingViewBridge->GetBoundSession();
	if (CurrentSession == Session)
	{
		ObserveFishingSessionLifecycle(CurrentSession);
		return;
	}
	if (!Session && CurrentSession && CurrentSession->IsTerminal())
	{
		ObserveFishingSessionLifecycle(CurrentSession);
		return;
	}
	SetFishingViewSession(Session);
}

// 会话切换流程：先让旧 Session 的 Actor 生命周期委托与 Bridge 解绑，再绑定新 Session 并观察它的 EndPlay/Destroyed；绑定失败或传空都会刷新为无活动会话。
void UCatLocalPlayerUISubsystem::SetFishingViewSession(ACatFishingSession* Session)
{
	if (!FishingViewBridge)
	{
		StopObservingFishingSessionLifecycle();
		return;
	}
	if (FishingViewBridge->GetBoundSession() == Session)
	{
		ObserveFishingSessionLifecycle(Session);
		return;
	}

	StopObservingFishingSessionLifecycle();
	if (Session && FishingViewBridge->BindSession(Session))
	{
		ObserveFishingSessionLifecycle(Session);
		return;
	}

	FishingViewBridge->UnbindSession();
	RefreshLakeView();
}

// 生命周期观察流程：先用对象身份键过滤同一会话的重复绑定；新会话到来时移除旧动态委托，再保存弱引用和 FObjectKey。
// Destroyed 与 EndPlay 都进同一收口以覆盖复制销毁和 World 移除；FObjectKey 只用于销毁期来源比对，不持有 Actor。
void UCatLocalPlayerUISubsystem::ObserveFishingSessionLifecycle(ACatFishingSession* Session)
{
	const FObjectKey SessionKey(Session);
	if (ObservedFishingSessionKey == SessionKey)
	{
		return;
	}
	StopObservingFishingSessionLifecycle();
	if (!Session)
	{
		return;
	}
	ObservedFishingSession = Session;
	ObservedFishingSessionKey = SessionKey;
	Session->OnDestroyed.AddDynamic(this, &ThisClass::HandleFishingSessionDestroyed);
	Session->OnEndPlay.AddDynamic(this, &ThisClass::HandleFishingSessionEndPlay);
}

// 生命周期解绑流程：旧会话仍有效时从同一个 Actor 移除 Destroyed 与 EndPlay；Actor 已经进入销毁时只清本地弱引用和身份键，不延长其生命周期。
void UCatLocalPlayerUISubsystem::StopObservingFishingSessionLifecycle()
{
	if (ACatFishingSession* Session = ObservedFishingSession.Get())
	{
		Session->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleFishingSessionDestroyed);
		Session->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleFishingSessionEndPlay);
	}
	ObservedFishingSession.Reset();
	ObservedFishingSessionKey = FObjectKey();
}

// Destroyed 回调流程：Destroyed 与 EndPlay 可能在同一次销毁中先后到达；统一收口会用当前观察身份键去重并过滤旧会话。
void UCatLocalPlayerUISubsystem::HandleFishingSessionDestroyed(AActor* DestroyedActor)
{
	HandleFishingSessionLifecycleEnded(DestroyedActor);
}

// EndPlay 回调流程：EndPlayReason 只表示 Actor 离开 World 的原因；UIReach 不根据原因推导玩法结果，只确认当前会话已经不可继续展示为活动会话。
void UCatLocalPlayerUISubsystem::HandleFishingSessionEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	(void)EndPlayReason;
	HandleFishingSessionLifecycleEnded(Actor);
}

// 会话结束收口流程：先用 FObjectKey 确认广播源仍是当前观察会话，过滤旧会话和 Destroyed/EndPlay 双触发；再解除观察、解绑 Bridge 并重建根 View。
// 这样终态复制窗口结束后，UI 会回到 no active session。
void UCatLocalPlayerUISubsystem::HandleFishingSessionLifecycleEnded(AActor* SessionActor)
{
	if (!SessionActor || ObservedFishingSessionKey != FObjectKey(SessionActor))
	{
		return;
	}
	StopObservingFishingSessionLifecycle();
	if (FishingViewBridge)
	{
		FishingViewBridge->UnbindSession();
	}
	RefreshLakeView();
}

// ViewState 刷新流程：先验证根 View、Controller、Character、ASC 和绑定 World 仍是同一套 Lake 生命周期；任一对象缺失或换 World/换 Pawn 时直接返回，不渲染旧事实。
// 通过校验后读取三项 ASC 属性；Condition、Growth、Equipment、GameState、鱼护、Profile 和 Online 都是可选只读来源，缺失时保留 DTO 默认值或不可用标记。
// FishingBridge 只有在仍绑定有效 Session 时才写入会话 DTO 并置 bHasFishingSession；最近命令结果由单独标记决定是否展示，避免旧结果伪装成当前会话。
// Profile 快照决定 bFishCollectionAvailable，Online 快照只决定菜单离局 gate；最后把这一份聚合 ViewState 一次性交给 LakeReach 根渲染。
void UCatLocalPlayerUISubsystem::RefreshLakeView()
{
	UAbilitySystemComponent* AbilitySystem = BoundLakeASC.Get();
	const APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UWorld* LakeWorld = BoundLakeWorld.Get();
	if (!LakeReachWidget || !Character || !AbilitySystem || !LakeWorld || LakeWorld != GetWorld()
		|| Character->GetWorld() != LakeWorld
		|| Character->GetAbilitySystemComponent() != AbilitySystem)
	{
		return;
	}
	FCatUIReachViewState ViewState;
	ViewState.Poison = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
	ViewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	ViewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (const UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		ViewState.Condition = Conditions->GetSnapshot();
	}
	if (const UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		ViewState.Growth = Growth->GetSnapshot();
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
	if (FishingViewBridge && FishingViewBridge->GetBoundSession())
	{
		ViewState.Fishing = FishingViewBridge->GetViewState();
		ViewState.bHasFishingSession = true;
	}
	ViewState.LastFishingCommandResult = LastFishingCommandResult;
	ViewState.bHasFishingCommandResult = bHasFishingCommandResult;
	if (const UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		ViewState.PersonalFishGuard = FishGuard->GetSnapshot();
	}
	if (const UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		ViewState.bFishCollectionAvailable = Profile->GetFishCollectionSnapshot(ViewState.FishCollection);
	}
	ViewState.bMenuOpen = bLakeMenuOpen;
	if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
	{
		ViewState.MenuToggleKeyName = Settings->LakeMenuToggleKeyName;
	}
	if (const UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		if (const UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
		{
			ViewState.bCanRequestOnlineLeave = CanRequestOnlineLeaveFromLake(Online->GetSnapshot());
		}
	}
	LakeReachWidget->Render(ViewState);
}

// 菜单输入安装流程：先要求 Controller 使用 EnhancedInputComponent、LocalPlayer 子系统和有效配置键；然后创建瞬时 Action/Context、映射一个键、安装 Context 并保存唯一 Action 绑定句柄。
void UCatLocalPlayerUISubsystem::InstallMenuInput(APlayerController* Controller)
{
	RemoveMenuInput();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FKey MenuKey = Settings ? FKey(Settings->LakeMenuToggleKeyName) : FKey();
	if (!Input || !InputSubsystem || !Settings || !MenuKey.IsValid())
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_reach_menu_input_unavailable Controller=%s Key=%s"),
			*GetNameSafe(Controller), Settings ? *Settings->LakeMenuToggleKeyName.ToString() : TEXT("None"));
		return;
	}

	LakeMenuToggleAction = NewObject<UInputAction>(this);
	LakeMenuMappingContext = NewObject<UInputMappingContext>(this);
	if (!LakeMenuToggleAction || !LakeMenuMappingContext)
	{
		LakeMenuToggleAction = nullptr;
		LakeMenuMappingContext = nullptr;
		return;
	}
	LakeMenuToggleAction->ValueType = EInputActionValueType::Boolean;
	LakeMenuMappingContext->MapKey(LakeMenuToggleAction, MenuKey);
	InputSubsystem->AddMappingContext(LakeMenuMappingContext, FMath::Max(0, Settings->LakeMenuInputPriority));
	bLakeMenuMappingInstalled = true;
	LakeMenuInputBindingHandle = Input->BindAction(
		LakeMenuToggleAction, ETriggerEvent::Started, this, &ThisClass::ToggleLakeMenu).GetHandle();
	BoundMenuInputComponent = Input;
}

// 菜单输入移除流程：先从安装时保存的 EnhancedInputComponent 删除精确绑定，再从同一 LocalPlayer 移除已安装 Context；最后清弱引用、句柄、配对标记和瞬时对象。
void UCatLocalPlayerUISubsystem::RemoveMenuInput()
{
	if (UEnhancedInputComponent* Input = BoundMenuInputComponent.Get();
		Input && LakeMenuInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(LakeMenuInputBindingHandle);
	}
	if (bLakeMenuMappingInstalled && LakeMenuMappingContext && GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			InputSubsystem->RemoveMappingContext(LakeMenuMappingContext);
		}
	}
	BoundMenuInputComponent.Reset();
	LakeMenuInputBindingHandle = 0;
	bLakeMenuMappingInstalled = false;
	LakeMenuToggleAction = nullptr;
	LakeMenuMappingContext = nullptr;
}

// InputMode 流程：打开时把根 Widget 设为 UIOnly 焦点并显示鼠标；关闭时恢复 GameOnly 和打开前鼠标可见性。无旧 Controller 时只保持状态，避免跨 World 操作新 Controller。
void UCatLocalPlayerUISubsystem::ApplyLakeMenuInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller)
	{
		return;
	}
	if (bOpen && LakeReachWidget)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(LakeReachWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(InputMode);
		Controller->bShowMouseCursor = true;
		LakeReachWidget->SetKeyboardFocus();
		return;
	}
	Controller->SetInputMode(FInputModeGameOnly());
	Controller->bShowMouseCursor = bPreviousMouseCursorVisible;
}

// 关闭意图流程：只有菜单仍打开时才调用唯一 Toggle；迟到按钮或销毁期广播不会把已关闭菜单重新打开。
void UCatLocalPlayerUISubsystem::HandleLakeMenuCloseRequested()
{
	if (bLakeMenuOpen)
	{
		ToggleLakeMenu();
	}
}

// Lake 离局流程：把菜单里的退出意图复用为已有 Online Leave 动作；请求后立刻重刷菜单，让同步拒绝或 pending 状态能反映到按钮可用性。
void UCatLocalPlayerUISubsystem::HandleLakeLeaveRequested()
{
	HandleActionRequested(ECatOnlineUIAction::Leave, FGuid());
	RefreshLakeView();
}
