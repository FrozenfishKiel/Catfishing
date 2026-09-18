#include "UI/CatLocalPlayerUISubsystem.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "UI/Run/CatAltarConfirmationWidget.h"
#include "UI/Run/CatDayTransitionWidget.h"
#include "UI/WorldInfo/CatWorldInfoController.h"

#include "Character/CatCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionPageController.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/Collection/CatFishRevealWidget.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/PlayerState.h"
#include "Profile/CatProfileSubsystem.h"
#include "Save/CatSaveSubsystem.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"
#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Voice/CatVoiceActivityWidget.h"
#include "UI/Interaction/CatInteractionPageController.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/Inventory/CatInventoryQuickbarWidget.h"
#include "Inventory/CatBackPackComponent.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/ItemTooltip/CatItemTooltipController.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/Save/CatLakeMainMenuController.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

namespace CatLocalPlayerUIFishReveal
{
	/**
	 * 首解锁特写留在屏幕上的最长秒数。它是兜底上限，不是设计给的出口：
	 * 设计的出口是「Space 继续」，那条路要一个 InputAction 资产，本轮没有。
	 * 浮层不抢键盘焦点（抢了玩家就动不了），所以必须有个上限，否则它会一直挂在那儿。
	 */
	constexpr float AutoDismissSeconds = 8.0f;
}

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

// 销毁流程：先释放 HUD、背包、物品提示和交互提示模块；Controller 解绑时移除翻天表现，最后清理 Frontend Root 与 Online 快照。
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

// 背包切换流程：先拒绝翻天期间的 HUD 调用，打开前先关掉图鉴，再由窗口控制器管理视口与输入；库存仍由自己的 Model 通知。
void UCatLocalPlayerUISubsystem::ToggleInventory()
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return;
	if (InventoryPageController)
	{
		if (!InventoryPageController->IsInventoryOpen()
			&& CollectionPageController && CollectionPageController->IsCollectionOpen())
		{
			CollectionPageController->RequestCloseCollectionFromWidget();
		}
		InventoryPageController->ToggleInventory();
		RefreshHUDAfterPageVisibilityChanged();
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
    const bool bOpened = InventoryPageController->OpenInventory(Inventory, InventoryViewClass);
    RefreshHUDAfterPageVisibilityChanged();
    return bOpened;
}

// 状态读取流程：从 PageController 读取唯一背包状态；未装配背包时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsInventoryOpen() const
{
	return InventoryPageController ? InventoryPageController->IsInventoryOpen() : false;
}

// 图鉴切换流程：先拒绝翻天期间的调用；打开前先关掉背包和局内菜单，保证同一 Controller 只有一层模态输入锁在生效。
void UCatLocalPlayerUISubsystem::ToggleCollection()
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return;
	if (!CollectionPageController)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_toggle_rejected Reason=PageControllerUnavailable World=%s"),
			*GetPathNameSafe(GetWorld()));
		return;
	}
	if (!CollectionPageController->IsCollectionOpen())
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
	CollectionPageController->ToggleCollection();
	RefreshHUDAfterPageVisibilityChanged();
}

// 页面开合读取流程：三页共用一层模态输入锁，任一开着就算「打开了界面」。
// 它是「打开界面」这件事的唯一事实源——HUD 读它决定世界进度那一位露不露面（交互册 §42），不在 HUD 里存第二份。
bool UCatLocalPlayerUISubsystem::IsAnyPlayerPageOpen() const
{
	return (InventoryPageController && InventoryPageController->IsInventoryOpen())
		|| (CollectionPageController && CollectionPageController->IsCollectionOpen())
		|| (LakeMainMenuController && LakeMainMenuController->IsMenuOpen());
}

// 页面开合后的 HUD 重读流程：只让 HUD 立刻重算一次投影。
// 世界进度那一位的显隐跟着 IsAnyPlayerPageOpen 走，这里不传值也不缓存，避免出现第二份开合状态。
void UCatLocalPlayerUISubsystem::RefreshHUDAfterPageVisibilityChanged()
{
	if (HUDModel)
	{
		HUDModel->Refresh();
	}
}

// 本机首解锁特写流程：
// 1. 事实已经 durable——Profile 在第二次落盘成功之后才广播，所以这里弹出来的东西一定已经写进图鉴。
// 2. 从正式鱼目录取展示名、介绍与彩页图；鱼种没登记时仍然弹，名字退回鱼种 ID，不静默吞掉一次首解锁。
// 3. 浮层 WBP 资产不在本轮范围：类没配置时只记一次诊断，不创建原生白盒替身，也不影响已经写好的图鉴记录。
void UCatLocalPlayerUISubsystem::HandleLocalFishSpeciesFirstRecorded(const int32  ItemId,
	const double WeightKilograms)
{
	if ((ItemId == 0))
	{
		return;
	}
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	TSubclassOf<UCatFishRevealWidget> RevealViewClass;
	if (Settings)
	{
		RevealViewClass = Settings->LoadFishRevealWidgetClass();
	}
	if (!RevealViewClass)
	{
		if (!bHasLoggedMissingFishRevealWidget)
		{
			UE_LOG(LogCatUI, Warning,
				TEXT("Event=ui_fish_reveal_class_missing Class=%s ItemId=%s Result=RevealSkippedCollectionStillRecorded"),
				Settings ? *Settings->FishRevealWidgetClass.ToSoftObjectPath().ToString() : TEXT("None"),
				*FString::FromInt(ItemId));
			bHasLoggedMissingFishRevealWidget = true;
		}
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller)
	{
		return;
	}
	if (!FishRevealWidget || FishRevealWidget->GetClass() != RevealViewClass)
	{
		if (FishRevealWidget)
		{
			FishRevealWidget->OnDismissRequested.RemoveAll(this);
			FishRevealWidget->RemoveFromParent();
		}
		FishRevealWidget = CreateWidget<UCatFishRevealWidget>(Controller, RevealViewClass);
		if (!FishRevealWidget)
		{
			return;
		}
		FishRevealWidget->OnDismissRequested.AddWeakLambda(this, [this]()
		{
			if (FishRevealWidget)
			{
				FishRevealWidget->RemoveFromParent();
			}
		});
	}
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	const UCatFishDefinition* Definition = Catalog ? Catalog->FindRuntimeDefinition(ItemId) : nullptr;
	FCatFishRevealViewData ViewData;
	const FText DisplayName = Definition ? Definition->GetInventoryDisplayName() : FText();
	ViewData.NameText = DisplayName.IsEmpty() ? FText::AsNumber(ItemId) : DisplayName;
	ViewData.DescriptionText = Definition ? Definition->GetInventoryDescription() : FText();
	if (FMath::IsFinite(WeightKilograms) && WeightKilograms > 0.0)
	{
		FNumberFormattingOptions WeightFormat;
		WeightFormat.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
		ViewData.WeightText = FText::Format(NSLOCTEXT("CatFishReveal", "Weight", "重量：{0} kg"),
			FText::AsNumber(WeightKilograms, &WeightFormat));
	}
	ViewData.ContinueHintText = NSLOCTEXT("CatFishReveal", "Continue", "Space 继续");
	ViewData.Portrait = Definition ? Definition->GetInventoryThumbnail().LoadSynchronous() : nullptr;
	if (!FishRevealWidget->IsInViewport())
	{
		// 层级比交互提示高、比翻天遮罩低：它是一次性揭示，不该盖住结算过场，也不该被交互提示压住。
		FishRevealWidget->AddToViewport(3);
	}
	FishRevealWidget->RenderReveal(ViewData);
	// 兜底收起：见 FishRevealDismissTimerHandle 的注释。每次新的一条都重置计时，不会叠加。
	if (UWorld* RevealWorld = Controller->GetWorld())
	{
		RevealWorld->GetTimerManager().ClearTimer(FishRevealDismissTimerHandle);
		RevealWorld->GetTimerManager().SetTimer(FishRevealDismissTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (FishRevealWidget)
			{
				FishRevealWidget->RemoveFromParent();
			}
		}), CatLocalPlayerUIFishReveal::AutoDismissSeconds, false);
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_fish_reveal_shown ItemId=%s Weight=%.3f"),
		*FString::FromInt(ItemId), WeightKilograms);
}

// 他人解锁提示流程：只读 GameState 复制的那一条广播，按 AnnouncementId 去重；本人那条不在这里重复弹。
// 提示走 HUD 的一次性播报位，因此它不打断操作、不抢焦点、不进模态层
// （主界面参考稿逐字写「你仍可移动，交互和继续钓鱼」）。
void UCatLocalPlayerUISubsystem::HandleFishSpeciesDiscoveryAnnounced()
{
	const ACatfishingGameState* GameState = BoundDiscoveryGameState.Get();
	const APlayerController* Controller = BoundPlayerController.Get();
	if (!GameState || !Controller)
	{
		return;
	}
	const FCatFishSpeciesDiscoveryAnnouncement& Announcement = GameState->GetLastFishSpeciesDiscovery();
	if (!Announcement.AnnouncementId.IsValid() || Announcement.AnnouncementId == LastAnnouncedFishDiscoveryId)
	{
		return;
	}
	LastAnnouncedFishDiscoveryId = Announcement.AnnouncementId;
	const int32 LocalPlayerId = Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : 0;
	if (Announcement.DiscovererPlayerId != 0 && Announcement.DiscovererPlayerId == LocalPlayerId)
	{
		// 自己这条已经由本机 Profile 的首解锁特写承担，不再多弹一条同内容的提示。
		return;
	}
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	const UCatFishDefinition* Definition = Catalog ? Catalog->FindRuntimeDefinition(Announcement.ItemId) : nullptr;
	const FText FishNameText = (Definition && !Definition->GetInventoryDisplayName().IsEmpty())
		? Definition->GetInventoryDisplayName() : FText::AsNumber(Announcement.ItemId);
	const FText DiscovererText = Announcement.DiscovererDisplayName.IsEmpty()
		? NSLOCTEXT("CatFishReveal", "UnknownDiscoverer", "有只猫")
		: FText::FromString(Announcement.DiscovererDisplayName);
	const FText BroadcastText = FText::Format(
		NSLOCTEXT("CatFishReveal", "OtherDiscovered", "{0} 发现了新鱼种！{1} - 首次记录"),
		DiscovererText, FishNameText);
	if (HUDWidget)
	{
		HUDWidget->AnnounceFishSpeciesDiscovery(BroadcastText);
	}
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_fish_discovery_announced AnnouncementId=%s ItemId=%s HUDPresent=%d"),
		*Announcement.AnnouncementId.ToString(EGuidFormats::DigitsWithHyphens),
		*FString::FromInt(Announcement.ItemId), HUDWidget != nullptr);
}

// 新鱼种广播接线流程：GameState 在客户端可能晚到，所以每次装配与 Controller 变化都重解析一次。
// 换 GameState 时先从旧宿主解绑，避免旅行后的迟到通知打到已失效的 UI 上。
void UCatLocalPlayerUISubsystem::RefreshFishDiscoveryBinding()
{
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (BoundDiscoveryGameState.Get() == GameState && (GameState == nullptr || FishSpeciesDiscoveryHandle.IsValid()))
	{
		return;
	}
	if (ACatfishingGameState* PreviousGameState = BoundDiscoveryGameState.Get())
	{
		PreviousGameState->OnFishSpeciesDiscoveryChanged.Remove(FishSpeciesDiscoveryHandle);
	}
	FishSpeciesDiscoveryHandle.Reset();
	BoundDiscoveryGameState = GameState;
	if (!GameState)
	{
		return;
	}
	FishSpeciesDiscoveryHandle = GameState->OnFishSpeciesDiscoveryChanged.AddUObject(
		this, &ThisClass::HandleFishSpeciesDiscoveryAnnounced);
	// 接线那一刻已经在场的广播算既往事实，只记不播：中途进局的人不该被上午发生的解锁刷屏。
	LastAnnouncedFishDiscoveryId = GameState->GetLastFishSpeciesDiscovery().AnnouncementId;
}

// 新鱼种广播解绑流程：成对移除 GameState 与 Profile 两侧订阅；已记下的广播序号保留，避免解绑再接线时重播同一条。
void UCatLocalPlayerUISubsystem::ClearFishDiscoveryBinding()
{
	if (ACatfishingGameState* GameState = BoundDiscoveryGameState.Get())
	{
		GameState->OnFishSpeciesDiscoveryChanged.Remove(FishSpeciesDiscoveryHandle);
	}
	FishSpeciesDiscoveryHandle.Reset();
	BoundDiscoveryGameState.Reset();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>())
		{
			Profile->OnFishSpeciesFirstRecorded.Remove(FishSpeciesFirstRecordedHandle);
		}
	}
	FishSpeciesFirstRecordedHandle.Reset();
}

// 背包开关键名诊断流程：只把正式 IMC 解析出来的键名写进日志，不断言、不改映射、不禁用背包。
// 设计（ui 表第 6 行、主界面.md:95）写的是 B；键位本体在 IMC 资产里，代码这一侧看不见，
// 对表因此连着两轮只能判 ❓。这条日志让「到底映到了哪个键」在不开编辑器的情况下可查，
// 改哪一边仍然要策划确认——程序不替它决定，更不为一个未裁的键把背包判死。
void UCatLocalPlayerUISubsystem::LogInventoryToggleKeyBinding()
{
	if (bHasLoggedInventoryToggleKey)
	{
		return;
	}
	bHasLoggedInventoryToggleKey = true;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FName ResolvedKeyName = Settings ? Settings->ResolveInventoryToggleKeyName() : NAME_None;
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_inventory_toggle_key_binding ResolvedKey=%s DesignExpectedKey=B Result=DiagnosticOnly"),
		ResolvedKeyName.IsNone() ? TEXT("Unresolved") : *ResolvedKeyName.ToString());
}

// 翻天表现流程：
// 1. 只接收本 LocalPlayer 的当前 Controller；锁定期关闭库存、菜单和图鉴，切断格子直接 use 与模态按键入口。
// 2. 失败按 RequestId 只开启一次基于本机实时时钟的两秒提示；无锁且无提示时先归还焦点，再移除视图，保留去重记录。
// 3. 按服务器时间计算淡出、停留、淡入；已经越过完整过场时透明度为零，阻断仍取 active 且非 failed，不由本地时钟解除。
// 4. 提交且处于结果展示时间段时显示 Message 或目标天数；成功凭据只有请求标识匹配才追加结算摘要，失败只显示错误文本。
// 5. 需要展示时加载正式 WBP，加载或创建失败按请求记录并停止重试；实例存在但视口晚到时保留实例，后续帧继续挂接。
// 6. 挂接全视口 9000 层后提交透明度、文本和阻断状态；旅行由 ClearDayTransition 清除视图、加载失败记忆及反馈计时。
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
		if (CollectionPageController && CollectionPageController->IsCollectionOpen())
		{
			CollectionPageController->RequestCloseCollectionFromWidget();
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Transition.bFailed && Transition.RequestId.IsValid() && Transition.RequestId != LastDayTransitionFailureId)
	{
		LastDayTransitionFailureId = Transition.RequestId;
		DayTransitionFailureUntilSeconds = Now + 2.0;
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=day_transition_failure_feedback RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Message=%s"),
			*Transition.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
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
	FText SettlementDetails;
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
			const FCatOfferingResultSnapshot& Result = Transition.LastCommittedOffering;
			if (Result.RequestId == Transition.RequestId)
			{
				SettlementDetails = FText::Format(NSLOCTEXT("CatDayTransition", "SettlementDetails", "献祭 {0}/{1} 点 · {2}\n世界进度 {3}% → {4}%"),
					FText::AsNumber(Result.OfferedPoints), FText::AsNumber(Result.TargetPoints),
					Result.bMetTarget ? NSLOCTEXT("CatWorldInfo", "TargetMet", "达标") : NSLOCTEXT("CatWorldInfo", "TargetMissed", "未达标"),
					FText::AsNumber(Result.WorldProgressBefore), FText::AsNumber(Result.WorldProgressAfter));
			}
		}
	}
	const bool bCreatedThisFrame = !DayTransitionWidget;
	if (bCreatedThisFrame)
	{
		if (UnavailableDayTransitionViewId == Transition.RequestId) return;
		const UCatUISettings* Settings = GetDefault<UCatUISettings>();
		const TSubclassOf<UCatDayTransitionWidget> ViewClass = Settings->LoadDayTransitionWidgetClass();
		if (ViewClass) DayTransitionWidget = CreateWidget<UCatDayTransitionWidget>(Controller, ViewClass);
		if (!DayTransitionWidget)
		{
			UnavailableDayTransitionViewId = Transition.RequestId;
			UE_LOG(LogCatUI, Error, TEXT("Event=DayTransitionWBPUnavailable World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s RequestId=%s Class=%s"),
				*GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(),
				static_cast<int32>(Controller->GetLocalRole()), *Controller->GetName(), *Transition.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *Settings->DayTransitionWidgetClass.ToString());
			return;
		}
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
				*Transition.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
				Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller));
		}
		return;
	}
	DayTransitionWidget->RenderTransition(BlackOpacity, Title, bBlocked, SettlementDetails);
}

// 清理流程：有视图时先以非阻断空内容渲染，使它归还自己仍持有的焦点，再移出视口并清引用；最后清空两种失败去重键和提示截止时间。
// 不修改玩家输入模式、Online loading 或 Run 快照，已由别的页面接管的焦点不会被强行抢回。
void UCatLocalPlayerUISubsystem::ClearDayTransition()
{
	if (DayTransitionWidget)
	{
		DayTransitionWidget->RenderTransition(0.0f, FText::GetEmpty(), false);
		DayTransitionWidget->RemoveFromParent();
		DayTransitionWidget = nullptr;
	}
	LastDayTransitionFailureId.Invalidate();
	UnavailableDayTransitionViewId.Invalidate();
	DayTransitionFailureUntilSeconds = 0.0;
}

// 祭坛确认表现流程：
// 1. 只接收当前 LocalPlayer 的 Controller，并以 Waiting 快照显示窗口；等待期间不关闭背包、商店或菜单，也不改变焦点和输入模式。
// 2. Cancelled 首次到达时以单调时间保留两秒原因；Accepted、Idle 或取消展示结束时移出窗口，正式翻天遮罩随后独立接管。
// 3. 只加载 Settings 中的正式 WBP，失败按 RequestId 去重记录；所有文本与倒计时仍由 View 从公开快照和 GameState 服务器时间读取。
void UCatLocalPlayerUISubsystem::RefreshAltarConfirmation(APlayerController* Controller,
	const FCatAltarConfirmationSnapshot& Confirmation)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!Controller || !LocalPlayer || Controller != LocalPlayer->GetPlayerController(GetWorld()))
	{
		return;
	}
	const bool bWaiting = Confirmation.State == ECatAltarConfirmationState::Waiting && Confirmation.RequestId.IsValid();
	const double NowSeconds = FPlatformTime::Seconds();
	if (Confirmation.State == ECatAltarConfirmationState::Cancelled && Confirmation.RequestId.IsValid()
		&& Confirmation.RequestId != LastAltarConfirmationCancellationId)
	{
		LastAltarConfirmationCancellationId = Confirmation.RequestId;
		AltarConfirmationCancellationUntilSeconds = NowSeconds + 2.0;
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=altar_confirmation_cancelled_feedback RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Reason=%s"),
			*Confirmation.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller->GetWorld()),
			static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()),
			*GetNameSafe(Controller), *Confirmation.CancelReason.ToString());
	}
	const bool bShowCancellation = Confirmation.State == ECatAltarConfirmationState::Cancelled
		&& Confirmation.RequestId == LastAltarConfirmationCancellationId && NowSeconds < AltarConfirmationCancellationUntilSeconds;
	if (!bWaiting && !bShowCancellation)
	{
		if (AltarConfirmationWidget)
		{
			AltarConfirmationWidget->RemoveFromParent();
			AltarConfirmationWidget = nullptr;
		}
		return;
	}
	const bool bCreatedThisFrame = !AltarConfirmationWidget;
	if (bCreatedThisFrame)
	{
		if (UnavailableAltarConfirmationViewId == Confirmation.RequestId)
		{
			return;
		}
		const UCatUISettings* Settings = GetDefault<UCatUISettings>();
		const TSubclassOf<UCatAltarConfirmationWidget> ViewClass = Settings ? Settings->LoadAltarConfirmationWidgetClass() : nullptr;
		if (ViewClass)
		{
			AltarConfirmationWidget = CreateWidget<UCatAltarConfirmationWidget>(Controller, ViewClass);
		}
		if (!AltarConfirmationWidget)
		{
			UnavailableAltarConfirmationViewId = Confirmation.RequestId;
			UE_LOG(LogCatUI, Error,
				TEXT("Event=altar_confirmation_wbp_unavailable RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Class=%s"),
				*Confirmation.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller->GetWorld()),
				static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()),
				*GetNameSafe(Controller), Settings ? *Settings->AltarConfirmationWidgetClass.ToString() : TEXT("None"));
			return;
		}
	}
	if (!AltarConfirmationWidget->IsInViewport())
	{
		AltarConfirmationWidget->AddToViewport(8500);
	}
	if (!AltarConfirmationWidget->IsInViewport())
	{
		if (bCreatedThisFrame)
		{
			UE_LOG(LogCatUI, Warning,
				TEXT("Event=altar_confirmation_view_unavailable RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s"),
				*Confirmation.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller->GetWorld()),
				static_cast<int32>(Controller->GetNetMode()), Controller->HasAuthority(), static_cast<int32>(Controller->GetLocalRole()), *GetNameSafe(Controller));
		}
		return;
	}
	AltarConfirmationWidget->RenderConfirmation(Confirmation);
}

// 祭坛确认清理流程：只移除本 LocalPlayer 创建的窗口并清空展示去重与取消截止时间；不会发送撤回、取消服务器 Timer 或改写公开快照。
void UCatLocalPlayerUISubsystem::ClearAltarConfirmation()
{
	if (AltarConfirmationWidget)
	{
		AltarConfirmationWidget->RemoveFromParent();
		AltarConfirmationWidget = nullptr;
	}
	UnavailableAltarConfirmationViewId.Invalidate();
	LastAltarConfirmationCancellationId.Invalidate();
	AltarConfirmationCancellationUntilSeconds = 0.0;
}

// 页面只为关闭和输入读取这个控制器；每个库存 WBP 自己绑定所属库存的 Model。
UCatInventoryPageController* UCatLocalPlayerUISubsystem::GetInventoryPageController() const
{
	return InventoryPageController;
}

// 快捷栏读取流程：返回 AttachPlayerLakeUI 已创建的正式 View；未装配、换 Pawn 或旅行清理期间返回空，不创建新控件。
UCatInventoryQuickbarWidget* UCatLocalPlayerUISubsystem::GetInventoryQuickbarWidget() const
{
	return InventoryQuickbarWidget;
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
// 1. 对齐本次 Start/Leave 请求；请求切换时取消旧完成展示，防止新一轮等待沿用旧截止时间。
// 2. 再按 Online、引擎和本地 UI 就绪事实决定显示；仍在真实等待时取消完成态停留并刷新正式 WBP。
// 3. 进入游戏完成后保留既有完成展示；返回主菜单就绪则立即撤罩，不增加完成态停留。错误或无过渡也立即释放。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreen(const FCatOnlineSnapshot& Snapshot)
{
	const ECatOnlineOperation PreviousLoadingOperation = GlobalLoadingOperation;
	const FGuid PreviousLoadingRequestId = GlobalLoadingRequestId;
	const bool bHadVisibleLoadingScreen = GlobalLoadingScreenWidget && GlobalLoadingScreenWidget->IsInViewport();
	TrackGlobalLoadingTransition(Snapshot);
	if (GlobalLoadingOperation != PreviousLoadingOperation || GlobalLoadingRequestId != PreviousLoadingRequestId)
	{
		ResetGlobalLoadingDismissal();
	}
	FCatGlobalLoadingPresentation Presentation;
	if (ShouldShowGlobalLoadingScreen(Snapshot, Presentation))
	{
		ResetGlobalLoadingDismissal();
		ShowGlobalLoadingScreen(Presentation);
		return;
	}
	const bool bCanPresentGameplayCompletion = GlobalLoadingOperation == ECatOnlineOperation::Start
		&& IsGameplayLoadingReadyToDismiss(Snapshot);
	if (Snapshot.LastError == ECatOnlineError::None && bHadVisibleLoadingScreen
		&& bCanPresentGameplayCompletion)
	{
		if (!bGlobalLoadingDismissalPending)
		{
			RequestGlobalLoadingDismissalAfterPresentation(GlobalLoadingOperation);
		}
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
		ResetGlobalLoadingDismissal();
		HideGlobalLoadingScreen();
		return;
	}
	RefreshGlobalLoadingScreen(Online->GetSnapshot());
}

// 全局遮罩 gate 流程：
// 1. 错误快照直接放行，让业务页面显示失败原因，不把失败藏在遮罩下面。
// 2. 进入游戏时显示总进度；Start、玩法软资源预热、地图包、Travel、World、BeginPlay、Connected 和本地 UI 都只按真实 gate 推进。
// 3. 返回主菜单保留真实就绪判断，只显示保存、离开房间或准备主菜单，不展示内部接口及诊断文字。
// 4. Start/Leave 操作已结案但 UI 尚未就绪时，依靠本地过渡记忆继续显示，直到真实就绪事件触发刷新。
bool UCatLocalPlayerUISubsystem::ShouldShowGlobalLoadingScreen(
	const FCatOnlineSnapshot& Snapshot, FCatGlobalLoadingPresentation& OutPresentation) const
{
	OutPresentation = FCatGlobalLoadingPresentation();
	// 摘要与「等什么」无关，无论进入游戏还是回主菜单都先填一次；填不出来就保持未提供。
	FillRunSummaryIntoLoadingPresentation(OutPresentation);
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
		// 退出只展示玩家需要知道的阶段；内部等待原因由 Online 与 Run 日志记录。
		if (Snapshot.SessionState == ECatOnlineSessionState::Destroying)
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在离开房间…"));
		}
		else if (Snapshot.ActiveOperation == ECatOnlineOperation::Leave && Snapshot.WorldState == ECatOnlineWorldState::Lake)
		{
			OutPresentation.StatusText = Snapshot.bLocalRoomActive || Snapshot.SessionRole == ECatOnlineSessionRole::Host
				? FText::FromString(TEXT("正在保存并离开游戏…"))
				: FText::FromString(TEXT("正在离开游戏…"));
		}
		else
		{
			OutPresentation.StatusText = FText::FromString(TEXT("正在准备主菜单…"));
		}
		return true;
	}
	return false;
}

// 本局摘要填充流程：只读本机 Save 子系统当前活动槽的摘要，不打开文件、不发 RPC、不等待网络。
// 加载期玩法 World 还没起来，所以这里给的是「存档记录的那一天」，不是本局实时天数——两者不是一回事，
// 写入侧的文案也必须这么说，不能把存档记录冒充成当前进度。
void UCatLocalPlayerUISubsystem::FillRunSummaryIntoLoadingPresentation(FCatGlobalLoadingPresentation& OutPresentation) const
{
	const UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	const UCatSaveSubsystem* Save = GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	const FName ActiveSlotId = Save ? Save->GetActiveSlotId() : NAME_None;
	if (!Save || ActiveSlotId.IsNone())
	{
		return;
	}
	const FCatSaveSlotSummary* Summary = Save->GetSlotSummaries().FindByPredicate(
		[ActiveSlotId](const FCatSaveSlotSummary& Candidate) { return Candidate.SlotId == ActiveSlotId; });
	if (!Summary)
	{
		return;
	}
	OutPresentation.bHasRunSummary = true;
	OutPresentation.RunSummaryDayIndex = Summary->DayIndex;
	OutPresentation.RunSummaryWorldProgress = Summary->WorldProgress;
	OutPresentation.RunSummaryDailyOfferingTarget = Summary->DailyOfferingTarget;
	OutPresentation.RunSummaryTankOfferingPoints = Summary->TankOfferingPoints;
}

// 全局遮罩显示流程：用 GameInstance 创建或复用正式 WBP 并挂到最高层；类或上下文缺失时记录失败并返回。
// 成功显示后只注册一份 Slate 观察，覆盖 GameState、BeginPlay 和视口晚于最后一次 Online/Pawn 通知到达的空窗；最后写入真实表现。
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
	if (!GlobalLoadingPostTickHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		GlobalLoadingPostTickHandle = FSlateApplication::Get().OnPostTick().AddUObject(
			this, &ThisClass::HandleGlobalLoadingPostTick);
	}
	RefreshGlobalLoadingScreenPresentation(Presentation);
}

// 全局遮罩隐藏流程：先停止 Slate 观察并清掉完成停留、Start/Leave 记忆；即使 Widget 已失效也必须完成解绑。
// 有实例时记录最后阶段并移出视口，随后释放缓存；Online 终态和错误仍归对应 Controller/Model。
void UCatLocalPlayerUISubsystem::HideGlobalLoadingScreen()
{
	if (GlobalLoadingPostTickHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnPostTick().Remove(GlobalLoadingPostTickHandle);
	}
	GlobalLoadingPostTickHandle.Reset();
	ResetGlobalLoadingDismissal();
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
// 1. 仅为进入游戏展示完成文案与 100%；退出或其他操作直接撤罩，主菜单就绪后不再人为等待。
// 2. 从 UI 设置读取最短展示秒数并换成单调时间；这段等待发生在加载全部完成之后，只服务玩家看清完成态。
// 3. 复用显示时已注册的 Slate 观察；若 Slate 不可用则直接清理，避免完成状态留下无法消费的停留请求。
void UCatLocalPlayerUISubsystem::RequestGlobalLoadingDismissalAfterPresentation(
	const ECatOnlineOperation CompletedOperation)
{
	if (CompletedOperation != ECatOnlineOperation::Start)
	{
		HideGlobalLoadingScreen();
		return;
	}
	bGlobalLoadingDismissalPending = true;
	FCatGlobalLoadingPresentation Presentation;
	Presentation.HeadingText = FText::FromString(TEXT("正在进入游戏"));
	Presentation.StatusText = FText::FromString(TEXT("游戏世界准备完成。"));
	Presentation.DetailText = FText::FromString(TEXT("本地玩家界面已就绪。"));
	Presentation.ReasonText = FText::FromString(TEXT("准备完成，马上开始旅程。"));
	Presentation.bShowProgressBar = true;
	Presentation.bHasProgressPercent = true;
	Presentation.ProgressPercent = 100.0f;
	ShowGlobalLoadingScreen(Presentation);
	const UCatUISettings* UISettings = GetDefault<UCatUISettings>();
	const double HoldSeconds = UISettings ? UISettings->GetGlobalLoadingCompletionHoldSeconds() : 0.0;
	GlobalLoadingDismissalReadyTimeSeconds = FPlatformTime::Seconds() + HoldSeconds;
	if (!FSlateApplication::IsInitialized())
	{
		HideGlobalLoadingScreen();
		return;
	}
}

// 遮罩观察流程：先重读当前 World/Online/UI 事实，让没有专门通知的迟到条件也能推进或取消完成展示。
// 仍在等待或展示时间未到时保留遮罩；全部条件成立且完成文案已展示足够时间后记录关联请求并统一隐藏。
// 此回调只观察真实状态，既不随时间增加加载进度，也不以超时跳过准入条件。
void UCatLocalPlayerUISubsystem::HandleGlobalLoadingPostTick(const float DeltaTime)
{
	(void)DeltaTime;
	RefreshGlobalLoadingScreenFromCurrentSnapshot();
	if (!bGlobalLoadingDismissalPending || FPlatformTime::Seconds() < GlobalLoadingDismissalReadyTimeSeconds)
	{
		return;
	}
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_global_loading_dismissal_presented Operation=%d RequestId=%s LastStatus=\"%s\""),
		static_cast<int32>(GlobalLoadingOperation),
		*GlobalLoadingRequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*LastGlobalLoadingStatusText.ToString());
	HideGlobalLoadingScreen();
}

// 完成展示重置流程：清空待撤罩标记和展示截止时间，避免下一次等待继承旧完成态；当前过渡意图与 Slate 观察由遮罩统一保留到隐藏。
void UCatLocalPlayerUISubsystem::ResetGlobalLoadingDismissal()
{
	bGlobalLoadingDismissalPending = false;
	GlobalLoadingDismissalReadyTimeSeconds = 0.0;
}

// 全局遮罩表现刷新流程：
// 1. 先写高层目标和当前真实步骤，让玩家能看到正在等保存、销毁房间、切图还是 UI 装配。
// 2. 进入游戏时把模型合成出的总进度和百分号直接写到 WBP；总进度来自状态事实，不来自倒计时或动画时长。
// 3. 返回主菜单会折叠进度条，只更新文字状态；代码只展示真实等待阶段，不由定时器判断完成。
// 4. 仅在阶段文本变化时记录 World、玩家和请求关联信息；每帧观察不刷日志，也不改写玩法或 Online 状态。
void UCatLocalPlayerUISubsystem::RefreshGlobalLoadingScreenPresentation(const FCatGlobalLoadingPresentation& Presentation)
{
	if (!GlobalLoadingScreenWidget)
	{
		return;
	}
	if (!LastGlobalLoadingStatusText.EqualTo(Presentation.StatusText))
	{
		const UWorld* World = GetWorld();
		const APlayerController* Controller = BoundPlayerController.Get();
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_global_loading_stage RequestId=%s World=%s NetMode=%d Controller=%s Authority=%d LocalRole=%d Status=\"%s\" Detail=\"%s\" Reason=\"%s\""),
			*GlobalLoadingRequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : -1,
			*GetNameSafe(Controller), Controller && Controller->HasAuthority(), Controller ? static_cast<int32>(Controller->GetLocalRole()) : -1,
			*Presentation.StatusText.ToString(), *Presentation.DetailText.ToString(), *Presentation.ReasonText.ToString());
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
	// 加载页的本局摘要：天数与三个量。原参考稿的「献祭进度」已作废，进度轴叫世界进度（主界面.md:71）。
	// 客户端读不到世界槽，逐格写「房主尚未提供」——它是事实陈述，不是错误，也不用 0 冒充。
	const FText HostNotProvided = NSLOCTEXT("CatLoading", "HostNotProvided", "房主尚未提供");
	if (UTextBlock* RunDayTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingRunDayTextBlock"))))
	{
		RunDayTextBlock->SetText(Presentation.bHasRunSummary && Presentation.RunSummaryDayIndex > 0
			? FText::Format(NSLOCTEXT("CatLoading", "RunDay", "存档记录：第 {0} 天"),
				FText::AsNumber(Presentation.RunSummaryDayIndex))
			: HostNotProvided);
	}
	if (UTextBlock* WorldProgressTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingWorldProgressTextBlock"))))
	{
		WorldProgressTextBlock->SetText(Presentation.bHasRunSummary
			? FText::Format(NSLOCTEXT("CatLoading", "WorldProgress", "世界进度 {0}%"),
				FText::AsNumber(Presentation.RunSummaryWorldProgress))
			: HostNotProvided);
	}
	if (UTextBlock* DailyTargetTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingDailyTargetTextBlock"))))
	{
		DailyTargetTextBlock->SetText(Presentation.bHasRunSummary && Presentation.RunSummaryDailyOfferingTarget > 0
			? FText::Format(NSLOCTEXT("CatLoading", "DailyTarget", "当日任务 {0} 点"),
				FText::AsNumber(Presentation.RunSummaryDailyOfferingTarget))
			: HostNotProvided);
	}
	if (UTextBlock* TankReserveTextBlock = Cast<UTextBlock>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingTankReserveTextBlock"))))
	{
		TankReserveTextBlock->SetText(
			Presentation.bHasRunSummary && Presentation.RunSummaryTankOfferingPoints >= 0
			? FText::Format(NSLOCTEXT("CatLoading", "TankReserve", "缸内可献 {0} 点"),
				FText::AsNumber(Presentation.RunSummaryTankOfferingPoints))
			: (Presentation.bHasRunSummary
				? NSLOCTEXT("CatLoading", "TankReserveUnrecorded", "缸内可献 未记录")
				: HostNotProvided));
	}
	if (UProgressBar* WorldProgressBar = Cast<UProgressBar>(
		GlobalLoadingScreenWidget->GetWidgetFromName(TEXT("LoadingWorldProgressBar"))))
	{
		WorldProgressBar->SetVisibility(Presentation.bHasRunSummary
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		WorldProgressBar->SetIsMarquee(false);
		WorldProgressBar->SetPercent(Presentation.bHasRunSummary
			? FMath::Clamp(Presentation.RunSummaryWorldProgress / 100.0f, 0.0f, 1.0f) : 0.0f);
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

// 过渡记忆流程：Start/Leave 的真实快照出现时记录当前请求，错误时失效；成功意图保留到实际隐藏，使完成展示期间的就绪回退仍能继续等待。
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

// Controller 解绑流程：先移除仅属于旧 Controller 的翻天与祭坛确认表现，再清理弱引用；Pawn 刷新继续由 Controller 生命周期推送。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	ClearDayTransition();
	ClearAltarConfirmation();
	BoundPlayerController.Reset();
}

// Tooltip 控制器读取流程：只返回 AttachPlayerLakeUI 写入的本地玩家唯一控制器；局内 UI 未装配或资产创建失败时返回空，调用者据此跳过显示请求而不创建第二条提示链路。
UCatItemTooltipController* UCatLocalPlayerUISubsystem::GetItemTooltipController() const
{
	return ItemTooltipController;
}

// 图鉴控制器读取流程：只返回 AttachPlayerLakeUI 写入的本地玩家唯一控制器；WBP 缺失或绑定失败时返回空，图鉴 WBP 据此跳过关闭请求。
UCatCollectionPageController* UCatLocalPlayerUISubsystem::GetCollectionPageController() const
{
	return CollectionPageController;
}

// Pawn 变化流程：
// 1. 先把 NewPawn 裁成项目猫身体；同一个已装配身体的重复通知只刷新输入绑定，库存和菜单数据继续等自己的读源广播。
// 2. 新身体或空身体会先完整拆掉上一套本地玩家 UI，避免跨 Pawn 复用 Model、View 或输入锁。
// 3. 只有新的 ACatCharacter 通过配置校验时才重新装配 HUD、背包、物品提示、交互提示和局内菜单。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	ACatCharacter* Character = Cast<ACatCharacter>(NewPawn);
	if (Character && AttachedPlayerLakeCharacter.Get() == Character
		&& HUDWidget && InventoryPageController && LakeMainMenuController && InteractionPageController)
	{
		InventoryPageController->RefreshInputBinding();
		LakeMainMenuController->RefreshInputBinding();
		// 图鉴页是可选模块；WBP 缺失时这里没有控制器，其余局内 UI 的输入刷新照常。
		if (CollectionPageController)
		{
			CollectionPageController->RefreshInputBinding();
		}
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
// 1. 验证本地设置、当前 Controller/Pawn 和 World；核心页面 WBP 缺失时停止装配，物品提示缺失则只关闭该提示并记录原因，均不创建原生替身。
// 2. 创建 HUD、背包、独立物品栏、菜单和交互提示；背包与物品栏分别装入各自格子 WBP，物品栏只读同一库存；任一必需实例缺失则统一解绑。
// 3. 绑定 HUD 动作与角色 Model，订阅 Model 更新后把 HUD 放入视口；Model 绑定失败同样统一清理。
// 4. 创建物品悬停 View；成功加入全视口层后才绑定控制器，失败释放引用并记录，库存格只提交来源。
// 5. 刷新 HUD，先创建图鉴页并绑定其页面控制器，再绑定库存和菜单控制器；页面都暂不入视口，仍由既有入口打开。
//    图鉴走在菜单之前，是为了让局内菜单首次渲染就能读到图鉴控制器决定图鉴按钮可用性；它绑定失败只关掉三个图鉴入口并记录，
//    库存或菜单绑定失败才整体解绑（DetachPlayerLakeUI 同时清理已建好的图鉴页）。
// 6. 将交互提示初始化为隐藏并放入视口，再订阅唯一准星目标；绑定失败清理所有局内 UI。
// 7. 最后绑定本玩家的 WorldInfo 控制器并记录装配日志；它读取注册锚点，后续单个信息牌资产失败不拆除整个 HUD。
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
	const TSubclassOf<UCatInventoryQuickbarWidget> InventoryQuickbarViewClass = Settings->LoadInventoryQuickbarWidgetClass();
	const TSubclassOf<UCatInventorySlotWidget> InventorySlotViewClass = Settings->LoadInventorySlotWidgetClass();
	const TSubclassOf<UCatInventorySlotWidget> QuickbarSlotViewClass = Settings->LoadInventoryQuickbarSlotWidgetClass();
	const TSubclassOf<UCatInteractionPromptWidget> InteractionPromptViewClass =
		Settings->LoadInteractionPromptWidgetClass();
	const TSubclassOf<UCatLakeMainMenuWidget> LakeMainMenuViewClass = Settings->LoadLakeMainMenuWidgetClass();
	if (!HUDViewClass || !InventoryViewClass || !InventoryQuickbarViewClass || !InventorySlotViewClass || !QuickbarSlotViewClass || !InteractionPromptViewClass
		|| !LakeMainMenuViewClass)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=ui_player_module_class_missing HUD=%s Inventory=%s Quickbar=%s Slot=%s QuickbarSlot=%s Interaction=%s LakeMenu=%s"),
			*Settings->HUDWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventoryWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventoryQuickbarWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventorySlotWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InventoryQuickbarSlotWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->InteractionPromptWidgetClass.ToSoftObjectPath().ToString(),
			*Settings->LakeMainMenuWidgetClass.ToSoftObjectPath().ToString());
		return;
	}

	HUDModel = NewObject<UCatHUDModel>(this);
	HUDWidget = CreateWidget<UCatHUDWidget>(Controller, HUDViewClass);
	InventoryPageController = NewObject<UCatInventoryPageController>(this);
	InventoryWidget = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	InventoryQuickbarWidget = CreateWidget<UCatInventoryQuickbarWidget>(Controller, InventoryQuickbarViewClass);
	LakeMainMenuController = NewObject<UCatLakeMainMenuController>(this);
	LakeMainMenuWidget = CreateWidget<UCatLakeMainMenuWidget>(Controller, LakeMainMenuViewClass);
	InteractionPageController = NewObject<UCatInteractionPageController>(this);
	InteractionPromptWidget = CreateWidget<UCatInteractionPromptWidget>(Controller, InteractionPromptViewClass);
	if (!HUDModel || !HUDWidget || !InventoryPageController || !InventoryWidget || !InventoryQuickbarWidget
		|| !LakeMainMenuController || !LakeMainMenuWidget || !InteractionPageController || !InteractionPromptWidget)
	{
		DetachPlayerLakeUI();
		return;
	}
	HUDActionHandle = HUDWidget->OnActionRequested.AddUObject(this, &ThisClass::HandleHUDActionRequested);
	InventoryWidget->SetInventorySlotWidgetClass(InventorySlotViewClass);
	InventoryQuickbarWidget->SetInventorySlotWidgetClass(QuickbarSlotViewClass);
	InventoryQuickbarWidget->SetBackPackContext(Cast<UCatBackPackComponent>(Character->GetInventoryComponent()));
	if (!HUDModel->Bind(GetLocalPlayer(), Controller, Character))
	{
		DetachPlayerLakeUI();
		return;
	}
	HUDModelViewChangedHandle = HUDModel->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleHUDModelViewStateChanged);
	HUDWidget->AddToViewport(1);
	VoiceActivityWidget = CreateWidget<UCatVoiceActivityWidget>(Controller);
	if (VoiceActivityWidget) { VoiceActivityWidget->AddToViewport(5); }
	InventoryQuickbarWidget->AddToViewport(2);
	// 库存提示独立于页面但隶属于本玩家；类缺失只关闭提示并落盘，不影响既有库存操作。
	if (const TSubclassOf<UCatItemTooltipWidget> TooltipClass = Settings->LoadItemTooltipWidgetClass())
	{
		ItemTooltipWidget = CreateWidget<UCatItemTooltipWidget>(Controller, TooltipClass);
		// 库存页和 Aegis 提示都在视口层；玩家层的 ZOrder 无法跨层覆盖库存，必须沿用同一层级排序。
		if (ItemTooltipWidget) ItemTooltipWidget->AddToViewport(1100);
		if (ItemTooltipWidget && ItemTooltipWidget->IsInViewport())
		{
			ItemTooltipController = NewObject<UCatItemTooltipController>(this);
			ItemTooltipController->Bind(ItemTooltipWidget);
		}
		else
		{
			ItemTooltipWidget = nullptr;
		}
	}
	if (!ItemTooltipController)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_item_tooltip_unavailable World=%s Class=%s"),
			*GetPathNameSafe(GetWorld()), *Settings->ItemTooltipWidgetClass.ToSoftObjectPath().ToString());
	}
	HandleHUDModelViewStateChanged();
	// 图鉴页隶属于本玩家但不是装配前提；类或绑定失败只关闭三个图鉴入口并落盘，不影响 HUD、背包、菜单与 Profile 记录。
	if (const TSubclassOf<UCatCollectionWidget> CollectionViewClass = Settings->LoadCollectionWidgetClass())
	{
		CollectionWidget = CreateWidget<UCatCollectionWidget>(Controller, CollectionViewClass);
		if (CollectionWidget)
		{
			CollectionPageController = NewObject<UCatCollectionPageController>(this);
			if (!CollectionPageController->Bind(GetLocalPlayer(), Controller, CollectionWidget))
			{
				CollectionPageController = nullptr;
				CollectionWidget = nullptr;
			}
		}
	}
	if (!CollectionPageController)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_unavailable World=%s Class=%s"),
			*GetPathNameSafe(GetWorld()), *Settings->CollectionWidgetClass.ToSoftObjectPath().ToString());
	}
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
	WorldInfoController = NewObject<UCatWorldInfoController>(this);
	WorldInfoController->Bind(Controller);
	// 首解锁特写与新鱼种广播：本人那条读本机 Profile 落盘事实，别人那条读 GameState 公开广播，两条互不代替。
	if (!FishSpeciesFirstRecordedHandle.IsValid())
	{
		if (UCatProfileSubsystem* Profile = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UCatProfileSubsystem>() : nullptr)
		{
			FishSpeciesFirstRecordedHandle = Profile->OnFishSpeciesFirstRecorded.AddUObject(
				this, &ThisClass::HandleLocalFishSpeciesFirstRecorded);
		}
	}
	RefreshFishDiscoveryBinding();
	LogInventoryToggleKeyBinding();
	const UWorld* World = GetWorld();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UE_LOG(LogCatUI, Log,
		TEXT("Event=ui_player_modules_attached World=%s NetMode=%d LocalPlayerIndex=%d Controller=%s LocalController=%s HUD=%s HUDMode=minimal_main Inventory=%s Quickbar=%s Slot=%s QuickbarSlot=%s LakeMenu=%s Interaction=%s Collection=%s ShopPrecreated=false"),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		LocalPlayer ? LocalPlayer->GetLocalPlayerIndex() : INDEX_NONE,
		*GetNameSafe(Controller),
		Controller->IsLocalController() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(HUDWidget->GetClass()),
		*GetNameSafe(InventoryWidget->GetClass()),
		*GetNameSafe(InventoryQuickbarWidget->GetClass()),
		*GetNameSafe(InventorySlotViewClass.Get()),
		*GetNameSafe(QuickbarSlotViewClass.Get()),
		*GetNameSafe(LakeMainMenuWidget ? LakeMainMenuWidget->GetClass() : nullptr),
		*GetNameSafe(InteractionPromptWidget ? InteractionPromptWidget->GetClass() : nullptr),
		*GetNameSafe(CollectionWidget ? CollectionWidget->GetClass() : nullptr));
}

// 本地玩家 UI 解绑流程：
// 1. 先让 WorldInfo 释放观察距离和信息牌，再解绑悬停来源并移除提示视图。
// 2. 按图鉴、菜单、库存的顺序解绑控制器以恢复各自输入状态，再移除对应页面；尚未创建的对象直接跳过。
// 3. 解除 HUD Model 事件与玩法订阅，清掉 HUD 动作委托并移除 HUD，随后解绑交互提示控制器和视图。
// 4. 清空当前挂接角色并记录卸载日志；各步释放自身引用，允许装配失败后复用同一清理入口。
void UCatLocalPlayerUISubsystem::DetachPlayerLakeUI()
{
	if (WorldInfoController)
	{
		WorldInfoController->Unbind();
		WorldInfoController = nullptr;
	}
	ClearFishDiscoveryBinding();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FishRevealDismissTimerHandle);
	}
	if (FishRevealWidget)
	{
		FishRevealWidget->OnDismissRequested.RemoveAll(this);
		FishRevealWidget->RemoveFromParent();
		FishRevealWidget = nullptr;
	}
	// 先清理全局悬停来源，再移除 View；后续格子的 Destruct 不会再触发过期提示。
	if (ItemTooltipController)
	{
		ItemTooltipController->Unbind();
		ItemTooltipController = nullptr;
	}
	if (ItemTooltipWidget)
	{
		ItemTooltipWidget->RemoveFromParent();
		ItemTooltipWidget = nullptr;
	}
	if (CollectionPageController)
	{
		CollectionPageController->Unbind();
		CollectionPageController = nullptr;
	}
	if (CollectionWidget)
	{
		CollectionWidget->RemoveFromParent();
		CollectionWidget = nullptr;
	}
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
	if (InventoryQuickbarWidget)
	{
		InventoryQuickbarWidget->RemoveFromParent();
		InventoryQuickbarWidget = nullptr;
	}
	if (HUDModel)
	{
		HUDModel->OnViewStateChanged.Remove(HUDModelViewChangedHandle);
		HUDModel->Unbind();
		HUDModel = nullptr;
	}
	HUDModelViewChangedHandle.Reset();
	if (VoiceActivityWidget) { VoiceActivityWidget->RemoveFromParent(); VoiceActivityWidget = nullptr; }
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
	// 客户端的 GameState 可能比 Pawn 晚到，装配那一次接不上新鱼种广播。
	// HUD Model 自己有等待 GameState 的重试，所以借它的每次投影补一次接线——已经接上时这里直接返回。
	RefreshFishDiscoveryBinding();
	if (HUDModel && HUDWidget)
	{
		HUDWidget->RenderHUD(HUDModel->GetViewState());
	}
}

// 局内菜单切换流程：翻天期间拒绝打开；其他时候先关闭背包和图鉴，保证同一 Controller 只有一个菜单模态恢复记录。
void UCatLocalPlayerUISubsystem::ToggleLakeMainMenu()
{
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (Controller && Controller->IsDayTransitionInputBlocked()) return;
	if (!LakeMainMenuController)
	{
		return;
	}
	if (!LakeMainMenuController->IsMenuOpen())
	{
		if (InventoryPageController && InventoryPageController->IsInventoryOpen())
		{
			InventoryPageController->RequestCloseInventoryFromWidget();
		}
		if (CollectionPageController && CollectionPageController->IsCollectionOpen())
		{
			CollectionPageController->RequestCloseCollectionFromWidget();
		}
	}
	LakeMainMenuController->ToggleMenu();
	RefreshHUDAfterPageVisibilityChanged();
}

// HUD 入口动作流程：背包、主菜单和图鉴都转交已有控制器；HUD 不拼业务页面，也不持有保存或离局业务。
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
	case ECatHUDAction::OpenCollection:
		ToggleCollection();
		break;
	default:
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_hud_action_unknown Action=%d"), static_cast<int32>(Action));
		break;
	}
}
