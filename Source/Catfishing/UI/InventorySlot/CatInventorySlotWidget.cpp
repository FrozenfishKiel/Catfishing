#include "UI/InventorySlot/CatInventorySlotWidget.h"

#include "Blueprint/DragDropOperation.h"
#include "Components/TextBlock.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

// 渲染流程：缓存主界面的只读投影，再同步给 Designer 绑定字段，最后触发蓝图扩展点；格子不持有容器数组或任何写入口。
void UCatInventorySlotWidget::RenderSlot(const FCatInventorySlotView& SlotView)
{
	LastSlotView = SlotView;
	BlueprintDisplayText = SlotView.DisplayText;
	bBlueprintOccupied = SlotView.bOccupied;
	bBlueprintSelected = SlotView.bSelected;
	if (DisplayTextBlock)
	{
		DisplayTextBlock->SetText(BlueprintDisplayText);
	}
	BP_RenderSlot(LastSlotView);
}

// 状态读取流程：返回最近投影的只读引用；调用者只能展示或比较下标，不能通过它修改鱼护。
const FCatInventorySlotView& UCatInventorySlotWidget::GetLastSlotView() const
{
	return LastSlotView;
}

// 初始化流程：允许 UUserWidget 接收鼠标交互；正式视觉结构仍完全由 WBP Designer 提供。
void UCatInventorySlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

// 鼠标按下流程：
// 1. 左键先广播选择，再让 Slate 进入拖拽检测窗口。
// 2. 右键广播上下文意图，主界面可以选择该格或打开动作区。
// 3. 其他按键不消费，避免格子截断父级面板快捷键。
FReply UCatInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotSelected.Broadcast(LastSlotView.SlotIndex);
		OnPointerActionRequested.Broadcast(LastSlotView.SlotIndex, ECatInventorySlotPointerAction::Select);
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnPointerActionRequested.Broadcast(LastSlotView.SlotIndex, ECatInventorySlotPointerAction::Context);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// 拖拽流程：创建轻量 Operation 只为让 UMG 拖拽链路成立；拖拽结果不直接改容器，主界面或蓝图后续仍必须走服务器命令。
void UCatInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	(void)InGeometry;
	(void)InMouseEvent;
	OnPointerActionRequested.Broadcast(LastSlotView.SlotIndex, ECatInventorySlotPointerAction::DragStarted);
	OutOperation = NewObject<UDragDropOperation>(this);
	if (OutOperation)
	{
		OutOperation->Payload = this;
		OutOperation->DefaultDragVisual = this;
	}
}
