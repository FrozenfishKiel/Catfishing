#include "UI/CatLocalPlayerUISubsystem.h"

#include "AbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "UI/CatSurvivalWidget.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// Online 解析流程：LocalPlayerSubsystem 可能在无窗口自动化或早期生命周期中没有 Viewport/GameInstance；这里统一把
	// 缺宿主收口为空，让 UI 保持 fail-closed 而不是解引用崩溃。
	UCatOnlineSubsystem* ResolveOnlineSubsystem(const ULocalPlayer* LocalPlayer)
	{
		UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	}

	// 控件类解析流程：配置里指了 WBP 就用 WBP，没指或者加载不出来就用模板参数那个 C++ 本体。
	// 不把"没配"当错误是故意的：项目现在一个 WBP 都没有，而 C++ 本体本来就能完整工作。按失败处理会让整块界面
	// 凭空消失，那比"长得不好看"严重得多；加载失败（资产被改名、被删、或者指错了类）同理。
	// LoadSynchronous 已经帮忙挡掉了"配了一个不是这条继承链的类"，所以这里只需要处理空值。
	template <typename TWidget>
	TSubclassOf<TWidget> ResolveWidgetClass(const TSoftClassPtr<TWidget>& ConfiguredClass)
	{
		UClass* Configured = ConfiguredClass.IsNull() ? nullptr : ConfiguredClass.LoadSynchronous();
		return Configured ? Configured : TWidget::StaticClass();
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
		OnlineWidget = CreateWidget<UCatTravelWidget>(Controller,
			ResolveWidgetClass<UCatTravelWidget>(GetDefault<UCatUISettings>()->TravelWidgetClass));
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

	SurvivalWidget = CreateWidget<UCatSurvivalWidget>(Controller,
		ResolveWidgetClass<UCatSurvivalWidget>(Settings->SurvivalWidgetClass));
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
	// 这条日志是"本机 LocalPlayer 已经占有身体并把 HUD 装上了"在日志里的唯一痕迹：双进程联机冒烟
	// Tools/run_multiplayer_smoke.ps1 从客户端日志里抓它，来判定远端客户端的"占有 Pawn + 装配 UI"管线是否成立。
	// 改名或删掉它会让那个冒烟失去客户端侧唯一的判据。
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_survival_view_created World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
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
	if (SurvivalWidget)
	{
		SurvivalWidget->RemoveFromParent();
		SurvivalWidget = nullptr;
		// 与 ui_survival_view_created 成对：拆掉时也留一条，事后看日志才能分清"从来没装配过"和"装配后又被拆了"。
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_survival_view_removed World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
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
}
