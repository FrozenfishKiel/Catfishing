#include "UI/Inventory/CatInventoryWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 构造流程：对可选按钮执行 Remove/Add 配对；格子由 RenderInventory 按后端 Slots 动态创建，不在构造时硬编码。
void UCatInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
		ConsumeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeClicked);
		SacrificeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleSacrificeClicked);
	}
}

// 销毁流程：
// 1. 先解除动态格子委托，避免旧槽位在本次移出视口后继续回调。
// 2. 再解除本 View 绑定到可选按钮上的 UMG 点击事件。
// 3. 保留 OnCloseRequested 等外部意图订阅；RemoveFromParent 只是一次关闭，PageController::Unbind 才代表外部订阅生命周期结束。
void UCatInventoryWidget::NativeDestruct()
{
	UnbindSlotWidgets();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeClicked);
	}
	Super::NativeDestruct();
}

// 键盘流程：背包打开且按下当前 IMC 解析出来的同一键时请求关闭；其他键保持默认传播。
FReply UCatInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (LastInventoryViewState.bOpen && !LastInventoryViewState.ToggleKeyName.IsNone()
		&& InKeyEvent.GetKey().GetFName() == LastInventoryViewState.ToggleKeyName)
	{
		RequestCloseInventory();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：缓存完整背包投影，更新鱼/装备/耗材/待取装备文本，再按 Slots 数组重建鱼竿槽和容器格子并触发蓝图扩展点。
void UCatInventoryWidget::RenderInventory(const FCatInventoryViewState& ViewState)
{
	LastInventoryViewState = ViewState;
	BlueprintSummaryText = ViewState.SummaryText;
	BlueprintEquipmentText = ViewState.EquipmentText;
	BlueprintConsumablesText = ViewState.ConsumablesText;
	BlueprintTeamEquipmentText = ViewState.TeamEquipmentText;
	BlueprintSelectedFishText = ViewState.SelectedFishText;
	BlueprintResultText = ViewState.ResultText;
	if (SummaryTextBlock)
	{
		SummaryTextBlock->SetText(BlueprintSummaryText);
	}
	if (EquipmentTextBlock)
	{
		EquipmentTextBlock->SetText(BlueprintEquipmentText);
	}
	if (ConsumablesTextBlock)
	{
		ConsumablesTextBlock->SetText(BlueprintConsumablesText);
	}
	if (TeamEquipmentTextBlock)
	{
		TeamEquipmentTextBlock->SetText(BlueprintTeamEquipmentText);
	}
	if (SelectedFishTextBlock)
	{
		SelectedFishTextBlock->SetText(BlueprintSelectedFishText);
	}
	if (ResultTextBlock)
	{
		ResultTextBlock->SetText(BlueprintResultText);
	}
	const bool bActionEnabled = ViewState.bCanSubmitAction;
	if (ConsumeFishButton)
	{
		ConsumeFishButton->SetIsEnabled(bActionEnabled);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->SetIsEnabled(bActionEnabled);
	}
	RebuildSlotWidgets();
	BP_RenderInventory(LastInventoryViewState);
}

// 格子类设置流程：保存拥有者解析出的正式 Slot WBP 类；若已有投影，立即重建 WrapBox，让热切换配置时界面同步。
void UCatInventoryWidget::SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass)
{
	InventorySlotWidgetClass = InSlotWidgetClass;
	RebuildSlotWidgets();
}

// 关闭请求流程：只广播 UI 意图；输入模式、鼠标和可见性由 PageController 成对处理。
void UCatInventoryWidget::RequestCloseInventory()
{
	OnCloseRequested.Broadcast();
}

// 选择请求流程：过滤负下标后广播；Model 会基于最新背包快照继续裁剪空格和越界。
void UCatInventoryWidget::RequestSelectSlot(const int32 SlotIndex)
{
	if (SlotIndex >= 0)
	{
		OnSlotSelectionRequested.Broadcast(SlotIndex);
	}
}

// 吃鱼请求流程：只广播动作枚举；鱼实例、容器 ID 和 Revision 都由 PageController 从 Model 重读。
void UCatInventoryWidget::RequestConsumeSelectedFish()
{
	OnInventoryActionRequested.Broadcast(ECatInventoryAction::ConsumeSelectedFish);
}

// 献祭请求流程：只广播动作枚举；献祭命令不在蓝图里组装。
void UCatInventoryWidget::RequestSacrificeSelectedFish()
{
	OnInventoryActionRequested.Broadcast(ECatInventoryAction::SacrificeSelectedFish);
}

// 状态读取流程：返回最近一次 Model 投影；蓝图可以只读展示，不获得任何订阅或写口。
const FCatInventoryViewState& UCatInventoryWidget::GetLastInventoryViewState() const
{
	return LastInventoryViewState;
}

// WrapBox 重建流程：
// 1. 先解绑旧格子并清空 WrapBox，避免刷新后旧下标继续广播。
// 2. 再为 Model 提供的每个 Slot 创建独立 UCatInventorySlotWidget。
// 3. 每个格子绑定选择、鼠标上下文和 Drop 委托，动作按钮仍留在主界面。
void UCatInventoryWidget::RebuildSlotWidgets()
{
	UnbindSlotWidgets();
	if (!InventorySlotWrapBox || !InventorySlotWidgetClass)
	{
		return;
	}
	InventorySlotWrapBox->ClearChildren();
	for (const FCatInventorySlotView& SlotView : LastInventoryViewState.Slots)
	{
		UCatInventorySlotWidget* SlotWidget = CreateWidget<UCatInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->OnSlotSelected.AddUObject(this, &ThisClass::HandleSlotSelected);
		SlotWidget->OnPointerActionRequested.AddUObject(this, &ThisClass::HandleSlotPointerAction);
		SlotWidget->OnSlotDropRequested.AddUObject(this, &ThisClass::HandleSlotDropRequested);
		SlotWidget->RenderSlot(SlotView);
		InventorySlotWrapBox->AddChildToWrapBox(SlotWidget);
		BoundSlotWidgets.Add(SlotWidget);
	}
}

// 格子解绑流程：逐个移除本对象绑定的原生委托，再清本地数组；WrapBox 子对象是否销毁交给 UMG 父子关系处理。
void UCatInventoryWidget::UnbindSlotWidgets()
{
	for (UCatInventorySlotWidget* SlotWidget : BoundSlotWidgets)
	{
		if (!SlotWidget)
		{
			continue;
		}
		SlotWidget->OnSlotSelected.RemoveAll(this);
		SlotWidget->OnPointerActionRequested.RemoveAll(this);
		SlotWidget->OnSlotDropRequested.RemoveAll(this);
	}
	BoundSlotWidgets.Reset();
}

// 格子选择流程：动态 Slot Widget 的下标事件统一转成主界面选择意图。
void UCatInventoryWidget::HandleSlotSelected(const int32 SlotIndex)
{
	RequestSelectSlot(SlotIndex);
}

// 格子上下文流程：右键或拖拽默认也先选择该格，再把原始鼠标意图继续广播给 PageController 或蓝图表现。
void UCatInventoryWidget::HandleSlotPointerAction(const int32 SlotIndex,
	const ECatInventorySlotPointerAction PointerAction)
{
	RequestSelectSlot(SlotIndex);
	OnSlotPointerRequested.Broadcast(SlotIndex, PointerAction);
}

// 格子 Drop 流程：Drop 会先选择目标格，随后把源/目标快照交给 PageController 做方向、Revision 和服务器入口裁决。
void UCatInventoryWidget::HandleSlotDropRequested(const FCatInventorySlotView& SourceSlot,
	const FCatInventorySlotView& TargetSlot)
{
	RequestSelectSlot(TargetSlot.SlotIndex);
	OnSlotDropRequested.Broadcast(SourceSlot, TargetSlot);
}

// 关闭按钮流程：收口到统一关闭请求，避免 WBP 图表和按钮绑定产生两套出口。
void UCatInventoryWidget::HandleCloseClicked()
{
	RequestCloseInventory();
}

// 吃鱼按钮流程：收口到统一吃鱼请求。
void UCatInventoryWidget::HandleConsumeClicked()
{
	RequestConsumeSelectedFish();
}

// 献祭按钮流程：收口到统一献祭请求。
void UCatInventoryWidget::HandleSacrificeClicked()
{
	RequestSacrificeSelectedFish();
}
