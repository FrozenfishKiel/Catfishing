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

// 销毁流程：先释放拾取提示、HUD、背包和交互提示模块，让输入和 View 意图不再触达旧 Controller；再解绑 Controller、移除 Frontend View 与 Online 快照订阅。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachInteractionView();
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
	DetachInteractionView();
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

// 状态读取流程：从 PageController 读取唯一背包状态；未装配背包时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsInventoryOpen() const
{
	return InventoryPageController ? InventoryPageController->IsInventoryOpen() : false;
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
			DetachInteractionView();
			DetachPlayerLakeUI();
			UnbindController();
			BindController(Controller);
		}
		return;
	}
	HandleControllerPawnChanged(Controller->GetPawn());
}

// Native 交互接管流程：
// 1. 只接受当前 LocalPlayer 绑定的本地 Controller，避免服务器远端 Controller 借 UI 子系统吞掉玩法输入。
// 2. 交给 Interaction PageController 判断是否存在通用 UI 目标或本帧已消费记录。
// 3. 返回 true 时 PlayerController 不再执行旧准星交互，避免同一个 IA_Interact 同时打开商店和触发玩法动作。
bool UCatLocalPlayerUISubsystem::TryHandleNativeInteractionInput(APlayerController* Controller)
{
	if (!Controller || Controller != BoundPlayerController.Get() || !Controller->IsLocalController())
	{
		return false;
	}
	return InteractionPageController
		? InteractionPageController->TryHandleNativeInteractionInput()
		: false;
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
// 1. 先把 NewPawn 裁成项目猫身体；同一个已装配身体的重复通知会重读库存快照并刷新输入绑定，不拆掉正在打开的 UI。
// 2. 新身体或空身体会先完整拆掉上一套本地玩家 UI，避免跨 Pawn 复用 Model、View 或输入锁。
// 3. 只有新的 ACatCharacter 通过配置校验时才重新装配 HUD、背包、交互提示和拾取提示层。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	ACatCharacter* Character = Cast<ACatCharacter>(NewPawn);
	if (Character && AttachedPlayerLakeCharacter.Get() == Character
		&& HUDWidget && InventoryPageController && InteractionPageController)
	{
		InventoryPageController->RefreshModel();
		InventoryPageController->RefreshInputBinding();
		InteractionPageController->RefreshInputBinding();
		return;
	}
	DetachInteractionView();
	DetachPlayerLakeUI();
	if (!Character)
	{
		return;
	}
	AttachInteractionView(BoundPlayerController.Get(), Character);
	AttachPlayerLakeUI(Character);
	if (HUDWidget && InventoryPageController && InteractionPageController)
	{
		AttachedPlayerLakeCharacter = Character;
	}
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
		TEXT("Event=ui_player_modules_attached World=%s NetMode=%d LocalPlayerIndex=%d Controller=%s LocalController=%s HUD=%s Inventory=%s Slot=%s Interaction=%s ShopPrecreated=false"),
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

// 本地玩家 UI 解绑流程：PageController 先恢复输入和解绑 View 意图，Model 再解除玩法订阅，最后移除各自 WBP 并清引用。
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

// HUD 渲染转交流程：Model 已聚合状态和钓鱼反馈；Subsystem 只把它交给 HUD WBP。
void UCatLocalPlayerUISubsystem::HandleHUDModelViewStateChanged()
{
	if (HUDModel && HUDWidget)
	{
		HUDWidget->RenderHUD(HUDModel->GetViewState());
	}
}
