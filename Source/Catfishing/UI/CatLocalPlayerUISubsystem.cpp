#include "UI/CatLocalPlayerUISubsystem.h"

#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
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
#include "UI/Save/CatLakeMainMenuController.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

namespace CatLocalPlayerUILoadingScreen
{
	/** 全局加载遮罩必须盖过 Frontend Root、HUD、背包和局内 ESC 菜单，避免等待表现落在某个业务页面下面。 */
	constexpr int32 ViewportZOrder = 10000;

	/** 正式 Loading WBP 的固定类路径；它原本属于 Frontend 资产组，现在直接由 LocalPlayer UI 挂成全局遮罩。 */
	constexpr const TCHAR* WidgetClassPath = TEXT("/Game/UI/Frontend/WBP_CatFrontendLoading.WBP_CatFrontendLoading_C");

	/** Start 请求已被 Online 接受但地图包还没暴露百分比时的总进度；它只表达请求事实，不随时间增长。 */
	constexpr float GameplayStartAcceptedProgress = 3.0f;

	/** 地图包真实百分比映射到总进度的起点；包加载仍由引擎异步加载进度驱动。 */
	constexpr float GameplayPackageProgressStart = 8.0f;

	/** 地图包真实百分比映射到总进度的终点；后续切图和 UI 就绪各自用真实 gate 推进。 */
	constexpr float GameplayPackageProgressEnd = 68.0f;

	/** 旅行请求已提交给 ServerTravel 或 ClientTravel 时的总进度；只有 Online 运输事实进入排队才可到达。 */
	constexpr float GameplayTravelQueuedProgress = 74.0f;

	/** 引擎 WorldContext 仍在 TravelURL、PendingNetGame 或 PreLoadMap 阶段时的总进度。 */
	constexpr float GameplayEngineTravelProgress = 82.0f;

	/** PostLoadMap 已确认玩法 World 到达时的总进度；它不代表 Pawn 和 HUD 已经可用。 */
	constexpr float GameplayWorldReachedProgress = 88.0f;

	/** 玩法 World 已经具备 GameState 并 BeginPlay 时的总进度；之后仍要等待本地玩家 UI。 */
	constexpr float GameplayWorldRunningProgress = 92.0f;

	/** Online 运输事实已收口为 Connected 时的总进度；本地玩家表现层仍可能尚未装配。 */
	constexpr float GameplayTransportConnectedProgress = 95.0f;

	/** 本地玩家 Controller 已绑定时的总进度；后续还要等 Pawn、HUD 和页面控制器。 */
	constexpr float GameplayLocalControllerProgress = 96.0f;

	/** 本地猫 Pawn 已经出现时的总进度；HUD 和局内 UI 未就绪时仍不能放掉遮罩。 */
	constexpr float GameplayLocalPawnProgress = 97.0f;

	/** HUD 已经加入视口时的总进度；背包、局内菜单和交互控制器还要继续确认。 */
	constexpr float GameplayHudVisibleProgress = 98.0f;

	/** 局内 UI 控制器基本装配完成时的等待上限；真正 ready 后直接隐藏，不在 100% 停留。 */
	constexpr float GameplayLocalUIReadyProgress = 99.0f;

	/** 加载遮罩仍可见时允许显示的最大总进度；100 只属于 ready 后的瞬时完成，不在等待态展示。 */
	constexpr float GameplayVisibleWaitingProgressCap = 99.0f;

	// 地图包进度映射流程：把引擎 LoadPackageAsync 的真实百分比映射到进入游戏总进度的包加载区间，不把时间或阶段动画混入连续进度。
	static float MapPackageProgressToGameplayProgress(const float PackagePercent)
	{
		const float ClampedPackagePercent = FMath::Clamp(PackagePercent, 0.0f, 100.0f);
		return FMath::Lerp(GameplayPackageProgressStart, GameplayPackageProgressEnd, ClampedPackagePercent / 100.0f);
	}

	// 地图包百分比展示流程：只要 LoadPackageAsync 还没触发完成回调，哪怕引擎采样已经接近 100，也不把等待态写成完成态。
	static float ClampVisiblePackageProgressWhilePending(const float PackagePercent)
	{
		return FMath::Min(FMath::Clamp(PackagePercent, 0.0f, 100.0f), 99.0f);
	}

	// Start 等待识别流程：只认 Online 明确的 Start、玩法包预载、玩法旅行排队或正在前往 Lake 的状态。
	static bool IsGameplayStartLoadingSnapshot(const FCatOnlineSnapshot& Snapshot)
	{
		return (Snapshot.ActiveOperation == ECatOnlineOperation::Start && Snapshot.RequestId.IsValid())
			|| Snapshot.bIsGameplayLoadPending
			|| (Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
				&& Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake)
			|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake;
	}

	// Leave 等待识别流程：只认 Online 明确的 Leave、前台包预载、回 Frontend 旅行排队或正在回主菜单的状态。
	static bool IsReturnToFrontendLoadingSnapshot(const FCatOnlineSnapshot& Snapshot)
	{
		return (Snapshot.ActiveOperation == ECatOnlineOperation::Leave && Snapshot.RequestId.IsValid())
			|| (Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
				&& Snapshot.WorldState == ECatOnlineWorldState::TravelingToFrontend)
			|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToFrontend;
	}
}

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
	HideGlobalLoadingScreen();
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
// 1. 先按当前 Online 快照判断已有 Frontend Root 是否只处于 Start 失败恢复保护窗。
// 2. 局内 HUD、背包和交互提示始终拆掉，因为它们绑定旧 Pawn 和输入；受保护的 Frontend Root 不在这里移除。
// 3. 父类完成 LocalPlayer 的 Controller 切换后重新绑定新 Controller，并在保留 Root 时显式恢复拥有者、鼠标和键盘焦点。
// 4. 最后重新调和 Frontend 和全局加载遮罩，非受保护状态会按常规 World/配置规则移除或重建。
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
// 1. 先读取当前 LocalPlayer、Controller 和 Online 快照；缺 LocalPlayer 或 Online 时拆掉 Root 与全局遮罩，避免旧 UI 脱离事实源继续显示。
// 2. 全局遮罩先消费 Start/Leave 快照并独立入视口；Root 不再为了加载表现跨 World 保留。
// 3. 没有本地 Controller 时只允许已有 Root 在 Start 失败恢复保护窗内短暂保留；其它情况立即拆除。
// 4. 非 Frontend World 只保留受保护的已有 Root，不在玩法图或异常 World 补建新主界面，避免旧前端变成第二入口。
// 5. 已有 Root 直接复用；需要新建时必须仍处于 Frontend World，并且配置能加载正式 Root WBP，否则记录失败并保持无原生替身。
// 6. 创建成功后装配 Root、PageController 和三个只读 Model，最后入视口、打开鼠标并设置键盘焦点。
void UCatLocalPlayerUISubsystem::RefreshFrontendForCurrentController()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!LocalPlayer || !Online)
	{
		HideGlobalLoadingScreen();
		RemoveFrontendRoot();
		return;
	}
	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	RefreshGlobalLoadingScreen(Snapshot);
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
	RefreshGlobalLoadingScreen(Online->GetSnapshot());
}

// 全局遮罩刷新流程：先记录 Start/Leave 的真实过渡意图，再按 Online、引擎和本地 UI 就绪事实决定显示；没有真实等待原因时立刻释放遮罩。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot)
{
	TrackGlobalLoadingTransition(Snapshot);
	FCatGlobalLoadingPresentation Presentation;
	if (ShouldShowGlobalLoadingScreen(Snapshot, Presentation))
	{
		ShowGlobalLoadingScreen(Presentation);
		return;
	}
	HideGlobalLoadingScreen();
}

// 当前快照刷新流程：从本 LocalPlayer 的 GameInstance 找回 Online 子系统并复用主遮罩刷新入口；缺事实源时清掉过渡记忆，避免旧请求跨 World 继续显示。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreenFromCurrentSnapshot()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		GlobalLoadingOperation = ECatOnlineOperation::None;
		GlobalLoadingRequestId.Invalidate();
		HideGlobalLoadingScreen();
		return;
	}
	RefreshGlobalLoadingScreen(Online->GetSnapshot());
}

// 全局遮罩 gate 流程：
// 1. 错误快照直接放行，让业务页面显示失败原因，不把失败藏在遮罩下面。
// 2. 进入游戏时显示总进度；Start、地图包、Travel、World、BeginPlay、Connected 和本地 UI 都只按真实 gate 推进。
// 3. 返回主菜单时不显示进度条，只展示保存、拆局、销毁房间、切图和主菜单 UI 就绪这些真实步骤。
// 4. Start/Leave 操作已结案但 UI 尚未就绪时，依靠本地过渡记忆继续显示，直到真实就绪事件触发刷新。
bool UCatLocalPlayerUISubsystem::ShouldShowGlobalLoadingScreen(
	const FCatOnlineSnapshot& Snapshot, FCatGlobalLoadingPresentation& OutPresentation) const
{
	OutPresentation = FCatGlobalLoadingPresentation();
	if (Snapshot.LastError != ECatOnlineError::None)
	{
		return false;
	}
	const bool bGameplayStartPending = CatLocalPlayerUILoadingScreen::IsGameplayStartLoadingSnapshot(Snapshot)
		|| GlobalLoadingOperation == ECatOnlineOperation::Start;
	const bool bReturnToMenuPending = CatLocalPlayerUILoadingScreen::IsReturnToFrontendLoadingSnapshot(Snapshot)
		|| GlobalLoadingOperation == ECatOnlineOperation::Leave;
	if (bGameplayStartPending && !IsGameplayLoadingReadyToDismiss(Snapshot))
	{
		OutPresentation.HeadingText = FText::FromString(TEXT("正在进入游戏"));
		OutPresentation.bShowProgressBar = true;
		OutPresentation.bHasProgressPercent = true;
		OutPresentation.ProgressPercent = GetGameplayLoadingProgressPercent(Snapshot);
		if (Snapshot.bIsGameplayLoadPending)
		{
			const float VisiblePackagePercent = CatLocalPlayerUILoadingScreen::ClampVisiblePackageProgressWhilePending(
				Snapshot.MapLoadProgressPercent);
			const int32 PackageDisplayPercent = FMath::RoundToInt(VisiblePackagePercent);
			OutPresentation.StatusText = FText::FromString(TEXT("正在读取游戏世界"));
			OutPresentation.DetailText = Snapshot.bHasMapLoadProgress
				? FText::FromString(FString::Printf(TEXT("地图包加载 %d%%"), PackageDisplayPercent))
				: FText::FromString(TEXT("等待引擎提供地图包进度。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 LoadPackageAsync 完成。"));
			return true;
		}
		FText EngineStatusText;
		FText EngineDetailText;
		FText EngineReasonText;
		if (TryGetEngineLoadingReason(Snapshot, false, EngineStatusText, EngineDetailText, EngineReasonText))
		{
			OutPresentation.StatusText = EngineStatusText;
			OutPresentation.DetailText = EngineDetailText;
			OutPresentation.ReasonText = EngineReasonText;
			return true;
		}
		if (Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
			|| Snapshot.TransportState == ECatOnlineTransportState::TravelQueued)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在切换到游戏世界。"));
			OutPresentation.DetailText = FText::FromString(TEXT("等待 PostLoadMap 确认玩法地图。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 ServerTravel 或 ClientTravel 完成。"));
			return true;
		}
		OutPresentation.StatusText = FText::FromString(TEXT("正在准备玩家界面。"));
		if (!BoundPlayerController.IsValid())
		{
			OutPresentation.DetailText = FText::FromString(TEXT("等待本地玩家控制器。"));
		}
		else if (!Cast<ACatCharacter>(BoundPlayerController->GetPawn()))
		{
			OutPresentation.DetailText = FText::FromString(TEXT("等待本地玩家 Pawn。"));
		}
		else if (!HUDWidget || !HUDWidget->IsInViewport())
		{
			OutPresentation.DetailText = FText::FromString(TEXT("等待 HUD 入视口。"));
		}
		else if (!InventoryPageController || !LakeMainMenuController || !InteractionPageController)
		{
			OutPresentation.DetailText = FText::FromString(TEXT("等待局内 UI 控制器装配完成。"));
		}
		else
		{
			OutPresentation.DetailText = FText::FromString(TEXT("等待玩家 UI 就绪确认。"));
		}
		OutPresentation.ReasonText = FText::FromString(TEXT("等待 LocalPlayer UI 装配完成。"));
		return true;
	}
	if (bReturnToMenuPending && !IsFrontendLoadingReadyToDismiss(Snapshot))
	{
		OutPresentation.HeadingText = FText::FromString(TEXT("正在返回主菜单"));
		OutPresentation.bShowProgressBar = false;
		if (Snapshot.SessionState == ECatOnlineSessionState::Destroying)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在关闭当前房间。"));
			OutPresentation.DetailText = FText::FromString(TEXT("等待 DestroySession 完成回调。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待平台 Session 销毁确认。"));
			return true;
		}
		if (Snapshot.ActiveOperation == ECatOnlineOperation::Leave && Snapshot.WorldState == ECatOnlineWorldState::Lake)
		{
			OutPresentation.StatusText = Snapshot.SessionRole == ECatOnlineSessionRole::Host
				? FText::FromString(TEXT("正在保存并收尾当前游戏。"))
				: FText::FromString(TEXT("正在离开当前游戏。"));
			OutPresentation.DetailText = Snapshot.SessionRole == ECatOnlineSessionRole::Host
				? FText::FromString(TEXT("等待存档和本局收尾完成。"))
				: FText::FromString(TEXT("等待客户端离开当前会话。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 Leave 链路进入 DestroySession 或前台旅行。"));
			return true;
		}
		if (Snapshot.bIsMapPreloadPending && Snapshot.WorldState == ECatOnlineWorldState::TravelingToFrontend)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在读取主菜单世界。"));
			OutPresentation.DetailText = FText::FromString(TEXT("等待 Frontend 地图包预载完成。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 LoadPackageAsync 完成。"));
			return true;
		}
		FText EngineStatusText;
		FText EngineDetailText;
		FText EngineReasonText;
		if (TryGetEngineLoadingReason(Snapshot, true, EngineStatusText, EngineDetailText, EngineReasonText))
		{
			OutPresentation.StatusText = EngineStatusText;
			OutPresentation.DetailText = EngineDetailText;
			OutPresentation.ReasonText = EngineReasonText;
			return true;
		}
		if (Snapshot.WorldState == ECatOnlineWorldState::TravelingToFrontend
			|| Snapshot.TransportState == ECatOnlineTransportState::TravelQueued)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在切换到主菜单。"));
			OutPresentation.DetailText = FText::FromString(TEXT("等待 PostLoadMap 确认 Frontend 世界。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 ServerTravel 或 ClientTravel 完成。"));
			return true;
		}
		OutPresentation.StatusText = FText::FromString(TEXT("正在创建主菜单界面。"));
		OutPresentation.DetailText = FText::FromString(TEXT("等待 Frontend Root 进入视口。"));
		OutPresentation.ReasonText = FText::FromString(TEXT("等待 LocalPlayer Frontend UI 装配完成。"));
		return true;
	}
	return false;
}

// 全局遮罩显示流程：优先复用当前实例；首次显示时用 GameInstance 创建正式 Loading WBP 并加到最高层，随后写入已经合成好的表现快照。
void UCatLocalPlayerUISubsystem::ShowGlobalLoadingScreen(const FCatGlobalLoadingPresentation& Presentation)
{
	if (!GlobalLoadingScreenWidget)
	{
		const TSubclassOf<UUserWidget> LoadingClass = LoadClass<UUserWidget>(nullptr, CatLocalPlayerUILoadingScreen::WidgetClassPath);
		UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
		if (!LoadingClass || !GameInstance)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_global_loading_screen_unavailable Class=%s GameInstance=%s"),
				CatLocalPlayerUILoadingScreen::WidgetClassPath,
				*GetNameSafe(GameInstance));
			return;
		}
		GlobalLoadingScreenWidget = CreateWidget<UUserWidget>(GameInstance, LoadingClass);
		if (!GlobalLoadingScreenWidget)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=ui_global_loading_screen_create_failed Class=%s"),
				*GetNameSafe(LoadingClass.Get()));
			return;
		}
		GlobalLoadingScreenWidget->AddToViewport(CatLocalPlayerUILoadingScreen::ViewportZOrder);
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_global_loading_screen_shown Class=%s Status=\"%s\""),
			*GetNameSafe(LoadingClass.Get()), *Presentation.StatusText.ToString());
	}
	else if (!GlobalLoadingScreenWidget->IsInViewport())
	{
		GlobalLoadingScreenWidget->AddToViewport(CatLocalPlayerUILoadingScreen::ViewportZOrder);
	}
	RefreshGlobalLoadingScreenPresentation(Presentation);
}

// 全局遮罩隐藏流程：先清掉本地 Start/Leave 过渡记忆，再移除最高层 WBP 和文本缓存；Online 终态和错误展示仍由对应 Controller/Model 自己处理。
void UCatLocalPlayerUISubsystem::HideGlobalLoadingScreen()
{
	GlobalLoadingOperation = ECatOnlineOperation::None;
	GlobalLoadingRequestId.Invalidate();
	if (!GlobalLoadingScreenWidget)
	{
		LastGlobalLoadingStatusText = FText::GetEmpty();
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_global_loading_screen_hidden LastStatus=\"%s\""),
		*LastGlobalLoadingStatusText.ToString());
	GlobalLoadingScreenWidget->RemoveFromParent();
	GlobalLoadingScreenWidget = nullptr;
	LastGlobalLoadingStatusText = FText::GetEmpty();
}

// 全局遮罩表现刷新流程：
// 1. 先写高层目标和当前真实步骤，让玩家能看到正在等保存、销毁房间、切图还是 UI 装配。
// 2. 进入游戏时显示由真实 gate 合成的总进度并追加百分号；总进度来自状态事实，不来自倒计时或动画时长。
// 3. 返回主菜单会折叠进度条，只更新文字状态；后续可由资产侧替换成旋转动画，代码不做定时器兜底。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreenPresentation(const FCatGlobalLoadingPresentation& Presentation)
{
	if (!GlobalLoadingScreenWidget)
	{
		return;
	}
	LastGlobalLoadingStatusText = Presentation.StatusText;
	const float ClampedPercent = FMath::Clamp(Presentation.ProgressPercent, 0.0f, 100.0f);
	const int32 DisplayPercent = FMath::RoundToInt(ClampedPercent);
	const FText EffectiveStatusText = Presentation.StatusText.IsEmpty()
		? FText::FromString(TEXT("正在加载。")) : Presentation.StatusText;
	if (UTextBlock* StatusTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingProgressTextBlock"))))
	{
		StatusTextBlock->SetText(Presentation.bHasProgressPercent
			? FText::FromString(FString::Printf(TEXT("%s %d%%"), *EffectiveStatusText.ToString(), DisplayPercent))
			: EffectiveStatusText);
	}
	if (UTextBlock* DayTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingDayTextBlock"))))
	{
		DayTextBlock->SetText(Presentation.HeadingText.IsEmpty()
			? FText::FromString(TEXT("正在切换世界")) : Presentation.HeadingText);
	}
	if (UTextBlock* SacrificeTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingSacrificeProgressTextBlock"))))
	{
		SacrificeTextBlock->SetText(Presentation.DetailText.IsEmpty()
			? FText::FromString(TEXT("等待当前步骤完成。")) : Presentation.DetailText);
	}
	if (UTextBlock* HintTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingHintText"))))
	{
		HintTextBlock->SetText(Presentation.ReasonText.IsEmpty()
			? FText::FromString(TEXT("等待真实加载状态更新。")) : Presentation.ReasonText);
	}
	if (UProgressBar* ProgressBar = Cast<UProgressBar>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingProgressBar"))))
	{
		ProgressBar->SetVisibility(Presentation.bShowProgressBar ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		ProgressBar->SetIsMarquee(false);
		ProgressBar->SetPercent(Presentation.bShowProgressBar ? ClampedPercent / 100.0f : 0.0f);
	}
}

// 进入游戏总进度合成流程：
// 1. 先用 Online 的 Start/预载/旅行事实确认请求至少已经受理。
// 2. 地图包阶段只消费引擎给出的真实 LoadPackageAsync 百分比，并映射到总进度的包加载区间。
// 3. 后续 Travel、LoadMap、World 到达、BeginPlay、Transport Connected、Controller/Pawn/HUD 和页面控制器只在对应真实 gate 成立时离散推进。
// 4. 只要遮罩还没被 ready 条件收起，显示值就封顶在 99，避免玩家看到 100% 仍在等待。
float UCatLocalPlayerUISubsystem::GetGameplayLoadingProgressPercent(const FCatOnlineSnapshot& Snapshot) const
{
	using namespace CatLocalPlayerUILoadingScreen;
	float ProgressPercent = GameplayStartAcceptedProgress;
	if (Snapshot.bIsGameplayLoadPending)
	{
		ProgressPercent = Snapshot.bHasMapLoadProgress
			? MapPackageProgressToGameplayProgress(Snapshot.MapLoadProgressPercent)
			: GameplayPackageProgressStart;
		return FMath::Min(ProgressPercent, GameplayVisibleWaitingProgressCap);
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	const UWorld* World = WorldContext ? WorldContext->World() : nullptr;
	if (Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
		|| Snapshot.WorldState == ECatOnlineWorldState::Lake
		|| Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
		|| Snapshot.TransportState == ECatOnlineTransportState::Connected)
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayTravelQueuedProgress);
	}
	if (Snapshot.bIsEngineLoadMapPending
		|| (WorldContext && !WorldContext->TravelURL.IsEmpty())
		|| (WorldContext && WorldContext->PendingNetGame != nullptr))
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayEngineTravelProgress);
	}
	if (Snapshot.WorldState == ECatOnlineWorldState::Lake)
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayWorldReachedProgress);
		if (World && World->GetGameState<AGameStateBase>() && World->HasBegunPlay() && !World->IsInSeamlessTravel())
		{
			ProgressPercent = FMath::Max(ProgressPercent, GameplayWorldRunningProgress);
		}
	}
	if (Snapshot.TransportState == ECatOnlineTransportState::Connected)
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayTransportConnectedProgress);
	}
	APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	if (Controller && Controller->IsLocalController())
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayLocalControllerProgress);
	}
	if (Character)
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayLocalPawnProgress);
	}
	if (HUDWidget && HUDWidget->IsInViewport())
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayHudVisibleProgress);
	}
	if (InventoryPageController && LakeMainMenuController && InteractionPageController
		&& AttachedPlayerLakeCharacter.Get() == Character)
	{
		ProgressPercent = FMath::Max(ProgressPercent, GameplayLocalUIReadyProgress);
	}
	return FMath::Min(ProgressPercent, GameplayVisibleWaitingProgressCap);
}

// 过渡记忆流程：Start/Leave 的真实快照出现时记录当前请求；错误或真实就绪时清空，避免 Online 结案早于 UI 就绪导致遮罩提前消失。
void UCatLocalPlayerUISubsystem::TrackGlobalLoadingTransition(const FCatOnlineSnapshot& Snapshot)
{
	if (Snapshot.LastError != ECatOnlineError::None)
	{
		GlobalLoadingOperation = ECatOnlineOperation::None;
		GlobalLoadingRequestId.Invalidate();
		return;
	}
	if (CatLocalPlayerUILoadingScreen::IsGameplayStartLoadingSnapshot(Snapshot))
	{
		GlobalLoadingOperation = ECatOnlineOperation::Start;
		GlobalLoadingRequestId = Snapshot.RequestId;
	}
	else if (CatLocalPlayerUILoadingScreen::IsReturnToFrontendLoadingSnapshot(Snapshot))
	{
		GlobalLoadingOperation = ECatOnlineOperation::Leave;
		GlobalLoadingRequestId = Snapshot.RequestId;
	}
	if (GlobalLoadingOperation == ECatOnlineOperation::Start && IsGameplayLoadingReadyToDismiss(Snapshot))
	{
		GlobalLoadingOperation = ECatOnlineOperation::None;
		GlobalLoadingRequestId.Invalidate();
	}
	else if (GlobalLoadingOperation == ECatOnlineOperation::Leave && IsFrontendLoadingReadyToDismiss(Snapshot))
	{
		GlobalLoadingOperation = ECatOnlineOperation::None;
		GlobalLoadingRequestId.Invalidate();
	}
}

// 进入玩法收口流程：只有 Online 同时确认目标世界是 Lake、Transport 已 Connected，且本地 Controller 的猫 Pawn、HUD、局内菜单和交互控制器都装配好，遮罩才允许消失。
bool UCatLocalPlayerUISubsystem::IsGameplayLoadingReadyToDismiss(const FCatOnlineSnapshot& Snapshot) const
{
	if (Snapshot.WorldState != ECatOnlineWorldState::Lake
		|| Snapshot.TransportState != ECatOnlineTransportState::Connected
		|| Snapshot.bIsGameplayLoadPending
		|| Snapshot.bIsMapPreloadPending
		|| Snapshot.bIsEngineLoadMapPending)
	{
		return false;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	return Controller && Controller->IsLocalController()
		&& Character
		&& AttachedPlayerLakeCharacter.Get() == Character
		&& HUDWidget && HUDWidget->IsInViewport()
		&& InventoryPageController
		&& LakeMainMenuController
		&& InteractionPageController;
}

// 回主菜单收口流程：只有 Online 同时确认目标世界是 Frontend、Transport 已 Idle，地图包与 LoadMap 都不再 pending，并且正式 Frontend Root 已入视口，遮罩才允许消失。
bool UCatLocalPlayerUISubsystem::IsFrontendLoadingReadyToDismiss(const FCatOnlineSnapshot& Snapshot) const
{
	return Snapshot.WorldState == ECatOnlineWorldState::Frontend
		&& Snapshot.TransportState == ECatOnlineTransportState::Idle
		&& !Snapshot.bIsMapPreloadPending
		&& !Snapshot.bIsEngineLoadMapPending
		&& FrontendRootWidget
		&& FrontendRootWidget->IsInViewport();
}

// Lyra 式引擎 gate 读取流程：按 WorldContext、LoadMap、TravelURL、PendingNetGame、GameState、BeginPlay、SeamlessTravel 的真实顺序检查；命中后返回玩家文案和工程锚点。
bool UCatLocalPlayerUISubsystem::TryGetEngineLoadingReason(const FCatOnlineSnapshot& Snapshot,
	const bool bReturningToFrontend, FText& OutStatusText, FText& OutDetailText, FText& OutReasonText) const
{
	OutStatusText = FText::GetEmpty();
	OutDetailText = FText::GetEmpty();
	OutReasonText = FText::GetEmpty();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	if (!WorldContext)
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在恢复主菜单上下文。"))
			: FText::FromString(TEXT("正在准备游戏上下文。"));
		OutDetailText = FText::FromString(TEXT("等待 GameInstance WorldContext。"));
		OutReasonText = FText::FromString(TEXT("WorldContext 尚未可用。"));
		return true;
	}
	UWorld* World = WorldContext->World();
	if (!World)
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在创建主菜单世界。"))
			: FText::FromString(TEXT("正在创建游戏世界。"));
		OutDetailText = FText::FromString(TEXT("等待 World 对象创建完成。"));
		OutReasonText = FText::FromString(TEXT("FWorldContext::World 为空。"));
		return true;
	}
	if (Snapshot.bIsEngineLoadMapPending)
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在加载主菜单世界。"))
			: FText::FromString(TEXT("正在加载游戏世界。"));
		OutDetailText = Snapshot.EngineLoadMapName.IsEmpty()
			? FText::FromString(TEXT("等待引擎 LoadMap 完成。"))
			: FText::FromString(FString::Printf(TEXT("等待引擎 LoadMap 完成：%s"), *Snapshot.EngineLoadMapName));
		OutReasonText = FText::FromString(TEXT("PreLoadMap 已触发，等待 PostLoadMap。"));
		return true;
	}
	if (!WorldContext->TravelURL.IsEmpty())
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在提交回主菜单的旅行。"))
			: FText::FromString(TEXT("正在提交进入游戏的旅行。"));
		OutDetailText = FText::FromString(TEXT("等待引擎清空 TravelURL。"));
		OutReasonText = FText::FromString(TEXT("WorldContext TravelURL 仍未清空。"));
		return true;
	}
	if (WorldContext->PendingNetGame != nullptr)
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在切换网络连接。"))
			: FText::FromString(TEXT("正在连接房主。"));
		OutDetailText = FText::FromString(TEXT("等待 PendingNetGame 完成。"));
		OutReasonText = FText::FromString(TEXT("PendingNetGame 仍存在。"));
		return true;
	}
	if (!World->GetGameState<AGameStateBase>())
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在同步主菜单世界状态。"))
			: FText::FromString(TEXT("正在同步游戏世界状态。"));
		OutDetailText = FText::FromString(TEXT("等待 GameState 创建或复制。"));
		OutReasonText = FText::FromString(TEXT("GameState 尚不可用。"));
		return true;
	}
	if (!World->HasBegunPlay())
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在启动主菜单世界。"))
			: FText::FromString(TEXT("正在启动游戏世界。"));
		OutDetailText = FText::FromString(TEXT("等待 World BeginPlay。"));
		OutReasonText = FText::FromString(TEXT("World 尚未 BeginPlay。"));
		return true;
	}
	if (World->IsInSeamlessTravel())
	{
		OutStatusText = bReturningToFrontend
			? FText::FromString(TEXT("正在无缝返回主菜单。"))
			: FText::FromString(TEXT("正在无缝切换游戏世界。"));
		OutDetailText = FText::FromString(TEXT("等待无缝旅行完成。"));
		OutReasonText = FText::FromString(TEXT("World 仍处于 SeamlessTravel。"));
		return true;
	}
	return false;
}

// 已有 Root 保留判断流程：
// 1. 没有 Root 时直接返回 false；该策略只保护 Start 失败后仍需要显示错误的既有前端，不负责补建任何非 Frontend World UI。
// 2. Start 加载和旅行期间由全局遮罩接管，Root 不再跨 World 保留，避免 Root 局部页面成为第二套表现。
// 3. Start 失败恢复只在已有 Root 上成立，让错误文本能回到 Frontend/Room；其它 Lake 或无关 World 继续走拆除路径。
bool UCatLocalPlayerUISubsystem::ShouldKeepExistingFrontendRoot(const FCatOnlineSnapshot& Snapshot) const
{
	if (!FrontendRootWidget)
	{
		return false;
	}
	const bool bGameplayStartFailureRecovering = Snapshot.ActiveOperation == ECatOnlineOperation::None
		&& (Snapshot.LastError == ECatOnlineError::GameplayPreloadFailed || Snapshot.LastError == ECatOnlineError::TravelRejected
			|| Snapshot.LastError == ECatOnlineError::TravelFailed || Snapshot.LastError == ECatOnlineError::ConnectStringUnavailable
			|| Snapshot.LastError == ECatOnlineError::NetworkFailure || Snapshot.LastError == ECatOnlineError::ClientStartRetryExhausted)
		&& (Snapshot.WorldState == ECatOnlineWorldState::Frontend || Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
			|| Snapshot.TransportState == ECatOnlineTransportState::Failed);
	return bGameplayStartFailureRecovering;
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
// 1. 先把 NewPawn 裁成项目猫身体；同一个已装配身体的重复通知只刷新输入绑定，库存和菜单数据继续等自己的读源广播。
// 2. 新身体或空身体会先完整拆掉上一套本地玩家 UI，避免跨 Pawn 复用 Model、View 或输入锁。
// 3. 只有新的 ACatCharacter 通过配置校验时才重新装配 HUD、背包、交互提示和拾取提示层。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	ACatCharacter* Character = Cast<ACatCharacter>(NewPawn);
	if (Character && AttachedPlayerLakeCharacter.Get() == Character
		&& HUDWidget && InventoryPageController && LakeMainMenuController && InteractionPageController)
	{
		InventoryPageController->RefreshInputBinding();
		LakeMainMenuController->RefreshInputBinding();
		RefreshGlobalLoadingScreenFromCurrentSnapshot();
		return;
	}
	DetachPlayerLakeUI();
	if (!Character)
	{
		RefreshGlobalLoadingScreenFromCurrentSnapshot();
		return;
	}
	AttachPlayerLakeUI(Character);
	if (HUDWidget && InventoryPageController && LakeMainMenuController && InteractionPageController)
	{
		AttachedPlayerLakeCharacter = Character;
	}
	RefreshGlobalLoadingScreenFromCurrentSnapshot();
}

// 本地玩家 UI 装配流程：
// 1. 验证本地设置、当前 Controller/Pawn 和 World；任一正式 WBP 类缺失或无效时直接 fail-closed，不创建脱离项目资产的原生替身。
// 2. 创建 HUD Model/View 并入视口；默认常驻天数、背包入口、设置入口和中心准星，背包内容由库存页打开后再显示。
// 3. 创建 Inventory Model/PageController/普通背包 View，但背包 View 不预先入视口，只通过既有 InputContext 的 Action 打开。
// 4. 创建局内主菜单 View/Controller；菜单不常驻视口，只在主菜单 Action 或 HUD 按钮触发时打开。
// 5. 创建 Interaction 提示 View 和控制器；控制器订阅 PlayerController 的唯一准星交互目标，商店、鱼护和未来箱子仍由世界交互对象提供页面上下文。
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
	const TSubclassOf<UCatLakeMainMenuWidget> LakeMainMenuViewClass = Settings->LoadLakeMainMenuWidgetClass();
	if (!HUDViewClass || !InventoryViewClass || !InventorySlotViewClass || !InteractionPromptViewClass
		|| !LakeMainMenuViewClass)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_player_module_class_missing HUD=%s Inventory=%s Slot=%s Interaction=%s LakeMenu=%s"),
			*Settings->HUDWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventoryWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventorySlotWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InteractionPromptWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->LakeMainMenuWidgetClass.ToSoftObjectPath().ToString());
		return;
	}

	HUDModel = NewObject<UCatHUDModel>(this);
	HUDWidget = CreateWidget<UCatHUDWidget>(Controller, HUDViewClass);
	InventoryModel = NewObject<UCatInventoryModel>(this);
	InventoryPageController = NewObject<UCatInventoryPageController>(this);
	InventoryWidget = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	LakeMainMenuController = NewObject<UCatLakeMainMenuController>(this);
	LakeMainMenuWidget = CreateWidget<UCatLakeMainMenuWidget>(Controller, LakeMainMenuViewClass);
	InteractionPageController = NewObject<UCatInteractionPageController>(this);
	InteractionPromptWidget = CreateWidget<UCatInteractionPromptWidget>(Controller, InteractionPromptViewClass);
	if (!HUDModel || !HUDWidget || !InventoryModel || !InventoryPageController || !InventoryWidget
		|| !LakeMainMenuController || !LakeMainMenuWidget || !InteractionPageController || !InteractionPromptWidget)
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
		|| !InventoryPageController->Bind(GetLocalPlayer(), Controller, InventoryModel, InventoryWidget)
		|| !LakeMainMenuController->Bind(GetLocalPlayer(), Controller, LakeMainMenuWidget))
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
		TEXT("Event=ui_player_modules_attached World=%s NetMode=%d LocalPlayerIndex=%d Controller=%s LocalController=%s HUD=%s HUDMode=minimal_main Inventory=%s Slot=%s LakeMenu=%s Interaction=%s ShopPrecreated=false"),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		LocalPlayer ? LocalPlayer->GetLocalPlayerIndex() : INDEX_NONE,
		*GetNameSafe(Controller),
		Controller->IsLocalController() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(HUDWidget->GetClass()),
		*GetNameSafe(InventoryWidget->GetClass()),
		*GetNameSafe(InventorySlotViewClass.Get()),
		*GetNameSafe(LakeMainMenuWidget ? LakeMainMenuWidget->GetClass() : nullptr),
		*GetNameSafe(InteractionPromptWidget ? InteractionPromptWidget->GetClass() : nullptr));
}

// 本地玩家 UI 解绑流程：PageController 先恢复输入并移出当前模态页，Model 再解除玩法订阅，最后移除各自 WBP 并清引用。
void UCatLocalPlayerUISubsystem::DetachPlayerLakeUI()
{
	if (LakeMainMenuController)
	{
		LakeMainMenuController->Unbind();
		LakeMainMenuController = nullptr;
	}
	if (LakeMainMenuWidget)
	{
		LakeMainMenuWidget->RemoveFromParent();
		LakeMainMenuWidget = nullptr;
	}
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

// 局内菜单切换流程：打开菜单前先关闭当前背包页面，保证同一 Controller 上只有一个模态输入恢复记录处于打开状态。
void UCatLocalPlayerUISubsystem::ToggleLakeMainMenu()
{
	if (!LakeMainMenuController)
	{
		return;
	}
	if (!LakeMainMenuController->IsMenuOpen() && InventoryPageController && InventoryPageController->IsInventoryOpen())
	{
		InventoryPageController->RequestCloseInventoryFromWidget();
	}
	LakeMainMenuController->ToggleMenu();
}

// HUD 入口动作流程：背包和主菜单都转交已有控制器；HUD 不兜底拼页面，也不持有保存或离局业务。
void UCatLocalPlayerUISubsystem::HandleHUDActionRequested(const ECatHUDAction Action)
{
	switch (Action)
	{
	case ECatHUDAction::OpenInventory:
		ToggleInventory();
		break;
	case ECatHUDAction::OpenMainMenu:
		ToggleLakeMainMenu();
		break;
	default:
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_hud_action_unknown Action=%d"), static_cast<int32>(Action));
		break;
	}
}
