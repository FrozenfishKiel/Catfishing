#include "UI/CatLocalPlayerUISubsystem.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "UI/Run/CatDayTransitionWidget.h"

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
#include "Framework/Application/SlateApplication.h"
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

	/** Start 请求已被 Online 接受这一真实 gate 在总进度中的权重；它只在请求事实存在时计入。 */
	constexpr float GameplayStartAcceptedWeight = 3.0f;

	/** 玩法启动软资源预热在总进度中的权重；这一段按 StreamableHandle 的真实加载进度推进。 */
	constexpr float GameplayStartupAssetWeight = 30.0f;

	/** 地图包异步加载在总进度中的权重；这一段按引擎 LoadPackageAsync 的真实进度事件推进。 */
	constexpr float GameplayPackageWeight = 35.0f;

	/** ServerTravel 或 ClientTravel 已提交这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayTravelSubmittedWeight = 6.0f;

	/** 引擎切图阻塞已结束这一真实 gate 在总进度中的权重；它不靠等待时长推断。 */
	constexpr float GameplayEngineTravelCompleteWeight = 8.0f;

	/** Online 已确认玩法 World 到达这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayWorldReachedWeight = 6.0f;

	/** 玩法 World 已经 GameState 可用且 BeginPlay 这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayWorldRunningWeight = 4.0f;

	/** Online 运输事实已收口为 Connected 这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayTransportConnectedWeight = 3.0f;

	/** 本地玩家 Controller 已绑定这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayLocalControllerWeight = 1.0f;

	/** 本地猫 Pawn 已出现这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayLocalPawnWeight = 1.0f;

	/** HUD 已加入视口这一真实 gate 在总进度中的权重。 */
	constexpr float GameplayHudVisibleWeight = 1.0f;

	/** 局内 UI 控制器已装配这一真实 gate 在总进度中的权重；到这里再加前面 gate 会自然得到 100。 */
	constexpr float GameplayLocalUIReadyWeight = 2.0f;

	// 地图包进度贡献流程：把 Online 从 LoadPackageAsync 事件得到的包内阶段百分比换成本项目总进度中的贡献值，显示层后续按合成结果直接写入。
	static float GetPackageProgressContribution(const float PackagePercent)
	{
		return GameplayPackageWeight * (PackagePercent / 100.0f);
	}

	// 启动软资源进度贡献流程：把 Online 从 StreamableHandle 读取到的真实百分比换成本项目总进度中的贡献值，显示层不做补值或限速。
	static float GetStartupAssetProgressContribution(const float AssetPercent)
	{
		return GameplayStartupAssetWeight * (AssetPercent / 100.0f);
	}

	// Start 等待识别流程：只认 Online 明确的 Start、玩法启动资源预热、地图包预载、玩法旅行排队或正在前往 Lake 的状态。
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

// 销毁流程：先释放 HUD、背包和交互提示模块；Controller 解绑时移除翻天表现，最后清理 Frontend Root 与 Online 快照。
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
// 2. 局内 HUD、背包和交互提示始终拆掉，因为它们绑定已解绑 Pawn 和输入；受保护的 Frontend Root 不在这里移除。
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

// 背包切换流程：先拒绝翻天期间的 HUD 调用，再由窗口控制器管理视口与输入；库存仍由自己的 Model 通知。
void UCatLocalPlayerUISubsystem::ToggleInventory()
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return;
	if (InventoryPageController)
	{
		InventoryPageController->ToggleInventory();
	}
}

// 打开流程：先拒绝翻天中的交互请求，再验证页面控制器；有效时用对象提供的库存和 WBP 打开页面，不转移 Model 所有权。
bool UCatLocalPlayerUISubsystem::OpenInventory(UCatInventoryComponent* Inventory,
    const TSubclassOf<UCatInventoryWidget> InventoryViewClass)
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return false;
    if (!InventoryPageController)
    {
        UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_open_rejected Reason=PageControllerUnavailable ViewClass=%s"),
            *GetNameSafe(InventoryViewClass.Get()));
        return false;
    }
    return InventoryPageController->OpenInventory(Inventory, InventoryViewClass);
}

// 状态读取流程：从 PageController 读取唯一背包状态；未装配背包时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsInventoryOpen() const
{
	return InventoryPageController ? InventoryPageController->IsInventoryOpen() : false;
}

// 翻天表现流程：
// 1. 只接收本 LocalPlayer 的当前 Controller；锁定期关闭库存和菜单，切断格子直接 use 与模态按键入口。
// 2. 用服务器时间计算淡出、停留、淡入；迟到或超时快照的黑色透明度为零，操作阻断仍服从服务器 active。
// 3. 提交后才展示服务器 Message 或目标天数；失败只按 RequestId 开启一次两秒提示，且不占用操作锁。
// 4. 仅在有锁或提示时创建原生 UI；普通结束移除视图但保留失败去重键，旅行则由 ClearDayTransition 完整清理。
void UCatLocalPlayerUISubsystem::RefreshDayTransition(APlayerController* Controller,
	const FCatRunDayTransition& Transition, const double ServerTimeSeconds)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!Controller || !LocalPlayer || Controller != LocalPlayer->GetPlayerController(GetWorld())) return;
	const bool bBlocked = Transition.bActive && !Transition.bFailed;
	if (bBlocked)
	{
		if (InventoryPageController && InventoryPageController->IsInventoryOpen())
		{
			InventoryPageController->RequestCloseInventoryFromWidget();
		}
		if (LakeMainMenuController && LakeMainMenuController->IsMenuOpen())
		{
			LakeMainMenuController->RequestCloseFromWidget();
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Transition.bFailed && Transition.RequestId.IsValid() && Transition.RequestId != LastDayTransitionFailureId)
	{
		LastDayTransitionFailureId = Transition.RequestId;
		DayTransitionFailureUntilSeconds = Now + 2.0;
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=day_transition_failure_feedback RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Message=%s"),
			*Transition.RequestId.ToString(), *GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
			Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller), *Transition.Message.ToString());
	}
	const bool bShowFailure = Transition.bFailed && Now < DayTransitionFailureUntilSeconds;
	if (!bBlocked && !bShowFailure)
	{
		if (DayTransitionWidget)
		{
			DayTransitionWidget->RenderTransition(0.0f, FText::GetEmpty(), false);
			DayTransitionWidget->RemoveFromParent();
			DayTransitionWidget = nullptr;
		}
		return;
	}
	float BlackOpacity = 0.0f;
	FText Title;
	if (bShowFailure)
	{
		Title = Transition.Message;
	}
	else
	{
		const double FadeOut = FMath::Max(0.0, static_cast<double>(Transition.FadeOutSeconds));
		const double Hold = FMath::Max(0.0, static_cast<double>(Transition.HoldSeconds));
		const double FadeIn = FMath::Max(0.0, static_cast<double>(Transition.FadeInSeconds));
		const double Elapsed = FMath::Max(0.0, ServerTimeSeconds - Transition.StartServerTimeSeconds);
		if (Elapsed < FadeOut)
		{
			BlackOpacity = static_cast<float>(Elapsed / FadeOut);
		}
		else if (Elapsed < FadeOut + Hold)
		{
			BlackOpacity = 1.0f;
		}
		else if (Elapsed < FadeOut + Hold + FadeIn)
		{
			BlackOpacity = static_cast<float>(1.0 - (Elapsed - FadeOut - Hold) / FadeIn);
		}
		if (Transition.bCommitted && Elapsed >= FadeOut && Elapsed < FadeOut + Hold + FadeIn)
		{
			Title = Transition.Message.IsEmpty()
				? FText::Format(NSLOCTEXT("CatDayTransition", "DayTitle", "第 {0} 天"), FText::AsNumber(Transition.TargetDayIndex))
				: Transition.Message;
		}
	}
	const bool bCreatedThisFrame = !DayTransitionWidget;
	if (bCreatedThisFrame)
	{
		DayTransitionWidget = CreateWidget<UCatDayTransitionWidget>(Controller, UCatDayTransitionWidget::StaticClass());
	}
	if (!DayTransitionWidget) return;
	// 与现有 HUD 使用同一全视口层：9000 遮住 HUD，仍低于 Online 的 10000；玩家子层无法覆盖全视口 HUD。
	if (!DayTransitionWidget->IsInViewport()) DayTransitionWidget->AddToViewport(9000);
	// 视口晚到时保留实例，下一帧再挂接；只在首次失败时记录，避免依赖未就绪期间刷屏。
	if (!DayTransitionWidget->IsInViewport())
	{
		if (bCreatedThisFrame)
		{
			UE_LOG(LogCatUI, Warning,
				TEXT("Event=day_transition_view_unavailable RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
				*Transition.RequestId.ToString(), *GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
				Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller));
		}
		return;
	}
	DayTransitionWidget->RenderTransition(BlackOpacity, Title, bBlocked);
}

// 清理流程：移出本功能视图并清空失败去重与停留时间；不修改输入模式、Online loading 或任何 Run 快照。
void UCatLocalPlayerUISubsystem::ClearDayTransition()
{
	if (DayTransitionWidget)
	{
		DayTransitionWidget->RenderTransition(0.0f, FText::GetEmpty(), false);
		DayTransitionWidget->RemoveFromParent();
		DayTransitionWidget = nullptr;
	}
	LastDayTransitionFailureId.Invalidate();
	DayTransitionFailureUntilSeconds = 0.0;
}

// 页面只为关闭和输入读取这个控制器；每个库存 WBP 自己绑定所属库存的 Model。
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
// 1. 先读取当前 LocalPlayer、Controller 和 Online 快照；缺 LocalPlayer 或 Online 时拆掉 Root 与全局遮罩，避免失效 UI 脱离事实源继续显示。
// 2. 全局遮罩先消费 Start/Leave 快照并独立入视口；Start 加载一旦成立，Root 立即拆除，避免已遮挡前端在地图包异步加载期间继续创建动态行。
// 3. 没有本地 Controller 时只允许已有 Root 在 Start 失败恢复保护窗内短暂保留；其它情况立即拆除。
// 4. 非 Frontend World 只保留受保护的已有 Root，不在玩法图或异常 World 补建新主界面，避免已遮挡前端变成第二入口。
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
	const bool bGameplayStartLoadingOwnsViewport = Snapshot.LastError == ECatOnlineError::None
		&& CatLocalPlayerUILoadingScreen::IsGameplayStartLoadingSnapshot(Snapshot)
		&& !IsGameplayLoadingReadyToDismiss(Snapshot);
	if (bGameplayStartLoadingOwnsViewport)
	{
		if (FrontendRootWidget)
		{
			UE_LOG(LogCatUI, Log,
				TEXT("Event=frontend_root_removed_for_global_loading RequestId=%s Epoch=%lld World=%s Operation=%s Transport=%s"),
				*Snapshot.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				Snapshot.OperationEpoch,
				*GetNameSafe(GetWorld()),
				*UEnum::GetValueAsString(Snapshot.ActiveOperation),
				*UEnum::GetValueAsString(Snapshot.TransportState));
		}
		RemoveFrontendRoot();
		return;
	}
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

// 全局遮罩刷新流程：
// 1. 先记录本次刷新前仍在跟随的 Start/Leave 请求，方便完成态同帧到达时还能写出完成状态。
// 2. 再按 Online、引擎和本地 UI 就绪事实决定显示；仍在真实等待时取消完成态停留并刷新正式 WBP。
// 3. 已经真实完成时只进入视觉层的短暂完成展示，再移除遮罩；错误或非 Start/Leave 状态则立刻释放。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot)
{
	const ECatOnlineOperation PreviousLoadingOperation = GlobalLoadingOperation;
	const FGuid PreviousLoadingRequestId = GlobalLoadingRequestId;
	const bool bHadVisibleLoadingScreen = GlobalLoadingScreenWidget && GlobalLoadingScreenWidget->IsInViewport();
	TrackGlobalLoadingTransition(Snapshot);
	FCatGlobalLoadingPresentation Presentation;
	if (ShouldShowGlobalLoadingScreen(Snapshot, Presentation))
	{
		ClearGlobalLoadingDismissalPostTick();
		ShowGlobalLoadingScreen(Presentation);
		return;
	}
	const bool bCanPresentGameplayCompletion = PreviousLoadingOperation == ECatOnlineOperation::Start
		&& IsGameplayLoadingReadyToDismiss(Snapshot);
	const bool bCanPresentFrontendCompletion = PreviousLoadingOperation == ECatOnlineOperation::Leave
		&& IsFrontendLoadingReadyToDismiss(Snapshot);
	if (Snapshot.LastError == ECatOnlineError::None && bHadVisibleLoadingScreen
		&& (bCanPresentGameplayCompletion || bCanPresentFrontendCompletion))
	{
		GlobalLoadingRequestId = PreviousLoadingRequestId;
		RequestGlobalLoadingDismissalAfterPresentation(PreviousLoadingOperation);
		return;
	}
	if (bGlobalLoadingDismissalPending && Snapshot.LastError == ECatOnlineError::None)
	{
		return;
	}
	HideGlobalLoadingScreen();
}

// 当前快照刷新流程：从本 LocalPlayer 的 GameInstance 找回 Online 子系统并复用主遮罩刷新入口；缺事实源时清掉过渡记忆，避免既有请求跨 World 继续显示。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreenFromCurrentSnapshot()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		GlobalLoadingOperation = ECatOnlineOperation::None;
		GlobalLoadingRequestId.Invalidate();
		ClearGlobalLoadingDismissalPostTick();
		HideGlobalLoadingScreen();
		return;
	}
	RefreshGlobalLoadingScreen(Online->GetSnapshot());
}

// 全局遮罩 gate 流程：
// 1. 错误快照直接放行，让业务页面显示失败原因，不把失败藏在遮罩下面。
// 2. 进入游戏时显示总进度；Start、玩法软资源预热、地图包、Travel、World、BeginPlay、Connected 和本地 UI 都只按真实 gate 推进。
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
		if (Snapshot.bIsGameplayStartupAssetLoadPending)
		{
			const int32 AssetDisplayPercent = FMath::RoundToInt(Snapshot.GameplayStartupAssetLoadProgressPercent);
			OutPresentation.StatusText = FText::FromString(TEXT("正在预热游戏资源"));
			OutPresentation.DetailText = Snapshot.bHasGameplayStartupAssetLoadProgress
				? FText::FromString(FString::Printf(TEXT("%s %d%%"),
					*Snapshot.GameplayStartupAssetLoadProgressStatus, AssetDisplayPercent))
				: FText::FromString(TEXT("等待资源管理器返回启动资源加载阶段。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("等待 StreamableHandle 完成玩法启动资源预热。"));
			return true;
		}
		if (Snapshot.bIsGameplayLoadPending && Snapshot.bIsMapPreloadPending)
		{
			const int32 PackageDisplayPercent = FMath::RoundToInt(Snapshot.MapLoadProgressPercent);
			OutPresentation.StatusText = FText::FromString(TEXT("正在读取游戏世界"));
			OutPresentation.DetailText = Snapshot.bHasMapLoadProgress
				? FText::FromString(FString::Printf(TEXT("%s %d%%"),
					*Snapshot.MapLoadProgressStatus, PackageDisplayPercent))
				: FText::FromString(TEXT("等待引擎返回地图包加载阶段。"));
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
		if (Snapshot.ActiveOperation == ECatOnlineOperation::Start
			&& Snapshot.WorldState == ECatOnlineWorldState::Frontend)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在准备游戏世界。"));
			OutPresentation.DetailText = FText::FromString(TEXT("等待 Online 提交地图包预载。"));
			OutPresentation.ReasonText = FText::FromString(TEXT("Start 操作已受理，等待 LoadPackageAsync 请求。"));
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
			OutPresentation.DetailText = Snapshot.bHasMapLoadProgress && !Snapshot.MapLoadProgressStatus.IsEmpty()
				? FText::FromString(Snapshot.MapLoadProgressStatus)
				: FText::FromString(TEXT("等待 Frontend 地图包预载完成。"));
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

// 全局遮罩隐藏流程：先成对解绑完成态停留回调并清掉本地 Start/Leave 过渡记忆，再移除最高层 WBP 和文本缓存；Online 终态和错误展示仍由对应 Controller/Model 自己处理。
void UCatLocalPlayerUISubsystem::HideGlobalLoadingScreen()
{
	ClearGlobalLoadingDismissalPostTick();
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

// 完成态遮罩移除请求流程：
// 1. 根据刚完成的 Start/Leave 写入真实完成文案，Start 明确显示总进度 100%，Leave 仍不显示进度条。
// 2. 从 UI 设置读取最短展示秒数并换成单调时间；这段等待发生在加载全部完成之后，只服务玩家看清完成态。
// 3. 若 Slate 可用则注册 PostTick 回调按界面刷新周期检查到点时间；PostTick 只是完成文案的展示检查点，不参与加载判断。
void UCatLocalPlayerUISubsystem::RequestGlobalLoadingDismissalAfterPresentation(
	const ECatOnlineOperation CompletedOperation)
{
	if (CompletedOperation != ECatOnlineOperation::Start && CompletedOperation != ECatOnlineOperation::Leave)
	{
		HideGlobalLoadingScreen();
		return;
	}
	bGlobalLoadingDismissalPending = true;
	GlobalLoadingDismissalOperation = CompletedOperation;
	GlobalLoadingDismissalRequestId = GlobalLoadingRequestId;
	FCatGlobalLoadingPresentation Presentation;
	if (CompletedOperation == ECatOnlineOperation::Start)
	{
		Presentation.HeadingText = FText::FromString(TEXT("正在进入游戏"));
		Presentation.StatusText = FText::FromString(TEXT("游戏世界准备完成。"));
		Presentation.DetailText = FText::FromString(TEXT("本地玩家界面已就绪。"));
		Presentation.ReasonText = FText::FromString(TEXT("准备完成，马上开始旅程。"));
		Presentation.bShowProgressBar = true;
		Presentation.bHasProgressPercent = true;
		Presentation.ProgressPercent = 100.0f;
	}
	else
	{
		Presentation.HeadingText = FText::FromString(TEXT("正在返回主菜单"));
		Presentation.StatusText = FText::FromString(TEXT("主菜单准备完成。"));
		Presentation.DetailText = FText::FromString(TEXT("主菜单界面已就绪。"));
		Presentation.ReasonText = FText::FromString(TEXT("准备完成，马上返回主菜单。"));
		Presentation.bShowProgressBar = false;
		Presentation.bHasProgressPercent = false;
	}
	ShowGlobalLoadingScreen(Presentation);
	const UCatUISettings* UISettings = GetDefault<UCatUISettings>();
	const double HoldSeconds = UISettings ? UISettings->GetGlobalLoadingCompletionHoldSeconds() : 0.0;
	GlobalLoadingDismissalReadyTimeSeconds = FPlatformTime::Seconds() + HoldSeconds;
	if (!FSlateApplication::IsInitialized())
	{
		HideGlobalLoadingScreen();
		return;
	}
	if (!GlobalLoadingDismissalPostTickHandle.IsValid())
	{
		GlobalLoadingDismissalPostTickHandle = FSlateApplication::Get().OnPostTick().AddUObject(
			this, &ThisClass::HandleGlobalLoadingDismissalPostTick);
	}
}

// 完成态停留后收口流程：只响应已经安排过的完成态请求；每次 Slate PostTick 都只检查真实时间是否越过最短展示点，未到点时继续保留遮罩且不改写 Online 状态。
void UCatLocalPlayerUISubsystem::HandleGlobalLoadingDismissalPostTick(const float DeltaTime)
{
	(void)DeltaTime;
	if (!bGlobalLoadingDismissalPending)
	{
		ClearGlobalLoadingDismissalPostTick();
		return;
	}
	if (FPlatformTime::Seconds() < GlobalLoadingDismissalReadyTimeSeconds)
	{
		return;
	}
	const ECatOnlineOperation CompletedOperation = GlobalLoadingDismissalOperation;
	const FGuid CompletedRequestId = GlobalLoadingDismissalRequestId;
	ClearGlobalLoadingDismissalPostTick();
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_global_loading_dismissal_presented Operation=%d RequestId=%s LastStatus=\"%s\""),
		static_cast<int32>(CompletedOperation),
		*CompletedRequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*LastGlobalLoadingStatusText.ToString());
	HideGlobalLoadingScreen();
}

// 完成态停留回调清理流程：如果曾经注册 Slate PostTick 就成对移除；随后清空完成态请求字段，避免新一次 Start/Leave 继承失效完成展示。
void UCatLocalPlayerUISubsystem::ClearGlobalLoadingDismissalPostTick()
{
	if (GlobalLoadingDismissalPostTickHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnPostTick().Remove(GlobalLoadingDismissalPostTickHandle);
	}
	GlobalLoadingDismissalPostTickHandle.Reset();
	bGlobalLoadingDismissalPending = false;
	GlobalLoadingDismissalOperation = ECatOnlineOperation::None;
	GlobalLoadingDismissalRequestId.Invalidate();
	GlobalLoadingDismissalReadyTimeSeconds = 0.0;
}

// 全局遮罩表现刷新流程：
// 1. 先写高层目标和当前真实步骤，让玩家能看到正在等保存、销毁房间、切图还是 UI 装配。
// 2. 进入游戏时把模型合成出的总进度和百分号直接写到 WBP；总进度来自状态事实，不来自倒计时或动画时长。
// 3. 返回主菜单会折叠进度条，只更新文字状态；代码只展示真实等待阶段，不由定时器判断完成。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreenPresentation(const FCatGlobalLoadingPresentation& Presentation)
{
	if (!GlobalLoadingScreenWidget)
	{
		return;
	}
	LastGlobalLoadingStatusText = Presentation.StatusText;
	const float DisplayedProgressPercent = Presentation.ProgressPercent;
	const int32 DisplayPercent = FMath::RoundToInt(DisplayedProgressPercent);
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
	if (UTextBlock* DetailTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingDetailTextBlock"))))
	{
		DetailTextBlock->SetText(Presentation.DetailText.IsEmpty()
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
		ProgressBar->SetPercent(Presentation.bShowProgressBar ? DisplayedProgressPercent / 100.0f : 0.0f);
	}
}

// 进入游戏总进度合成流程：
// 1. 先用 Online 的 Start/预载/旅行事实确认请求至少已经受理，再把这个事实计入总进度。
// 2. 启动资源阶段只消费 StreamableHandle 百分比，地图包阶段只消费 LoadPackageAsync 事件百分比。
// 3. 后续 Travel、LoadMap、World 到达、BeginPlay、Transport Connected、Controller/Pawn/HUD 和页面控制器只在对应真实 gate 成立时继续累加。
// 4. 返回值保持由真实事实累加得出的结果；如果展示异常，应修正事实来源和权重对应关系，不在显示层改写数值。
float UCatLocalPlayerUISubsystem::GetGameplayLoadingProgressPercent(const FCatOnlineSnapshot& Snapshot) const
{
	using namespace CatLocalPlayerUILoadingScreen;
	float ProgressPercent = 0.0f;
	const bool bStartAccepted = IsGameplayStartLoadingSnapshot(Snapshot)
		|| GlobalLoadingOperation == ECatOnlineOperation::Start;
	if (bStartAccepted)
	{
		ProgressPercent += GameplayStartAcceptedWeight;
	}
	if (Snapshot.bIsGameplayStartupAssetLoadPending)
	{
		if (Snapshot.bHasGameplayStartupAssetLoadProgress)
		{
			ProgressPercent += GetStartupAssetProgressContribution(Snapshot.GameplayStartupAssetLoadProgressPercent);
		}
		return ProgressPercent;
	}
	if (Snapshot.bHasGameplayStartupAssetLoadProgress)
	{
		ProgressPercent += GameplayStartupAssetWeight;
	}
	if (Snapshot.bIsGameplayLoadPending)
	{
		if (Snapshot.bHasMapLoadProgress)
		{
			ProgressPercent += GetPackageProgressContribution(Snapshot.MapLoadProgressPercent);
		}
		return ProgressPercent;
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	const UWorld* World = WorldContext ? WorldContext->World() : nullptr;
	const bool bGameplayTravelSubmitted = Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
		|| Snapshot.WorldState == ECatOnlineWorldState::Lake
		|| Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
		|| Snapshot.TransportState == ECatOnlineTransportState::Connected;
	if (bGameplayTravelSubmitted)
	{
		if (!Snapshot.bHasGameplayStartupAssetLoadProgress)
		{
			ProgressPercent += GameplayStartupAssetWeight;
		}
		ProgressPercent += GameplayPackageWeight;
		ProgressPercent += GameplayTravelSubmittedWeight;
	}
	const bool bEngineTravelComplete = bGameplayTravelSubmitted
		&& Snapshot.WorldState == ECatOnlineWorldState::Lake
		&& !Snapshot.bIsEngineLoadMapPending
		&& WorldContext
		&& World
		&& WorldContext->TravelURL.IsEmpty()
		&& WorldContext->PendingNetGame == nullptr;
	if (bEngineTravelComplete)
	{
		ProgressPercent += GameplayEngineTravelCompleteWeight;
	}
	if (Snapshot.WorldState == ECatOnlineWorldState::Lake)
	{
		ProgressPercent += GameplayWorldReachedWeight;
		if (World && World->GetGameState<AGameStateBase>() && World->HasBegunPlay() && !World->IsInSeamlessTravel())
		{
			ProgressPercent += GameplayWorldRunningWeight;
		}
	}
	if (Snapshot.TransportState == ECatOnlineTransportState::Connected)
	{
		ProgressPercent += GameplayTransportConnectedWeight;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	if (Controller && Controller->IsLocalController())
	{
		ProgressPercent += GameplayLocalControllerWeight;
	}
	if (Character)
	{
		ProgressPercent += GameplayLocalPawnWeight;
	}
	if (HUDWidget && HUDWidget->IsInViewport())
	{
		ProgressPercent += GameplayHudVisibleWeight;
	}
	if (InventoryPageController && LakeMainMenuController && InteractionPageController
		&& AttachedPlayerLakeCharacter.Get() == Character)
	{
		ProgressPercent += GameplayLocalUIReadyWeight;
	}
	return ProgressPercent;
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

// 进入玩法收口流程：只有资源/地图包预载和引擎 LoadMap 都结束，Online 确认 Lake/Connected、World 已 BeginPlay，且本地 Controller 的猫 Pawn、HUD、局内菜单和交互控制器都装配好，遮罩才允许消失。
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
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	UWorld* World = WorldContext ? WorldContext->World() : nullptr;
	if (!WorldContext || !World || !WorldContext->TravelURL.IsEmpty() || WorldContext->PendingNetGame != nullptr
		|| !World->GetGameState<AGameStateBase>() || !World->HasBegunPlay() || World->IsInSeamlessTravel())
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

// 回主菜单收口流程：只有 Online 确认 Frontend/Idle、引擎 World 已 BeginPlay，并且正式 Frontend Root 已入视口，遮罩才允许消失。
bool UCatLocalPlayerUISubsystem::IsFrontendLoadingReadyToDismiss(const FCatOnlineSnapshot& Snapshot) const
{
	if (Snapshot.WorldState != ECatOnlineWorldState::Frontend
		|| Snapshot.TransportState != ECatOnlineTransportState::Idle
		|| Snapshot.bIsMapPreloadPending
		|| Snapshot.bIsEngineLoadMapPending)
	{
		return false;
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	UWorld* World = WorldContext ? WorldContext->World() : nullptr;
	if (!WorldContext || !World || !WorldContext->TravelURL.IsEmpty() || WorldContext->PendingNetGame != nullptr
		|| !World->GetGameState<AGameStateBase>() || !World->HasBegunPlay() || World->IsInSeamlessTravel())
	{
		return false;
	}
	return FrontendRootWidget && FrontendRootWidget->IsInViewport();
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
// 2. Start 加载和旅行期间由全局遮罩接管，Root 在 World 切换前释放，避免 Root 局部页面成为第二套表现。
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
			|| Snapshot.LastError == ECatOnlineError::NetworkFailure)
		&& (Snapshot.WorldState == ECatOnlineWorldState::Frontend || Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake
			|| Snapshot.TransportState == ECatOnlineTransportState::Failed);
	return bGameplayStartFailureRecovering;
}

// Frontend 拆除流程：先关闭流程控制器的 Model 订阅和 Root 绑定，再拆 Root，最后关闭三个 Model；若调用时仍有绑定 Controller，记录一次真实拆除并恢复非鼠标前端状态，此路径不碰既有 Lake HUD、背包和交互提示。
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
		UE_LOG(LogCatUI, Log, TEXT("Event=frontend_root_removed World=%s NetMode=%d Controller=%s"),
			*GetNameSafe(GetWorld()),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
			*GetNameSafe(BoundPlayerController.Get()));
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

// Controller 解绑流程：先移除仅属于旧 Controller 的翻天表现，再清理弱引用；Pawn 刷新继续由 Controller 生命周期推送。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	ClearDayTransition();
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
// 3. 创建库存窗口控制器和默认背包 WBP；面板绑定角色库存自己的 Model，不预先入视口，仍通过既有 Action 打开。
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
	InventoryPageController = NewObject<UCatInventoryPageController>(this);
	InventoryWidget = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	LakeMainMenuController = NewObject<UCatLakeMainMenuController>(this);
	LakeMainMenuWidget = CreateWidget<UCatLakeMainMenuWidget>(Controller, LakeMainMenuViewClass);
	InteractionPageController = NewObject<UCatInteractionPageController>(this);
	InteractionPromptWidget = CreateWidget<UCatInteractionPromptWidget>(Controller, InteractionPromptViewClass);
	if (!HUDModel || !HUDWidget || !InventoryPageController || !InventoryWidget
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
	if (!InventoryPageController->Bind(Controller, InventoryWidget)
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

// 局内菜单切换流程：翻天期间拒绝打开；其他时候先关闭背包，保证同一 Controller 只有一个菜单模态恢复记录。
void UCatLocalPlayerUISubsystem::ToggleLakeMainMenu()
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return;
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

// HUD 入口动作流程：背包和主菜单都转交已有控制器；HUD 不拼业务页面，也不持有保存或离局业务。
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
