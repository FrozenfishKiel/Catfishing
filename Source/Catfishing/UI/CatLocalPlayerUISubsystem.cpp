#include "UI/CatLocalPlayerUISubsystem.h"

#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionModel.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPageController.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatCampInventoryWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 初始化流程：先订阅唯一 Online 快照，再弱绑定当前 Controller；本地玩家 UI 模块是否装配由 AttachPlayerLakeUI 统一验证 WBP 配置。
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

// 销毁流程：先释放 HUD、背包和唯一交互提示模块；再解绑 Controller、移除 Frontend View 与 Online 快照订阅。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachPlayerLakeUI();
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
	DetachPlayerLakeUI();
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

// 外部容器背包打开流程：把交互对象提供的容器读源原样交给 Inventory PageController；Subsystem 不解释容器种类或移动权限。
void UCatLocalPlayerUISubsystem::OpenInventoryWithExternalContainerContexts(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers)
{
	if (InventoryPageController)
	{
		InventoryPageController->OpenInventoryWithExternalContainerContexts(ExternalContainers);
	}
}

// 指定库存页打开流程：
// 1. 只检查本地库存控制器是否存在；具体箱子类型、页面类和容器来源都由交互对象提供。
// 2. 控制器缺失时直接返回 false，避免把外部容器打开请求伪装成普通背包切换。
// 3. 把 ViewClass 原样交给库存 PageController 创建并持有，LocalPlayer 不预建鱼护、鱼缸或未来箱子的专用字段。
// 4. 返回真实打开结果，让交互对象按自己的规则决定是否继续报告成功或记录拒绝。
bool UCatLocalPlayerUISubsystem::OpenInventoryWithExternalContainerContextsUsingViewClass(
	const TArray<UCatContainerReplicationComponent*>& ExternalContainers,
	const TSubclassOf<UCatInventoryWidget> InventoryViewClass)
{
	if (!InventoryPageController)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_external_inventory_unavailable PageController=missing ViewClass=%s"),
			*GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	return InventoryPageController->OpenInventoryWithExternalContainerContextsUsingViewClass(
		ExternalContainers, InventoryViewClass);
}

// 营地公共仓库打开流程：
// 1. 只检查本地库存控制器、目标公共仓库和仓库自己的独立 WBP 类；缺任一项都明确返回失败。
// 2. 把目标 Actor 和页面类原样交给库存 PageController，LocalPlayer 不读取公共仓库格，也不把这次请求退回普通背包。
// 3. 返回真实打开结果，让交互 Actor 可以记录拒绝或成功，不把失败伪装成默认背包切换。
bool UCatLocalPlayerUISubsystem::OpenCampInventory(ACatCampInventoryActor* CampInventory,
	const TSubclassOf<UCatCampInventoryWidget> InventoryViewClass)
{
	if (!InventoryPageController || !CampInventory || !InventoryViewClass)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_camp_inventory_unavailable PageController=%s CampInventory=%s ViewClass=%s"),
			InventoryPageController ? TEXT("valid") : TEXT("missing"),
			*GetNameSafe(CampInventory),
			*GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	return InventoryPageController->OpenCampInventory(CampInventory, InventoryViewClass);
}

// 状态读取流程：从 PageController 读取唯一背包状态；未装配背包时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsInventoryOpen() const
{
	return InventoryPageController ? InventoryPageController->IsInventoryOpen() : false;
}

// 库存 Model 查询流程：只把当前本地玩家已有的 Model 暴露给库存 WBP；空指针表示本地玩家 UI 尚未装配完成。
UCatInventoryModel* UCatLocalPlayerUISubsystem::GetInventoryModel() const
{
	return InventoryModel;
}

// 库存 PageController 查询流程：库存 WBP 通过它提交点击、拖拽和关闭意图；显示刷新仍由 WBP 自己完成。
UCatInventoryPageController* UCatLocalPlayerUISubsystem::GetInventoryPageController() const
{
	return InventoryPageController;
}

// 快照消费流程：Online 变更时只调和 Frontend TravelWidget 和 HUD；库存只听自己的数据源，不把会话状态当库存变化。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshOnlineWidgetForCurrentController();
	if (HUDModel)
	{
		HUDModel->Refresh();
	}
}

// 动作转交流程：Frontend TravelWidget 的每个意图只调用 Online 的一个公开入口；局内拆分 UI 不在这里预建或转发对象页面。
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

// Frontend View 调和流程：读取当前完整 Online 快照；只有 Frontend 或前往 Lake 的等待态保留 TravelWidget，Lake 内正式入口交给局内拆分 UI。
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

// Frontend 面板判断流程：只承认前台和从前台出发去 Lake 的旅行等待；到达 Lake 后玩家入口由 HUD、背包和交互提示接管。
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

// Controller 刷新流程：
// 1. 拒绝非本地 Controller，避免服务器远端 Controller 误创建 UI。
// 2. 如果通知来自当前未绑定但属于本 LocalPlayer 的 Controller，先完成绑定并消费当前 Pawn。
// 3. 如果通知来自已绑定 Controller，则按它当前 Pawn 刷新本地玩家 UI；重复通知由 HandleControllerPawnChanged 幂等裁剪。
void UCatLocalPlayerUISubsystem::RefreshPlayerLakeUIForController(APlayerController* Controller)
{
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	if (Controller != BoundPlayerController.Get())
	{
		ULocalPlayer* LocalPlayer = GetLocalPlayer();
		if (LocalPlayer && LocalPlayer->GetPlayerController(GetWorld()) == Controller)
		{
			DetachPlayerLakeUI();
			UnbindController();
			BindController(Controller);
		}
		return;
	}
	HandleControllerPawnChanged(Controller->GetPawn());
}

// Controller 绑定流程：保存弱引用并立即消费当前 Pawn，覆盖绑定前已经完成占有或客户端 ClientRestart 的冷启动情况。
void UCatLocalPlayerUISubsystem::BindController(APlayerController* Controller)
{
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	BoundPlayerController = Controller;
	HandleControllerPawnChanged(Controller->GetPawn());
}

// Controller 解绑流程：只清理本地弱引用；Pawn 刷新由 PlayerController 生命周期主动推送，因此这里不再保留旧 notifier 句柄。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	BoundPlayerController.Reset();
}

// Pawn 变化流程：
// 1. 先把 NewPawn 裁成项目猫身体；同一个已装配身体的重复通知只刷新输入绑定，库存数据继续等自己的读源广播。
// 2. 新身体或空身体会先完整拆掉上一套本地玩家 UI，避免跨 Pawn 复用 Model、View 或输入锁。
// 3. 只有新的 ACatCharacter 通过配置校验时才重新装配 HUD、背包、交互提示和拾取提示层。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	ACatCharacter* Character = Cast<ACatCharacter>(NewPawn);
	if (Character && AttachedPlayerLakeCharacter.Get() == Character
		&& HUDWidget && InventoryPageController && InteractionPageController)
	{
		InventoryPageController->RefreshInputBinding();
		return;
	}
	DetachPlayerLakeUI();
	if (!Character)
	{
		return;
	}
	AttachPlayerLakeUI(Character);
	if (HUDWidget && InventoryPageController && InteractionPageController)
	{
		AttachedPlayerLakeCharacter = Character;
	}
}

// 本地玩家 UI 装配流程：
// 1. 验证本地设置、当前 Controller/Pawn 和 World；核心 WBP 类缺失或无效时直接 fail-closed，不创建原生白盒替身。
// 2. 创建 HUD Model/View 并入视口；HUD 展示猫状态、钓鱼反馈、入口按钮和固定屏幕中心准星。
// 3. 创建 Inventory Model/PageController/普通背包 View，但背包 View 不预先入视口，只通过既有 InputContext 的 Action 打开。
// 4. 创建 Interaction 提示 View 和控制器；控制器订阅 PlayerController 的唯一准星交互目标，商店、鱼护和未来箱子仍由世界交互对象提供页面上下文。
void UCatLocalPlayerUISubsystem::AttachPlayerLakeUI(ACatCharacter* Character)
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
		DetachPlayerLakeUI();
		return;
	}
	HUDActionHandle = HUDWidget->OnActionRequested.AddUObject(this, &ThisClass::HandleHUDActionRequested);
	InventoryWidget->SetInventorySlotWidgetClass(InventorySlotViewClass);
	if (!HUDModel->Bind(GetLocalPlayer(), Controller, Character))
	{
		DetachPlayerLakeUI();
		return;
	}
	HUDModelViewChangedHandle = HUDModel->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleHUDModelViewStateChanged);
	HUDWidget->AddToViewport(1);
	HandleHUDModelViewStateChanged();
	if (!InventoryModel->Bind(GetLocalPlayer(), Controller, Character)
		|| !InventoryPageController->Bind(GetLocalPlayer(), Controller, InventoryModel, InventoryWidget))
	{
		DetachPlayerLakeUI();
		return;
	}
	FCatInteractionPromptViewState HiddenPrompt;
	HiddenPrompt.bVisible = false;
	InteractionPromptWidget->RenderPrompt(HiddenPrompt);
	InteractionPromptWidget->AddToViewport(2);
	if (!InteractionPageController->Bind(Controller, InteractionPromptWidget))
	{
		DetachPlayerLakeUI();
		return;
	}
	const UWorld* World = GetWorld();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_player_modules_attached World=%s NetMode=%d LocalPlayerIndex=%d Controller=%s LocalController=%s HUD=%s Crosshair=gray_center Inventory=%s Slot=%s Interaction=%s ShopPrecreated=false"),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		LocalPlayer ? LocalPlayer->GetLocalPlayerIndex() : INDEX_NONE,
		*GetNameSafe(Controller),
		Controller->IsLocalController() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(HUDWidget->GetClass()),
		*GetNameSafe(InventoryWidget->GetClass()),
		*GetNameSafe(InventorySlotViewClass.Get()),
		*GetNameSafe(InteractionPromptWidget ? InteractionPromptWidget->GetClass() : nullptr));
}

// 本地玩家 UI 解绑流程：PageController 先恢复输入并移出当前库存页，Model 再解除玩法订阅，最后移除各自 WBP 并清引用。
void UCatLocalPlayerUISubsystem::DetachPlayerLakeUI()
{
	CloseCollection();
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
		if (HUDActionHandle.IsValid())
		{
			HUDWidget->OnActionRequested.Remove(HUDActionHandle);
		}
		HUDActionHandle.Reset();
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
	AttachedPlayerLakeCharacter.Reset();
	const UWorld* World = GetWorld();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_player_modules_detached World=%s NetMode=%d LocalPlayerIndex=%d ShopPrecreated=false"),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		LocalPlayer ? LocalPlayer->GetLocalPlayerIndex() : INDEX_NONE);
}

// 图鉴切换流程：存在可见图鉴时关闭；否则按 UI Settings 懒创建 View/Model 并渲染当前 Profile 只读快照。
void UCatLocalPlayerUISubsystem::ToggleCollection()
{
	if (CollectionWidget)
	{
		const bool bWasVisible = CollectionWidget->IsInViewport();
		CloseCollection();
		if (bWasVisible)
		{
			return;
		}
	}

	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const TSubclassOf<UCatCollectionWidget> CollectionViewClass = Settings
		? Settings->LoadCollectionWidgetClass() : nullptr;
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller || !CollectionViewClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_unavailable Controller=%s ViewClass=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(CollectionViewClass.Get()));
		return;
	}

	CollectionModel = NewObject<UCatCollectionModel>(this);
	CollectionWidget = CreateWidget<UCatCollectionWidget>(Controller, CollectionViewClass);
	if (!CollectionModel || !CollectionWidget || !CollectionModel->Bind(GetLocalPlayer()))
	{
		CloseCollection();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_bind_failed Controller=%s ViewClass=%s"),
			*GetNameSafe(Controller),
			*GetNameSafe(CollectionViewClass.Get()));
		return;
	}

	CollectionModelViewChangedHandle = CollectionModel->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleCollectionModelViewStateChanged);
	CollectionWidget->AddToViewport(9);
	HandleCollectionModelViewStateChanged();
}

// 图鉴关闭流程：先移除 Model 订阅并清空只读投影，再移出 View；重复关闭不会影响 HUD、背包或交互提示。
void UCatLocalPlayerUISubsystem::CloseCollection()
{
	if (CollectionModel)
	{
		CollectionModel->OnViewStateChanged.Remove(CollectionModelViewChangedHandle);
		CollectionModel->Unbind();
		CollectionModel = nullptr;
	}
	CollectionModelViewChangedHandle.Reset();
	if (CollectionWidget)
	{
		CollectionWidget->RemoveFromParent();
		CollectionWidget = nullptr;
	}
}

// HUD 渲染转交流程：Model 已聚合状态和钓鱼反馈；Subsystem 只把它交给 HUD WBP。
void UCatLocalPlayerUISubsystem::HandleHUDModelViewStateChanged()
{
	if (HUDModel && HUDWidget)
	{
		HUDWidget->RenderHUD(HUDModel->GetViewState());
	}
}

// 图鉴渲染转交流程：Model 已聚合 Profile 图鉴记录；Subsystem 只把它交给 Collection WBP。
void UCatLocalPlayerUISubsystem::HandleCollectionModelViewStateChanged()
{
	if (CollectionModel && CollectionWidget)
	{
		CollectionWidget->RenderCollection(CollectionModel->GetViewState());
	}
}

// HUD 入口动作流程：背包和图鉴分别转交现有控制器/模型；菜单仍保留给蓝图或后续页面控制器。
void UCatLocalPlayerUISubsystem::HandleHUDActionRequested(const ECatHUDAction Action)
{
	switch (Action)
	{
	case ECatHUDAction::OpenInventory:
		ToggleInventory();
		break;
	case ECatHUDAction::OpenCollection:
		ToggleCollection();
		break;
	case ECatHUDAction::OpenMainMenu:
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_hud_action_forwarded_without_native_page Action=%s"),
			*UEnum::GetValueAsString(Action));
		break;
	default:
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_hud_action_unknown Action=%d"), static_cast<int32>(Action));
		break;
	}
}
