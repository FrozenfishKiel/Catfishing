#include "UI/CatLocalPlayerUISubsystem.h"

#include "Character/CatCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Interaction/CatInteractionTargetingComponent.h"
#include "UI/CatInteractionWidget.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"
#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPageController.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 初始化流程：先订阅唯一 Online 快照，再弱绑定当前 Controller 的 Pawn notifier；本地玩家 UI 模块是否装配由 AttachLakePawn 统一验证 WBP 配置。
void UCatLocalPlayerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
			{
				OnlineSnapshotHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineSnapshotChanged);
			}
		}
		BindController(LocalPlayer->GetPlayerController(GetWorld()));
	}
	RefreshOnlineWidgetForCurrentController();
}

// 销毁流程：先释放 HUD、背包和提示模块，让输入和 View 意图不再触达旧 Controller；再解绑 Controller、移除 Frontend View 与 Online 快照订阅。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachInteractionView();
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
			{
				Online->OnSnapshotChanged.Remove(OnlineSnapshotHandle);
			}
		}
	}
	OnlineSnapshotHandle.Reset();
	Super::Deinitialize();
}

// Controller 替换流程：旧 Controller 仍可访问时先拆掉本地玩家 UI 和 Frontend UI；父类切换后再只针对 NewController 装配当前 Pawn。
void UCatLocalPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	DetachInteractionView();
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	Super::PlayerControllerChanged(NewController);
	BindController(NewController);
	RefreshOnlineWidgetForCurrentController();
}

// 背包切换流程：把输入、焦点和 ViewState 更新全部交给 Inventory PageController；Subsystem 不持有背包布尔值或渲染细节。
void UCatLocalPlayerUISubsystem::ToggleInventory()
{
	if (InventoryPageController)
	{
		InventoryPageController->ToggleInventory();
	}
}

// 状态读取流程：从 PageController 读取唯一背包状态；未装配背包时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsInventoryOpen() const
{
	return InventoryPageController ? InventoryPageController->IsInventoryOpen() : false;
}

// 旧菜单切换流程：迁移期转发给个人背包，避免外部旧入口继续打开已拆除的 LakeReach 总入口。
void UCatLocalPlayerUISubsystem::ToggleLakeMenu()
{
	ToggleInventory();
}

// 旧菜单状态读取流程：迁移期返回个人背包状态，避免保留第二份 LakeReach 打开状态。
bool UCatLocalPlayerUISubsystem::IsLakeMenuOpen() const
{
	return IsInventoryOpen();
}

// 快照消费流程：Online 变更时先调和 Frontend TravelWidget，再刷新 HUD/背包只读模型；不把 Online 事件参数拼进 View。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshOnlineWidgetForCurrentController();
	if (HUDModel)
	{
		HUDModel->Refresh();
	}
	if (InventoryPageController)
	{
		InventoryPageController->RefreshModel();
	}
}

// 动作转交流程：Frontend TravelWidget 的每个意图只调用 Online 的一个公开入口；LakeReach 离局意图由 PageController 直接走同一 Online 管线。
void UCatLocalPlayerUISubsystem::HandleActionRequested(const ECatOnlineUIAction Action, const FGuid OpaqueHandle)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
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

// Frontend View 调和流程：读取当前完整 Online 快照；只有 Frontend 或前往 Lake 的等待态保留 TravelWidget，Lake 内正式入口交给 UIReach MVC。
void UCatLocalPlayerUISubsystem::RefreshOnlineWidgetForCurrentController()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
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

// Frontend 面板判断流程：只承认前台和从前台出发去 Lake 的旅行等待；到达 Lake 后玩家入口由正式 LakeReach WBP 接管。
bool UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot)
{
	return Snapshot.WorldState == ECatOnlineWorldState::Frontend
		|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake;
}

// Frontend View 移除流程：先解绑动作广播再移出视口；空实例保持幂等，避免 Controller 切换时重复 removed 日志干扰验收。
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

// Controller 绑定流程：保存弱引用并订阅当前 Controller 的 Pawn notifier；随后消费当前 Pawn，覆盖绑定前已经完成占有的冷启动情况。
void UCatLocalPlayerUISubsystem::BindController(APlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}
	BoundPlayerController = Controller;
	PawnChangedHandle = Controller->GetOnNewPawnNotifier().AddUObject(this, &ThisClass::HandleControllerPawnChanged);
	HandleControllerPawnChanged(Controller->GetPawn());
}

// Controller 解绑流程：旧对象仍存活时从同一个 notifier 精确移除；弱引用失效时只清本地句柄，不延长 Controller 生命周期。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	if (APlayerController* Controller = BoundPlayerController.Get(); Controller && PawnChangedHandle.IsValid())
	{
		Controller->GetOnNewPawnNotifier().Remove(PawnChangedHandle);
	}
	PawnChangedHandle.Reset();
	BoundPlayerController.Reset();
}

// Pawn 变化流程：无论 NewPawn 类型如何都先完整拆掉上一套本地玩家 UI；只有新的 ACatCharacter 通过配置校验时才重新装配。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	DetachInteractionView();
	DetachLakePawn();
	ACatCharacter* Character = Cast<ACatCharacter>(NewPawn);
	AttachInteractionView(BoundPlayerController.Get(), Character);
	AttachLakePawn(Character);
}

void UCatLocalPlayerUISubsystem::AttachInteractionView(APlayerController* Controller, ACatCharacter* Character)
{
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
	UCatInteractionTargetingComponent* Targeting = CatController
		? CatController->GetInteractionTargetingComponent() : nullptr;
	if (!Settings || !Settings->IsInteractionViewEnabled() || !Character || !CatController || !Targeting
		|| Character->GetWorld() != GetWorld() || CatController->GetPawn() != Character)
	{
		return;
	}

	InteractionWidget = CreateWidget<UCatInteractionWidget>(CatController, UCatInteractionWidget::StaticClass());
	if (!InteractionWidget)
	{
		return;
	}
	BoundInteractionTargeting = Targeting;
	InteractionTargetChangedHandle = Targeting->OnTargetChanged.AddUObject(
		this, &ThisClass::HandleInteractionTargetChanged);
	InteractionWidget->AddToViewport(10);
	InteractionWidget->RenderTarget(Targeting->GetCurrentTarget());
}

void UCatLocalPlayerUISubsystem::DetachInteractionView()
{
	if (UCatInteractionTargetingComponent* Targeting = BoundInteractionTargeting.Get())
	{
		Targeting->OnTargetChanged.Remove(InteractionTargetChangedHandle);
	}
	InteractionTargetChangedHandle.Reset();
	BoundInteractionTargeting.Reset();
	if (InteractionWidget)
	{
		InteractionWidget->RemoveFromParent();
		InteractionWidget = nullptr;
	}
}

void UCatLocalPlayerUISubsystem::HandleInteractionTargetChanged(AActor* PreviousTarget, AActor* CurrentTarget)
{
	(void)PreviousTarget;
	if (InteractionWidget)
	{
		InteractionWidget->RenderTarget(CurrentTarget);
	}
}

// 本地玩家 UI 装配流程：
// 1. 验证本地设置、当前 Controller/Pawn 和 World；核心 WBP 类缺失或无效时直接 fail-closed，不创建原生白盒替身。
// 2. 创建 HUD Model/View 并入视口；HUD 只展示猫状态和钓鱼反馈。
// 3. 创建 Inventory Model/PageController/View，但背包 View 不预先入视口，只通过既有 InputContext 的 Action 打开。
// 4. 创建 Interaction 提示 View 和控制器；控制器只扫描通用交互目标，商店仍由世界交互对象拥有。
void UCatLocalPlayerUISubsystem::AttachLakePawn(ACatCharacter* Character)
{
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (!Settings || !Settings->IsPlayerLakeUIEnabled() || !Character || Character->GetWorld() != GetWorld())
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller || Controller->GetPawn() != Character)
	{
		return;
	}

	const TSubclassOf<UCatHUDWidget> HUDViewClass = Settings->LoadHUDWidgetClass();
	const TSubclassOf<UCatInventoryWidget> InventoryViewClass = Settings->LoadInventoryWidgetClass();
	const TSubclassOf<UCatInventorySlotWidget> InventorySlotViewClass = Settings->LoadInventorySlotWidgetClass();
	const TSubclassOf<UCatInteractionPromptWidget> InteractionPromptViewClass =
		Settings->LoadInteractionPromptWidgetClass();
	if (!HUDViewClass || !InventoryViewClass || !InventorySlotViewClass || !InteractionPromptViewClass)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_player_module_class_missing HUD=%s Inventory=%s Slot=%s Interaction=%s"),
			*Settings->HUDWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventoryWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventorySlotWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InteractionPromptWidgetClass.ToSoftObjectPath().ToString());
		return;
	}

	HUDModel = NewObject<UCatHUDModel>(this);
	HUDWidget = CreateWidget<UCatHUDWidget>(Controller, HUDViewClass);
	InventoryModel = NewObject<UCatInventoryModel>(this);
	InventoryPageController = NewObject<UCatInventoryPageController>(this);
	InventoryWidget = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	InteractionPageController = NewObject<UCatInteractionPageController>(this);
	InteractionPromptWidget = CreateWidget<UCatInteractionPromptWidget>(Controller, InteractionPromptViewClass);
	if (!HUDModel || !HUDWidget || !InventoryModel || !InventoryPageController || !InventoryWidget
		|| !InteractionPageController || !InteractionPromptWidget)
	{
		DetachLakePawn();
		return;
	}
	InventoryWidget->SetInventorySlotWidgetClass(InventorySlotViewClass);
	if (!HUDModel->Bind(GetLocalPlayer(), Controller, Character))
	{
		DetachLakePawn();
		return;
	}
	HUDModelViewChangedHandle = HUDModel->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleHUDModelViewStateChanged);
	HUDWidget->AddToViewport(1);
	HandleHUDModelViewStateChanged();
	if (!InventoryModel->Bind(GetLocalPlayer(), Controller, Character)
		|| !InventoryPageController->Bind(GetLocalPlayer(), Controller, InventoryModel, InventoryWidget))
	{
		DetachLakePawn();
		return;
	}
	FCatInteractionPromptViewState HiddenPrompt;
	HiddenPrompt.bVisible = false;
	InteractionPromptWidget->RenderPrompt(HiddenPrompt);
	InteractionPromptWidget->AddToViewport(2);
	if (!InteractionPageController->Bind(Controller, InteractionPromptWidget))
	{
		DetachLakePawn();
		return;
	}
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_player_modules_attached World=%s Controller=%s HUD=%s Inventory=%s Slot=%s Interaction=%s ShopPrecreated=false"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
		*GetNameSafe(Controller),
		*GetNameSafe(HUDWidget->GetClass()),
		*GetNameSafe(InventoryWidget->GetClass()),
		*GetNameSafe(InventorySlotViewClass.Get()),
		*GetNameSafe(InteractionPromptWidget ? InteractionPromptWidget->GetClass() : nullptr));
}

// 本地玩家 UI 解绑流程：PageController 先恢复输入和解绑 View 意图，Model 再解除玩法订阅，最后移除各自 WBP 并清引用。
void UCatLocalPlayerUISubsystem::DetachLakePawn()
{
	if (InventoryPageController)
	{
		InventoryPageController->Unbind();
		InventoryPageController = nullptr;
	}
	if (InventoryModel)
	{
		InventoryModel->Unbind();
		InventoryModel = nullptr;
	}
	if (InventoryWidget)
	{
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}
	if (HUDModel)
	{
		HUDModel->OnViewStateChanged.Remove(HUDModelViewChangedHandle);
		HUDModel->Unbind();
		HUDModel = nullptr;
	}
	HUDModelViewChangedHandle.Reset();
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	if (InteractionPageController)
	{
		InteractionPageController->Unbind();
		InteractionPageController = nullptr;
	}
	if (InteractionPromptWidget)
	{
		InteractionPromptWidget->RemoveFromParent();
		InteractionPromptWidget = nullptr;
	}
	LakeReachPageController = nullptr;
	LakeReachModel = nullptr;
	LakeReachWidget = nullptr;
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_player_modules_detached World=%s ShopPrecreated=false"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
}

// HUD 渲染转交流程：Model 已聚合状态和钓鱼反馈；Subsystem 只把它交给 HUD WBP。
void UCatLocalPlayerUISubsystem::HandleHUDModelViewStateChanged()
{
	if (HUDModel && HUDWidget)
	{
		HUDWidget->RenderHUD(HUDModel->GetViewState());
	}
}
