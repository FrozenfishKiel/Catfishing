#include "UI/Inventory/CatInventoryQuickbarWidget.h"

#include "Components/WrapBox.h"
#include "Inventory/CatBackPackComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 背包上下文切换流程：
// 1. 解除旧 Model 和旧格子，防止换 Pawn 后继续显示旧角色的数据。
// 2. 只读订阅背包的库存列表；物品栏焦点另从本地 Controller 订阅，背包窗口不参与。
// 3. 立即读取当前快照，覆盖通知发生在 View 创建之前的冷启动。
void UCatInventoryQuickbarWidget::SetBackPackContext(UCatBackPackComponent* InBackPack)
{
	if (BackPack.Get() == InBackPack)
	{
		RefreshSlots();
		return;
	}
	UnbindInventoryModel();
	UnbindSlotWidgets();
	BackPack = InBackPack;
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer()))
	{
		SelectionChangedHandle = Controller->OnQuickbarSelectionChanged.AddUObject(this, &ThisClass::RefreshSelectedSlot);
	}
	if (UCatInventoryModel* Model = InBackPack ? InBackPack->GetInventoryModel() : nullptr)
	{
		InventoryModelChangedHandle = Model->OnInventoryListChanged.AddUObject(this, &ThisClass::RefreshSlots);

	}
	RefreshSlots();
}

// 格子类写入流程：保存 Settings 指向的正式类，随后按当前背包列表重建；没有上下文时只保留类供后续装配使用。
void UCatInventoryQuickbarWidget::SetInventorySlotWidgetClass(const TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass)
{
	InventorySlotWidgetClass = InSlotWidgetClass;
	RefreshSlots();
}

// 格子数量读取流程：返回本 View 已创建的控件数量；它随同一 Model 列表刷新，不读取或配置独立快捷栏容量。
int32 UCatInventoryQuickbarWidget::GetDisplayedSlotCount() const
{
	return SlotWidgets.Num();
}

// 外圈读取流程：只检查目标格是否已绑定并读取其正式可见状态；无效格返回 false，不触发刷新或选择。
bool UCatInventoryQuickbarWidget::IsDisplayedSlotSelected(const int32 SlotIndex) const
{
	const UCatInventorySlotWidget* SlotWidget = SlotWidgets.IsValidIndex(SlotIndex) ? SlotWidgets[SlotIndex] : nullptr;
	return SlotWidget && SlotWidget->IsSelectedFromModel();
}

// 构建流程：先由配置补齐正式格子类，再读已经注入的背包；View 从不创建、修改或选择库存条目。
void UCatInventoryQuickbarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!InventorySlotWidgetClass)
	{
		InventorySlotWidgetClass = GetDefault<UCatUISettings>()->LoadInventoryQuickbarSlotWidgetClass();
	}
	RefreshSlots();
}

// 销毁流程：解除 Model 通知与所有动态格子，随后释放背包弱引用；不反向写入玩家选中或库存状态。
void UCatInventoryQuickbarWidget::NativeDestruct()
{
	UnbindInventoryModel();
	UnbindSlotWidgets();
	BackPack.Reset();
	Super::NativeDestruct();
}

// 列表刷新流程：
// 1. 先撤销旧格子，避免旧物品悬停和新列表并存。
// 2. 按同一 Model 的完整列表原序创建格子，包含空格，因此格数永远等于背包真实容量。
// 3. 读取本地 Controller 的物品栏焦点设置外圈；Widget 不维护第二份选中索引。
void UCatInventoryQuickbarWidget::RefreshSlots()
{
	UnbindSlotWidgets();
	if (!QuickbarSlotWrapBox)
	{
		return;
	}
	QuickbarSlotWrapBox->ClearChildren();
	UCatBackPackComponent* CurrentBackPack = BackPack.Get();
	UCatInventoryModel* Model = CurrentBackPack ? CurrentBackPack->GetInventoryModel() : nullptr;
	if (!CurrentBackPack || !Model || !InventorySlotWidgetClass)
	{
		return;
	}
	const TArray<FCatInventoryEntry>& Entries = Model->GetInventoryList();
	const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	const int32 SelectedSlotIndex = Controller ? Controller->GetSelectedQuickbarSlotIndex() : INDEX_NONE;
	SlotWidgets.Reserve(Entries.Num());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->SetSlotContext(Index, CurrentBackPack, Entries[Index]);
		SlotWidget->SetAcceptsSlotInput(false);
		SlotWidget->SetSelectedFromModel(Index == SelectedSlotIndex);
		QuickbarSlotWrapBox->AddChildToWrapBox(SlotWidget);
		SlotWidgets.Add(SlotWidget);
	}
}

// 选择刷新流程：Controller 的本地物品栏焦点变化后遍历当前已建格更新外圈；列表还未解析到该槽时不会点亮不存在的格子。
void UCatInventoryQuickbarWidget::RefreshSelectedSlot(const int32 SelectedSlotIndex)
{
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->SetSelectedFromModel(SlotWidget->GetSlotIndex() == SelectedSlotIndex);
		}
	}
}

// Model 解绑流程：分别从背包 Model 和本地 Controller 移除列表、焦点通知；两条订阅各自配对，不往背包写状态。
void UCatInventoryQuickbarWidget::UnbindInventoryModel()
{
	UCatInventoryModel* Model = BackPack.IsValid() ? BackPack->GetInventoryModel() : nullptr;
	if (Model && InventoryModelChangedHandle.IsValid())
	{
		Model->OnInventoryListChanged.Remove(InventoryModelChangedHandle);
	}
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer()); Controller && SelectionChangedHandle.IsValid())
	{
		Controller->OnQuickbarSelectionChanged.Remove(SelectionChangedHandle);
	}
	InventoryModelChangedHandle.Reset();
	SelectionChangedHandle.Reset();
}

// 格子解绑流程：快捷栏不绑定点击选择，但仍撤销 Tooltip 来源，再释放本 View 对动态控件的所有权。
void UCatInventoryQuickbarWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->CancelTooltip();
		}
	}
	SlotWidgets.Reset();
}
