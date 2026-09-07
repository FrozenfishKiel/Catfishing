#include "UI/CatLocalPlayerUISubsystem.h"

#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"
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
	RefreshFrontendForCurrentController();
}

// 销毁流程：先释放 HUD、背包和唯一交互提示模块；再解绑 Controller、移除 Frontend Root 与 Online 快照订阅。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachPlayerLakeUI();
	UnbindController();
	RemoveFrontendRoot();
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

// Controller 替换流程：
// 1. 先按当前 Online 快照判断已有 Frontend Root 是否属于 Start 加载、旅行等待或失败恢复保护窗。
// 2. 局内 HUD、背包和交互提示始终拆掉，因为它们绑定旧 Pawn 和输入；受保护的 Frontend Root 不在这里移除。
// 3. 父类完成 LocalPlayer 的 Controller 切换后重新绑定新 Controller，并在保留 Root 时显式恢复拥有者、鼠标和键盘焦点。
// 4. 最后重新调和 Frontend，非受保护状态会按常规 World/配置规则移除或重建。
void UCatLocalPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	bool bShouldKeepFrontendRoot = false;
	if (FrontendRootWidget)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
			{
				if (UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
				{
					bShouldKeepFrontendRoot = ShouldKeepExistingFrontendRoot(Online->GetSnapshot());
				}
			}
		}
	}
	DetachPlayerLakeUI();
	UnbindController();
	if (!bShouldKeepFrontendRoot)
	{
		RemoveFrontendRoot();
	}
	Super::PlayerControllerChanged(NewController);
	BindController(NewController);
	if (bShouldKeepFrontendRoot && FrontendRootWidget && NewController && NewController->IsLocalController())
	{
		if (FrontendRootWidget->GetOwningPlayer() != NewController)
		{
			FrontendRootWidget->SetOwningPlayer(NewController);
		}
		NewController->SetShowMouseCursor(true);
		FrontendRootWidget->SetKeyboardFocus();
	}
	RefreshFrontendForCurrentController();
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

// 快照消费流程：Online 变更时按当前 World 调和正式 Frontend Root，并刷新局内 HUD；库存只听自己的数据源，不把会话状态当库存变化。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshFrontendForCurrentController();
	if (HUDModel)
	{
		HUDModel->Refresh();
	}
}

// Frontend 调和流程：
// 1. 先读取当前 LocalPlayer、Controller 和 Online 快照；缺 LocalPlayer 或 Online 时拆掉 Root，避免旧前端脱离事实源继续显示。
// 2. 没有本地 Controller 时只允许已有 Root 在 Start 加载、旅行等待或失败恢复保护窗内短暂保留；其它情况立即拆除。
// 3. 非 Frontend World 只保留受保护的已有 Root，不在玩法图或异常 World 补建新主界面，避免旧前端变成第二入口。
// 4. 已有 Root 直接复用；需要新建时必须仍处于 Frontend World，并且配置能加载正式 Root WBP，否则记录失败并保持无原生替身。
// 5. 创建成功后装配 Root、PageController 和三个只读 Model，最后入视口、打开鼠标并设置键盘焦点。
void UCatLocalPlayerUISubsystem::RefreshFrontendForCurrentController()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!LocalPlayer || !Online)
	{
		RemoveFrontendRoot();
		return;
	}
	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	const bool bIsFrontendWorld = Snapshot.WorldState == ECatOnlineWorldState::Frontend;
	const bool bShouldKeepExistingRoot = ShouldKeepExistingFrontendRoot(Snapshot);
	if (!Controller || !Controller->IsLocalController())
	{
		if (bShouldKeepExistingRoot) { return; }
		RemoveFrontendRoot();
		return;
	}
	if (!bIsFrontendWorld && !bShouldKeepExistingRoot)
	{
		RemoveFrontendRoot();
		return;
	}
	if (FrontendRootWidget)
	{
		return;
	}
	if (!bIsFrontendWorld)
	{
		return;
	}
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const TSubclassOf<UCatFrontendRootWidget> RootClass = Settings ? Settings->LoadFrontendRootWidgetClass() : nullptr;
	if (!RootClass)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_root_unavailable World=%s Reason=invalid_config"), *GetWorld()->GetName());
		return;
	}
	FrontendRootWidget = CreateWidget<UCatFrontendRootWidget>(Controller, RootClass);
	FrontendSaveModel = NewObject<UCatFrontendSaveModel>(this);
	FrontendRoomModel = NewObject<UCatFrontendRoomModel>(this);
	FrontendSettingsModel = NewObject<UCatFrontendSettingsModel>(this);
	FrontendPageController = NewObject<UCatFrontendPageController>(this);
	if (!FrontendRootWidget || !FrontendSaveModel || !FrontendRoomModel || !FrontendSettingsModel || !FrontendPageController)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_create_failed World=%s"), *GetWorld()->GetName());
		RemoveFrontendRoot();
		return;
	}
	FrontendSaveModel->Initialize(LocalPlayer);
	FrontendRoomModel->Initialize(LocalPlayer);
	FrontendSettingsModel->Initialize(LocalPlayer);
	FrontendRootWidget->InitializeFrontend(FrontendPageController, FrontendSaveModel, FrontendRoomModel, FrontendSettingsModel);
	FrontendPageController->Initialize(LocalPlayer, FrontendRootWidget, FrontendSaveModel, FrontendRoomModel, FrontendSettingsModel);
	FrontendRootWidget->AddToViewport(20);
	Controller->SetShowMouseCursor(true);
	FrontendRootWidget->SetKeyboardFocus();
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_root_created World=%s NetMode=%d Controller=%s RootClass=%s"), *GetWorld()->GetName(),
		static_cast<int32>(GetWorld()->GetNetMode()), *GetNameSafe(Controller), *GetNameSafe(RootClass.Get()));
}

// 已有 Root 保留判断流程：
// 1. 没有 Root 时直接返回 false；该策略只保护已经可见的加载页，不负责补建任何非 Frontend World UI。
// 2. Start 请求带有效 RequestId 且处在预载、旅行排队或前往 Lake World 时保留，覆盖 Controller 暂空和 World 切换窗口。
// 3. Start 失败恢复只在已有 Root 上成立，让错误文本能回到 Frontend/Room；其它 Lake 或无关 World 继续走拆除路径。
bool UCatLocalPlayerUISubsystem::ShouldKeepExistingFrontendRoot(const FCatOnlineSnapshot& Snapshot) const
{
	if (!FrontendRootWidget)
	{
		return false;
	}
	const bool bGameplayStartLoadingOrTraveling = Snapshot.ActiveOperation == ECatOnlineOperation::Start
		&& Snapshot.LastError == ECatOnlineError::None && Snapshot.RequestId.IsValid()
		&& (Snapshot.bIsGameplayLoadPending || Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
			|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake);
	const bool bGameplayStartFailureRecovering = Snapshot.ActiveOperation == ECatOnlineOperation::None
		&& (Snapshot.LastError == ECatOnlineError::GameplayPreloadFailed || Snapshot.LastError == ECatOnlineError::TravelRejected
			|| Snapshot.LastError == ECatOnlineError::TravelFailed || Snapshot.LastError == ECatOnlineError::ConnectStringUnavailable
			|| Snapshot.LastError == ECatOnlineError::NetworkFailure || Snapshot.LastError == ECatOnlineError::ClientStartRetryExhausted)
		&& (Snapshot.WorldState == ECatOnlineWorldState::Frontend || Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
			|| Snapshot.TransportState == ECatOnlineTransportState::Failed);
	return bGameplayStartLoadingOrTraveling || bGameplayStartFailureRecovering;
}

// Frontend 拆除流程：先关闭流程控制器的 Model 订阅和 Root 绑定，再拆 Root，最后关闭三个 Model；若调用时仍有绑定 Controller，才恢复非鼠标前端状态，此路径不碰既有 Lake HUD、背包和交互提示。
void UCatLocalPlayerUISubsystem::RemoveFrontendRoot()
{
	const bool bHadFrontendRoot = FrontendRootWidget != nullptr;
	if (FrontendPageController) { FrontendPageController->Shutdown(); }
	if (FrontendRootWidget)
	{
		FrontendRootWidget->ResetFrontend();
		FrontendRootWidget->RemoveFromParent();
	}
	if (FrontendSaveModel) { FrontendSaveModel->Shutdown(); }
	if (FrontendRoomModel) { FrontendRoomModel->Shutdown(); }
	if (FrontendSettingsModel) { FrontendSettingsModel->Shutdown(); }
	FrontendPageController = nullptr;
	FrontendRootWidget = nullptr;
	FrontendSaveModel = nullptr;
	FrontendRoomModel = nullptr;
	FrontendSettingsModel = nullptr;
	if (bHadFrontendRoot)
	{
		if (APlayerController* Controller = BoundPlayerController.Get()) { Controller->SetShowMouseCursor(false); }
	}
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
// 2. 创建 HUD Model/View 并入视口；默认主界面只常驻天数、背包入口和设置入口，背包内容由库存页打开后再显示。
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
		TEXT("Event=ui_player_modules_attached World=%s NetMode=%d LocalPlayerIndex=%d Controller=%s LocalController=%s HUD=%s HUDMode=minimal_main Inventory=%s Slot=%s Interaction=%s ShopPrecreated=false"),
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

// HUD 渲染转交流程：Model 已聚合天数、入口显隐和可选调试反馈；Subsystem 只把它交给 HUD WBP。
void UCatLocalPlayerUISubsystem::HandleHUDModelViewStateChanged()
{
	if (HUDModel && HUDWidget)
	{
		HUDWidget->RenderHUD(HUDModel->GetViewState());
	}
}

// HUD 入口动作流程：背包转交现有控制器；菜单仍保留给蓝图或后续页面控制器。
void UCatLocalPlayerUISubsystem::HandleHUDActionRequested(const ECatHUDAction Action)
{
	switch (Action)
	{
	case ECatHUDAction::OpenInventory:
		ToggleInventory();
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
