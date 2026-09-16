#include "UI/Inventory/CatInventoryPageController.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryContextMenuWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SObjectWidget.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/ItemTooltip/CatItemTooltipController.h"

/** 菜单专属的输入预处理器；只在菜单打开期间检查外部鼠标按下，并把原事件继续交还 Slate。 */
class FCatInventoryContextMenuInputProcessor final : public IInputProcessor
{
public:
	/** 保存页面控制器的弱引用；输入系统不能延长 LocalPlayer UI 或世界的生命周期。 */
	explicit FCatInventoryContextMenuInputProcessor(UCatInventoryPageController* InController) : Controller(InController) {}
	/** 此监听器只处理鼠标边沿；Slate 要求提供 Tick，但不轮询库存或维护逐帧状态。 */
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	/** 鼠标按下时若不在当前菜单几何内就取消菜单；始终返回 false，格子右键、左键选择和拖拽继续走原路由。 */
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UCatInventoryPageController* PageController = Controller.Get())
		{
			UCatInventoryContextMenuWidget* Menu = PageController->GetInventoryContextMenu();
			if (Menu && Menu->IsMenuOpen() && !Menu->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition())) { PageController->CancelInventoryContextMenu(); }
		}
		return false;
	}
private:
	/** 当前菜单所属页面控制器；页面解绑后弱引用失效，预处理器自然不再访问 UI。 */
	TWeakObjectPtr<UCatInventoryPageController> Controller;
};

// 先解除旧页面与输入，再验证当前本地角色；默认背包只绑定该角色库存，随后安装现有库存 Action。
bool UCatInventoryPageController::Bind(APlayerController* InController, UCatInventoryWidget* InView)
{
	Unbind();
	ACatCharacter* Character = InController ? Cast<ACatCharacter>(InController->GetPawn()) : nullptr;
	if (!InController || !InController->IsLocalController() || !InView || !Character || !Character->GetInventoryComponent())
	{
		return false;
	}
	BoundPlayerController = InController;
	DefaultInventoryView = InView;
	BoundView = InView;
	InView->SetInventoryContext(Character->GetInventoryComponent());
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		InventoryActionResultHandle = CatController->OnCampCommandResultReceived.AddUObject(this, &ThisClass::HandleInventoryActionCommandResult);
	}
	InstallInventoryInput();
	return true;
}

// 先释放视口与模态输入，再移除 Action 和页面引用；各 WBP 在移出时自行解绑所属库存 Model。
void UCatInventoryPageController::Unbind()
{
	SetInventoryOpen(false);
	CancelInventoryContextMenu(false);
	if (InventoryContextMenu)
	{
		InventoryContextMenu->RemoveFromParent();
		InventoryContextMenu = nullptr;
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()); CatController && InventoryActionResultHandle.IsValid())
	{
		CatController->OnCampCommandResultReceived.Remove(InventoryActionResultHandle);
	}
	InventoryActionResultHandle.Reset();
	PendingInventoryActionRequestId.Invalidate();
	RemoveInventoryInput();
	BoundView = nullptr;
	DefaultInventoryView.Reset();
	BoundPlayerController.Reset();
}

// 关闭交互页后 BoundView 已回到默认背包；普通切换不改任何库存 Model 或世界上下文。
void UCatInventoryPageController::ToggleInventory()
{
	SetInventoryOpen(!bInventoryOpen);
}

// 所有世界库存采用相同打开流程：关闭旧页，验证资源，创建指定 WBP，注入库存并入视口。
// 嵌套的普通背包 WBP 没有被注入外部库存，构建时会独立绑定 owning Pawn 的背包。
bool UCatInventoryPageController::OpenInventory(UCatInventoryComponent* Inventory,
	const TSubclassOf<UCatInventoryWidget> InventoryViewClass)
{
	SetInventoryOpen(false);
	APlayerController* Controller = BoundPlayerController.Get();
	const TSubclassOf<UCatInventorySlotWidget> SlotClass = GetDefault<UCatUISettings>()->LoadInventorySlotWidgetClass();
	if (!Controller || !Inventory || !InventoryViewClass || !SlotClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_open_rejected World=%s Reason=DependencyUnavailable Inventory=%s ViewClass=%s"),
			*GetPathNameSafe(GetWorld()), *GetPathNameSafe(Inventory), *GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	UCatInventoryWidget* View = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	if (!View)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_open_rejected World=%s Reason=WidgetCreationFailed ViewClass=%s"),
			*GetPathNameSafe(Controller->GetWorld()), *GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	View->SetInventorySlotWidgetClass(SlotClass);
	View->SetInventoryContext(Inventory);
	BoundView = View;
	SetInventoryOpen(true);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_opened World=%s NetMode=%d Inventory=%s View=%s Opened=%s"),
		*GetPathNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
		*GetPathNameSafe(Inventory), *GetNameSafe(View), bInventoryOpen ? TEXT("true") : TEXT("false"));
	return bInventoryOpen;
}

// 只返回本窗口状态，不从 Model、Widget 可见性或鼠标状态拼接第二份状态。
bool UCatInventoryPageController::IsInventoryOpen() const
{
	return bInventoryOpen;
}

// 输入链完成装配后重装同一 Action，安装函数先移除旧绑定。
void UCatInventoryPageController::RefreshInputBinding()
{
	InstallInventoryInput();
}

// 迟到的关闭请求不会重新打开窗口；关闭路径与键盘切换共用。
void UCatInventoryPageController::RequestCloseInventoryFromWidget()
{
	CancelInventoryContextMenu();
	SetInventoryOpen(false);
}

// 右键打开流程：
// 1. 先收起任何旧菜单并强制隐藏 Tooltip，避免同一鼠标位置同时显示两层信息。
// 2. 从当前格只读条目取得定义动作，逐项调用实例预检生成可用性和原因；不按库存类型或物品子类分支。
// 3. 仅在正式菜单 WBP 可创建时保存源格并显示；提交时会再次重读该格，不能信任这里的快照。
void UCatInventoryPageController::OpenInventoryContextMenu(UCatInventorySlotWidget* SourceSlot, const FVector2D& ScreenPosition)
{
	// 未回执时保留本次请求的反馈归属；关闭页面会清除此等待，不允许旧视图覆盖它。
	if (PendingInventoryActionRequestId.IsValid()) return;
	CancelInventoryContextMenu(false);
	if (ULocalPlayer* LocalPlayer = BoundPlayerController.IsValid() ? BoundPlayerController->GetLocalPlayer() : nullptr)
	{
		if (UCatLocalPlayerUISubsystem* UI = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>(); UI)
		{
			if (UCatItemTooltipController* Tooltip = UI->GetItemTooltipController()) { Tooltip->SetContextMenuSuppressed(true); }
		}
	}
	if (!bInventoryOpen || !IsValid(SourceSlot))
	{
		CancelInventoryContextMenu();
		return;
	}
	const FCatInventoryEntry& Entry = SourceSlot->GetInventoryEntry();
	UCatInventoryItemInstance* Instance = Entry.Instance;
	const UCatInventoryItemDefinition* Definition = Instance ? Instance->GetItemDefinition() : nullptr;
	APawn* UserPawn = BoundPlayerController.IsValid() ? BoundPlayerController->GetPawn() : nullptr;
	if (!SourceSlot->GetSourceInventory() || Entry.StackCount <= 0 || !Instance || !Definition || !UserPawn)
	{
		CancelInventoryContextMenu();
		return;
	}
	if (!InventoryContextMenu)
	{
		const TSubclassOf<UCatInventoryContextMenuWidget> MenuClass = GetDefault<UCatUISettings>()->LoadInventoryContextMenuWidgetClass();
		if (!MenuClass)
		{
			UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_context_menu_rejected Reason=MenuClassUnavailable"));
			CancelInventoryContextMenu();
		return;
		}
		InventoryContextMenu = CreateWidget<UCatInventoryContextMenuWidget>(BoundPlayerController.Get(), MenuClass);
		if (!InventoryContextMenu)
		{
			CancelInventoryContextMenu();
		return;
		}
		InventoryContextMenu->OnActionChosen.BindUObject(this, &ThisClass::SubmitInventoryContextAction);
		InventoryContextMenu->OnMenuCancelled.BindUObject(this, &ThisClass::HandleInventoryContextMenuCancelled);
		InventoryContextMenu->AddToViewport(30);
	}
	ContextMenuSourceInventory = SourceSlot->GetSourceInventory();
	ContextMenuSourceSlotIndex = SourceSlot->GetSlotIndex();
	ContextMenuItemInstanceId = Instance->GetItemInstanceId();
	ContextMenuScreenPosition = ScreenPosition;
	PresentInventoryContextMenuForStoredSource(Entry);
}

// 取消流程：菜单和来源同时失效；Tooltip 的抑制状态由 Tooltip Controller 保留到下一次真实 Slate 命中，不能回显关闭前的旧格。
void UCatInventoryPageController::CancelInventoryContextMenu(const bool bRestoreTooltipFromCurrentSlateHit)
{
	if (InventoryContextMenu)
	{
		InventoryContextMenu->Dismiss();
	}
	ContextMenuSourceInventory.Reset();
	ContextMenuSourceSlotIndex = INDEX_NONE;
	ContextMenuItemInstanceId.Invalidate();
	if (ULocalPlayer* LocalPlayer = BoundPlayerController.IsValid() ? BoundPlayerController->GetLocalPlayer() : nullptr)
	{
		if (UCatLocalPlayerUISubsystem* UI = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>())
		{
			if (UCatItemTooltipController* Tooltip = UI->GetItemTooltipController()) { Tooltip->SetContextMenuSuppressed(bRestoreTooltipFromCurrentSlateHit && bInventoryOpen); }
		}
	}
	RemoveInventoryContextMenuSlateHooks();
	if (bRestoreTooltipFromCurrentSlateHit && bInventoryOpen) { ScheduleTooltipRestoreFromSlateHit(); }
}

// 菜单状态读取流程：只查询控制器持有的唯一 View，库存页面不从 Widget 可见性建立第二份状态。
bool UCatInventoryPageController::IsInventoryContextMenuOpen() const
{
	return InventoryContextMenu && InventoryContextMenu->IsMenuOpen();
}

// 输入监听借用现有菜单几何进行命中判断；菜单所有权仍留在页面控制器，失效时返回空让监听忽略事件。
UCatInventoryContextMenuWidget* UCatInventoryPageController::GetInventoryContextMenu() const { return InventoryContextMenu; }

// 显示菜单后注册弱引用输入监听；重复打开沿用已有监听，关闭时成对注销。
void UCatInventoryPageController::InstallInventoryContextMenuInputProcessor()
{
	if (!InventoryContextMenuInputProcessor.IsValid())
	{
		InventoryContextMenuInputProcessor = MakeShared<FCatInventoryContextMenuInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InventoryContextMenuInputProcessor);
	}
}

// 先注销菜单输入再移除待恢复回调；关闭页面、替换来源和解绑共用，防止旅行后旧回调回显提示。
void UCatInventoryPageController::RemoveInventoryContextMenuSlateHooks()
{
	if (InventoryContextMenuInputProcessor.IsValid()) { FSlateApplication::Get().UnregisterInputPreProcessor(InventoryContextMenuInputProcessor); InventoryContextMenuInputProcessor.Reset(); }
	if (TooltipRestorePostTickHandle.IsValid()) { FSlateApplication::Get().OnPostTick().Remove(TooltipRestorePostTickHandle); TooltipRestorePostTickHandle.Reset(); }
}

// 只安排一次 Slate 帧尾恢复：先注销自身，再检查页面和真实命中路径；只恢复本玩家的有效库存格，空白或其他窗口保持隐藏。
void UCatInventoryPageController::ScheduleTooltipRestoreFromSlateHit()
{
	if (TooltipRestorePostTickHandle.IsValid()) { return; }
	TWeakObjectPtr<UCatInventoryPageController> WeakThis(this);
	TooltipRestorePostTickHandle = FSlateApplication::Get().OnPostTick().AddWeakLambda(this, [WeakThis](float DeltaTime)
	{
		UCatInventoryPageController* PageController = WeakThis.Get();
		if (!PageController) { return; }
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.OnPostTick().Remove(PageController->TooltipRestorePostTickHandle);
		PageController->TooltipRestorePostTickHandle.Reset();
		if (!PageController->bInventoryOpen || PageController->IsInventoryContextMenuOpen()) return;
		ULocalPlayer* Owner = PageController->BoundPlayerController.IsValid() ? PageController->BoundPlayerController->GetLocalPlayer() : nullptr;
		UCatLocalPlayerUISubsystem* UI = Owner ? Owner->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
		UCatItemTooltipController* Tooltip = UI ? UI->GetItemTooltipController() : nullptr;
		if (!Tooltip) return;
		Tooltip->SetContextMenuSuppressed(false);
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Slate.GetCursorPos(), Slate.GetInteractiveTopLevelWindows());
		for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
		{
			const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
			if (Widget->GetWidgetClass().GetWidgetType() != SObjectWidget::StaticWidgetClass().GetWidgetType()) { continue; }
			if (UCatInventorySlotWidget* Slot = Cast<UCatInventorySlotWidget>(StaticCastSharedRef<SObjectWidget>(Widget)->GetWidgetObject()))
			{
				if (Slot->GetOwningLocalPlayer() == Owner && IsValid(Slot->GetSourceInventory()) && Slot->IsVisible()) Tooltip->ShowTooltip(Slot, Slate.GetCursorPos());
				return;
			}
		}
	});
}

// 刷新流程：只处理当前菜单所属库存；同一实例仍在同一格时刷新行状态和数量上限，换物、移动或清空才关闭。
void UCatInventoryPageController::RefreshInventoryContextMenuForInventory(UCatInventoryComponent* ChangedInventory)
{
	if (!InventoryContextMenu || !InventoryContextMenu->IsMenuOpen() || !ChangedInventory || ChangedInventory != ContextMenuSourceInventory.Get())
	{
		return;
	}
	const TArray<FCatInventoryEntry>& Entries = ChangedInventory->GetInventoryModel()->GetInventoryList();
	if (!Entries.IsValidIndex(ContextMenuSourceSlotIndex) || !Entries[ContextMenuSourceSlotIndex].Instance
		|| Entries[ContextMenuSourceSlotIndex].Instance->GetItemInstanceId() != ContextMenuItemInstanceId || Entries[ContextMenuSourceSlotIndex].StackCount <= 0)
	{
		CancelInventoryContextMenu(false);
		return;
	}
	PresentInventoryContextMenuForStoredSource(Entries[ContextMenuSourceSlotIndex]);
}

// 菜单投影流程：从固定实例的当前定义读取有序动作，并在每次刷新时重跑实例可用性预检，把状态变化直接反映为置灰原因和数量上限。
void UCatInventoryPageController::PresentInventoryContextMenuForStoredSource(const FCatInventoryEntry& Entry)
{
	UCatInventoryItemInstance* Instance = Entry.Instance;
	const UCatInventoryItemDefinition* Definition = Instance ? Instance->GetItemDefinition() : nullptr;
	APawn* UserPawn = BoundPlayerController.IsValid() ? BoundPlayerController->GetPawn() : nullptr;
	if (!InventoryContextMenu || !Instance || !Definition || !UserPawn || Entry.StackCount <= 0)
	{
		CancelInventoryContextMenu(false);
		return;
	}
	TArray<FText> UnavailableReasons;
	UnavailableReasons.Reserve(Definition->InventoryActions.Num());
	for (const FCatInventoryActionDefinition& ActionDefinition : Definition->InventoryActions)
	{
		FText Reason;
		UnavailableReasons.Add(Instance->CanExecuteInventoryAction(ActionDefinition.Action, Entry, UserPawn, Reason)
			? FText::GetEmpty() : (Reason.IsEmpty() ? NSLOCTEXT("CatInventory", "ActionUnavailable", "当前不可用") : Reason));
	}
	InventoryContextMenu->PresentActions(Definition->InventoryActions, UnavailableReasons, Entry.StackCount, ContextMenuScreenPosition);
	// 没有有效操作时视图不会打开；仍须走成对关闭，解除 Tooltip 抑制并撤销旧输入监听。
	if (!InventoryContextMenu->IsMenuOpen())
	{
		CancelInventoryContextMenu();
		return;
	}
	InstallInventoryContextMenuInputProcessor();
}

// 提交流程：
// 1. 从当前来源格重新读取库存、槽位、实例和数量，拒绝刷新后换物或来源销毁。
// 2. 调用实例的同一可用性接口，防止菜单打开后状态变化仍提交旧资格。
// 3. 生成 RequestId、登记回执关联并立即关闭菜单，再把唯一事实参数交给服务器 RPC。
void UCatInventoryPageController::SubmitInventoryContextAction(const FGameplayTag Action, const int32 Quantity)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	UCatInventoryComponent* SourceInventory = ContextMenuSourceInventory.Get();
	const TArray<FCatInventoryEntry>* Entries = SourceInventory ? &SourceInventory->GetInventoryModel()->GetInventoryList() : nullptr;
	const FCatInventoryEntry Entry = Entries && Entries->IsValidIndex(ContextMenuSourceSlotIndex) ? (*Entries)[ContextMenuSourceSlotIndex] : FCatInventoryEntry();
	UCatInventoryItemInstance* Instance = Entry.Instance;
	APawn* UserPawn = Controller ? Controller->GetPawn() : nullptr;
	FText Reason;
	const bool bValid = Controller && SourceInventory && ContextMenuSourceSlotIndex != INDEX_NONE && Entry.StackCount > 0
		&& Instance && Entry.Instance->GetItemInstanceId() == ContextMenuItemInstanceId && Action.IsValid() && Quantity > 0 && Quantity <= Entry.StackCount
		&& Instance->CanExecuteInventoryAction(Action, Entry, UserPawn, Reason);
	if (!bValid)
	{
		CancelInventoryContextMenu();
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	PendingInventoryActionRequestId = RequestId;
	AActor* SourceHost = SourceInventory->GetOwner();
	const int32 SourceSlotIndex = ContextMenuSourceSlotIndex;
	const FGuid ItemInstanceId = ContextMenuItemInstanceId;
	CancelInventoryContextMenu();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_action_submitted World=%s NetMode=%d RequestId=%s SourceHost=%s SourceIndex=%d Item=%s Action=%s Quantity=%d"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(Controller->GetNetMode()), *RequestId.ToString(), *GetNameSafe(SourceHost),
		SourceSlotIndex, *ItemInstanceId.ToString(), *Action.ToString(), Quantity);
	Controller->ServerExecuteInventoryAction(RequestId, SourceHost, SourceSlotIndex, ItemInstanceId, Action, Quantity,
		Action == CatInventoryActionTags::Use ? Instance->CaptureUseTarget(Controller) : FCatInventoryUseTarget());
}

// 菜单回调流程：取消不恢复旧 Tooltip，只清理控制器保存的来源上下文。
void UCatInventoryPageController::HandleInventoryContextMenuCancelled()
{
	CancelInventoryContextMenu();
}

// 回执流程：只接受本菜单提交的 RequestId；匹配后清除等待标识并让当前页面使用既有结果文本显示服务器终态。
void UCatInventoryPageController::HandleInventoryActionCommandResult(const FCatDomainCommandResult& Result)
{
	if (!PendingInventoryActionRequestId.IsValid() || Result.RequestId != PendingInventoryActionRequestId)
	{
		return;
	}
	PendingInventoryActionRequestId.Invalidate();
	if (BoundView)
	{
		BoundView->ShowInventoryActionResult(Result);
	}
}

// 打开先加入视口再申请输入锁；关闭先撤销全局提示、恢复输入再移出页面，交互页关闭后释放，默认背包留给下一次打开。
void UCatInventoryPageController::SetInventoryOpen(const bool bOpen)
{
	if (bInventoryOpen == bOpen)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (bOpen)
	{
		if (!Controller || !BoundView)
		{
			return;
		}
		BoundView->AddToViewport(10);
		bInventoryOpen = true;
		CatUIModalInputMode::Open(Controller, BoundView, ModalInputModeState);
		return;
	}
	CancelInventoryContextMenu(false);
	PendingInventoryActionRequestId.Invalidate();
	if (ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr)
	{
		if (UCatLocalPlayerUISubsystem* UI = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>())
		{
			if (UCatItemTooltipController* Tooltip = UI->GetItemTooltipController()) Tooltip->ForceHideTooltip();
		}
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
	bInventoryOpen = false;
	if (BoundView)
	{
		BoundView->RemoveFromParent();
	}
	BoundView = DefaultInventoryView.Get();
}

// 从已有输入配置加载库存 Action；与主菜单共用时避让，只安装 Action 回调，不另建 IMC 或硬编码按键。
void UCatInventoryPageController::InstallInventoryInput()
{
	RemoveInventoryInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ToggleAction = Settings->LoadInventoryToggleAction();
	UInputAction* MainMenuAction = Settings->LoadMainMenuToggleAction();
	const UInputMappingContext* MappingContext = Settings->LoadGameplayInputMappingContext();
	if (!Input || !ToggleAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller), *Settings->InventoryToggleAction.ToSoftObjectPath().ToString(),
			*Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString());
		return;
	}
	if (MainMenuAction && ToggleAction == MainMenuAction)
	{
		return;
	}
	AppliedInventoryToggleAction = ToggleAction;
	InventoryInputBindingHandle = Input->BindAction(AppliedInventoryToggleAction, ETriggerEvent::Started,
		this, &ThisClass::ToggleInventory).GetHandle();
	BoundInventoryInputComponent = Input;
}

// 从当初绑定的输入组件精确移除句柄；不修改基础 IMC，重复调用保持无操作。
void UCatInventoryPageController::RemoveInventoryInput()
{
	if (UEnhancedInputComponent* Input = BoundInventoryInputComponent.Get(); Input && InventoryInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(InventoryInputBindingHandle);
	}
	BoundInventoryInputComponent.Reset();
	InventoryInputBindingHandle = 0;
	AppliedInventoryToggleAction = nullptr;
}
