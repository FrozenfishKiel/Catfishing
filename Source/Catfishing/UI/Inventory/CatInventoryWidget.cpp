#include "UI/Inventory/CatInventoryWidget.h"

#include "Character/CatCharacter.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/ItemTooltip/CatItemTooltipController.h"

// 上下文切换流程：先解绑原 Model，再清除容器级等待状态和结果，随后绑定新库存并重读完整列表。
void UCatInventoryWidget::SetInventoryContext(UCatInventoryComponent* InInventory)
{
	UnbindInventoryModel();
	if (DisplayInventory.Get() != InInventory)
	{
		if (DisplayInventory.IsValid()) if (UCatInventoryPageController* Page = ResolveInventoryPageController()) Page->CancelInventoryContextMenu(false);
		PendingCommandRequestId.Invalidate();
		if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	}
	DisplayInventory = InInventory;
	if (InInventory)
	{
		UCatInventoryModel* Model = InInventory->GetInventoryModel();
		InventoryModelChangedHandle = Model->OnInventoryListChanged.AddUObject(this, &ThisClass::RefreshInventorySlots);
	}
	RefreshInventorySlots();
}

// 数据源读取流程：只返回本页面已绑定的库存组件；空值表示页面尚未装配完成。
UCatInventoryComponent* UCatInventoryWidget::GetInventoryContext() const { return DisplayInventory.Get(); }

// 格子类设置流程：保存正式 WBP 类，下一次刷新统一创建，不即时改写当前格子树。
void UCatInventoryWidget::SetInventorySlotWidgetClass(const TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass) { InventorySlotWidgetClass = InSlotWidgetClass; }

// 构建流程：恢复本页回执订阅，再解析显示库存并进入同一上下文绑定入口；退出仍由按键交给页面控制器。
void UCatInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PendingCommandRequestId.Invalidate();
	if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	CommandResultController = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	if (ACatfishingPlayerController* Controller = CommandResultController.Get()) { Controller->OnCampCommandResultReceived.AddUObject(this, &ThisClass::HandleInventoryCommandResult); }
	if (!InventorySlotWidgetClass) { InventorySlotWidgetClass = GetDefault<UCatUISettings>()->LoadInventorySlotWidgetClass(); }
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory) { if (const ACatCharacter* Character = Cast<ACatCharacter>(GetOwningPlayerPawn())) { Inventory = Character->GetInventoryComponent(); } }
	SetInventoryContext(Inventory);
}

// 销毁流程：解除回执和 Model 监听，再撤销格子 Tooltip 来源与选择委托，最后释放设计器按钮绑定。
void UCatInventoryWidget::NativeDestruct()
{
	if (ACatfishingPlayerController* Controller = CommandResultController.Get()) { Controller->OnCampCommandResultReceived.RemoveAll(this); }
	CommandResultController.Reset();
	PendingCommandRequestId.Invalidate();
	UnbindInventoryModel();
	UnbindSlotWidgets();
	Super::NativeDestruct();
}

// 刷新流程：库存通知可能移动或换物，先使 PageController 菜单来源失效，再撤销旧格并按最新 Model 重建。
void UCatInventoryWidget::RefreshInventorySlots()
{
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController()) { PageController->RefreshInventoryContextMenuForInventory(DisplayInventory.Get()); }
	UnbindSlotWidgets();
	SelectedSlotIndex = INDEX_NONE;
	if (!InventorySlotWrapBox) { return; }
	InventorySlotWrapBox->ClearChildren();
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory || !InventorySlotWidgetClass) { return; }
	const TArray<FCatInventoryEntry>& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	SlotWidgets.SetNum(Entries.Num());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget) { continue; }
		SlotWidget->SetSlotContext(Index, Inventory, Entries[Index]);
		// 背包只提供拖拽、悬停与右键菜单，不接收物品栏选择；其他容器保留已有页面局部选择。
		if (!Cast<UCatBackPackComponent>(Inventory))
		{
			SlotWidget->OnSlotSelected.AddUObject(this, &ThisClass::RequestSelectSlot);
		}
		InventorySlotWrapBox->AddChildToWrapBox(SlotWidget);
		SlotWidgets[Index] = SlotWidget;
	}
}

// Model 解绑流程：只从旧显示库存移除确切句柄；没有库存时清空句柄即可。
void UCatInventoryWidget::UnbindInventoryModel()
{
	if (UCatInventoryComponent* Inventory = DisplayInventory.Get())
	{
		UCatInventoryModel* Model = Inventory->GetInventoryModel();
		if (InventoryModelChangedHandle.IsValid()) Model->OnInventoryListChanged.Remove(InventoryModelChangedHandle);
	}
	InventoryModelChangedHandle.Reset();
}

// 格子解绑流程：逐个撤销 Tooltip 与选择监听，再清空引用；不能依赖 WidgetTree 重建时额外发送 MouseLeave。
void UCatInventoryWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget) { SlotWidget->CancelTooltip(); SlotWidget->OnSlotSelected.RemoveAll(this); }
	}
	SlotWidgets.Reset();
}

// 页面选择流程：个人背包不支持选择，直接忽略外部点击调用；其他容器只维护原有页面下标，不连接物品栏控制器。
void UCatInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	if (Cast<UCatBackPackComponent>(DisplayInventory.Get())) return;
	SelectedSlotIndex = SlotWidgets.IsValidIndex(SlotIndex) && SlotWidgets[SlotIndex] ? SlotIndex : INDEX_NONE;
}

// 选中条目读取流程：从当前 Model 重读下标，空格和失效实例不能成为容器级提交目标。
bool UCatInventoryWidget::GetSelectedInventoryEntry(FCatInventoryEntry& OutEntry, int32& OutSlotIndex) const
{
	OutEntry = FCatInventoryEntry(); OutSlotIndex = INDEX_NONE;
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory) { return false; }
	const TArray<FCatInventoryEntry>& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	if (!Entries.IsValidIndex(SelectedSlotIndex) || !IsValid(Entries[SelectedSlotIndex].Instance.Get()) || Entries[SelectedSlotIndex].StackCount <= 0) { return false; }
	OutEntry = Entries[SelectedSlotIndex]; OutSlotIndex = SelectedSlotIndex; return true;
}

// 关闭流程：交由 PageController 取消菜单和恢复输入；库存 Model 保持原样供下次打开重用。
void UCatInventoryWidget::RequestCloseInventory() { if (UCatInventoryPageController* Controller = ResolveInventoryPageController()) { Controller->RequestCloseInventoryFromWidget(); } }

// 控制器解析流程：关闭和菜单状态只读取 LocalPlayer 持有的唯一页面控制器。
UCatInventoryPageController* UCatInventoryWidget::ResolveInventoryPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetInventoryPageController() : nullptr;
}

// 关闭键判断流程：Escape 和背包切换键始终关闭；外部库存额外接受交互键，普通背包不抢占世界交互。
bool UCatInventoryWidget::ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const
{
	const UCatInventoryPageController* Controller = ResolveInventoryPageController();
	if (!Controller || !Controller->IsInventoryOpen()) { return false; }
	const FName Key = InKeyEvent.GetKey().GetFName(); const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (Key == EKeys::Escape.GetFName() || Key == Settings->ResolveInventoryToggleKeyName()) { return true; }
	const UCatInventoryComponent* Inventory = DisplayInventory.Get();
	return Inventory && Inventory->GetOwner() != GetOwningPlayerPawn() && Key == Settings->ResolveInteractionConfirmKeyName();
}

// 预览键流程：F8/F9 优先走祭坛确认；Escape 在菜单打开时仅取消菜单，其他关闭键继续关闭页面。
FReply UCatInventoryWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer()); Controller && Controller->TrySetAltarConfirmationFromKey(InKeyEvent.GetKey())) { return FReply::Handled(); }
	if (UCatInventoryPageController* PageController = ResolveInventoryPageController(); InKeyEvent.GetKey() == EKeys::Escape && PageController && PageController->IsInventoryContextMenuOpen()) { PageController->CancelInventoryContextMenu(); return FReply::Handled(); }
	if (ShouldCloseInventoryFromKey(InKeyEvent)) { RequestCloseInventory(); return FReply::Handled(); }
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 根键流程：焦点落在页面根时复用预览键的完整路由，保持菜单 Escape、祭坛键和关闭键的时序一致。
FReply UCatInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) { return NativeOnPreviewKeyDown(InGeometry, InKeyEvent); }


// 请求登记流程：容器级批量操作在 RPC 前保存关联 ID并刷新格子，菜单操作使用 PageController 的独立关联状态。
void UCatInventoryWidget::BeginInventoryCommand(const FGuid& RequestId)
{
	PendingCommandRequestId = RequestId;
	if (InventoryActionResultText) { InventoryActionResultText->SetText(FText::GetEmpty()); }
	RefreshInventorySlots();
}

// 回执流程：只消费本页容器级 RequestId；排队时保留关联，最终结果到达后清除并重读库存。
void UCatInventoryWidget::HandleInventoryCommandResult(const FCatDomainCommandResult& Result)
{
	if (!PendingCommandRequestId.IsValid() || Result.RequestId != PendingCommandRequestId) { return; }
	if (!Result.bPending) PendingCommandRequestId.Invalidate();
	ShowInventoryActionResult(Result);
}

// 结果展示流程：先区分正在排队与最终回执，再显示成功或拒绝并刷新 Model；排队只代表受理，不代表已经扣除物品。
void UCatInventoryWidget::ShowInventoryActionResult(const FCatDomainCommandResult& Result)
{
	if (InventoryActionResultText)
	{
		InventoryActionResultText->SetText(Result.bPending ? NSLOCTEXT("Catfishing", "InventoryActionQueued", "正在依次丢弃")
			: Result.FailureReason == TEXT("PartialDrop") ? NSLOCTEXT("Catfishing", "InventoryDropPartial", "部分物品未能丢出，请检查剩余物品")
			: CatIsAcceptedDomainCommandResult(Result) ? NSLOCTEXT("Catfishing", "InventoryActionSucceeded", "操作成功")
			: NSLOCTEXT("Catfishing", "InventoryActionRejected", "操作未完成，请重新选择物品"));
	}
	RefreshInventorySlots();
}

// Slate 命中刷新流程：菜单撤销后遍历当前重新创建的格子，只把仍处于真实悬停状态的有效格交给 Tooltip；没有命中则保持隐藏。
void UCatInventoryWidget::RefreshTooltipAtCurrentSlateHit()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	UCatItemTooltipController* Tooltip = UI ? UI->GetItemTooltipController() : nullptr;
	if (!Tooltip) { return; }
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget && SlotWidget->IsHovered())
		{
			Tooltip->ShowTooltip(SlotWidget, FSlateApplication::Get().GetCursorPos());
			return;
		}
	}
}
