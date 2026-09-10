#include "UI/Inventory/CatInventoryWidget.h"

#include "Character/CatCharacter.h"
#include "Components/Button.h"
#include "Components/WrapBox.h"
#include "Engine/LocalPlayer.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 先移除原库存 Model 监听，再保存本页显示库存、绑定新通知并读取当前列表；不改变其他库存 WBP 的上下文。
void UCatInventoryWidget::SetInventoryContext(UCatInventoryComponent* InInventory)
{
	UnbindInventoryModel();
	DisplayInventory = InInventory;
	if (InInventory)
	{
		InventoryModelChangedHandle = InInventory->GetInventoryModel()->OnInventoryListChanged.AddUObject(
			this, &ThisClass::RefreshInventorySlots);
	}
	RefreshInventorySlots();
}

// 本页只暴露构建或打开时确定的库存组件；没有上下文就返回空，避免 UI 根据营地、鱼护等页面种类重新选择数据源。
UCatInventoryComponent* UCatInventoryWidget::GetInventoryContext() const
{
	return DisplayInventory.Get();
}

// 仅保存本页格子类；构建或库存数据通知到达时由同一刷新入口使用。
void UCatInventoryWidget::SetInventorySlotWidgetClass(const TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass)
{
	InventorySlotWidgetClass = InSlotWidgetClass;
}

// 绑定本页按钮与数据源；外部页面已有明确上下文时保留它，未注入上下文的普通/嵌套背包只解析 owning Pawn。
void UCatInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (!InventorySlotWidgetClass)
	{
		InventorySlotWidgetClass = GetDefault<UCatUISettings>()->LoadInventorySlotWidgetClass();
	}
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory)
	{
		if (const ACatCharacter* Character = Cast<ACatCharacter>(GetOwningPlayerPawn()))
		{
			Inventory = Character->GetInventoryComponent();
		}
	}
	SetInventoryContext(Inventory);
}

// 移除本页所有监听，再交给 UMG 结束视口生命周期；DisplayInventory 保留，重复打开时会重新绑定同一数据源。
void UCatInventoryWidget::NativeDestruct()
{
	UnbindInventoryModel();
	UnbindSlotWidgets();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	Super::NativeDestruct();
}

// 按 AOBackPackUI 的列表刷新方式清空本页格子，再逐条设置库存、下标和条目；子库存面板有自己的 Model 和 WrapBox。
void UCatInventoryWidget::RefreshInventorySlots()
{
	UnbindSlotWidgets();
	SelectedSlotIndex = INDEX_NONE;
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(false);
	}
	if (!InventorySlotWrapBox)
	{
		return;
	}
	InventorySlotWrapBox->ClearChildren();
	UCatInventoryComponent* Inventory = DisplayInventory.Get();
	if (!Inventory || !InventorySlotWidgetClass)
	{
		return;
	}
	const TArray<FCatInventoryEntry>& Entries = Inventory->GetInventoryModel()->GetInventoryList();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->SetSlotContext(Index, Inventory, Entries[Index]);
		SlotWidget->OnSlotSelected.AddUObject(this, &ThisClass::RequestSelectSlot);
		InventorySlotWrapBox->AddChildToWrapBox(SlotWidget);
		SlotWidgets.Add(SlotWidget);
	}
}

// 句柄始终从原显示库存的 Model 移除；库存已销毁时只清句柄，不创建替代数据源。
void UCatInventoryWidget::UnbindInventoryModel()
{
	if (InventoryModelChangedHandle.IsValid())
	{
		if (UCatInventoryComponent* Inventory = DisplayInventory.Get())
		{
			Inventory->GetInventoryModel()->OnInventoryListChanged.Remove(InventoryModelChangedHandle);
		}
		InventoryModelChangedHandle.Reset();
	}
}

// 逐个撤销本页格子的悬停来源和选择监听，再释放引用；先撤销提示才能保证重建期间不显示旧实例。
void UCatInventoryWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->CancelTooltip();
			SlotWidget->OnSlotSelected.RemoveAll(this);
		}
	}
	SlotWidgets.Reset();
}

// 点击只保存本页使用按钮所指的格位，并按是否有物品更新按钮；不修改 Model 或重建格子，因此不会中断后续拖放。
void UCatInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	if (!SlotWidgets.IsValidIndex(SlotIndex))
	{
		return;
	}
	SelectedSlotIndex = SlotIndex;
	UCatInventorySlotWidget* SlotWidget = SlotWidgets[SelectedSlotIndex];
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(SlotWidget->GetInventoryEntry().Instance != nullptr && SlotWidget->GetInventoryEntry().StackCount > 0);
	}
}

// 使用按钮与右键进入同一个格子方法；本页不生成另一份请求或等待状态。
void UCatInventoryWidget::RequestUseSelectedItem()
{
	if (SlotWidgets.IsValidIndex(SelectedSlotIndex))
	{
		SlotWidgets[SelectedSlotIndex]->RequestUseItem();
	}
}

// 窗口与输入生命周期归 PageController；本页只提交关闭意图。
void UCatInventoryWidget::RequestCloseInventory()
{
	if (UCatInventoryPageController* Controller = ResolveInventoryPageController())
	{
		Controller->RequestCloseInventoryFromWidget();
	}
}

// 只为页面关闭解析 owning LocalPlayer 的控制器；Model 数据源由 DisplayInventory 独立确定。
UCatInventoryPageController* UCatInventoryWidget::ResolveInventoryPageController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetInventoryPageController() : nullptr;
}

// 关闭条件读取页面控制器的唯一打开状态；外部库存额外接受交互键，普通背包保持背包键与 Escape。
bool UCatInventoryWidget::ShouldCloseInventoryFromKey(const FKeyEvent& InKeyEvent) const
{
	const UCatInventoryPageController* Controller = ResolveInventoryPageController();
	if (!Controller || !Controller->IsInventoryOpen())
	{
		return false;
	}
	const FName Key = InKeyEvent.GetKey().GetFName();
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (Key == EKeys::Escape.GetFName() || Key == Settings->ResolveInventoryToggleKeyName())
	{
		return true;
	}
	const UCatInventoryComponent* Inventory = DisplayInventory.Get();
	return Inventory && Inventory->GetOwner() != GetOwningPlayerPawn() && Key == Settings->ResolveInteractionConfirmKeyName();
}

// 在子控件消费之前处理关闭键，避免焦点落在格子上后无法关闭整个库存窗口。
FReply UCatInventoryWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 根控件直接收到按键时复用同一关闭判断；其余输入保持默认传播。
FReply UCatInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseInventoryFromKey(InKeyEvent))
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 关闭按钮使用公开关闭入口，和键盘关闭共享输入恢复时序。
void UCatInventoryWidget::HandleCloseClicked()
{
	RequestCloseInventory();
}

// 使用按钮直接使用当前选中格，和该格右键共享服务器请求。
void UCatInventoryWidget::HandleConsumeClicked()
{
	RequestUseSelectedItem();
}
