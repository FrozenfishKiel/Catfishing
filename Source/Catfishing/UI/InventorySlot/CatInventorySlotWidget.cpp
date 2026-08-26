#include "UI/InventorySlot/CatInventorySlotWidget.h"

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

// 状态读取流程：返回最近投影的只读引用；调用者只能展示或比较下标，不能通过它修改后端容器。
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

// 拖拽流程：
// 1. 只有 Items 容器物体允许拖拽；当前鱼竿槽能选中查看，但不能被当成后端容器源。
// 2. 容器物体拖拽冻结一份源格只读投影，避免刷新 WrapBox 后继续依赖旧 Widget 指针。
// 3. Operation 使用独立临时文字控件作为拖拽视觉；真实移动必须等目标格 NativeOnDrop 广播给 PageController 后提交服务器。
void UCatInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	(void)InGeometry;
	(void)InMouseEvent;
	OutOperation = nullptr;
	if (!LastSlotView.bCanDrag || LastSlotView.SlotSource != ECatInventorySlotSource::ContainerObject
		|| !LastSlotView.bOccupied || LastSlotView.ObjectKind == ECatContainedObjectKind::Unknown
		|| !LastSlotView.ObjectInstanceId.IsValid())
	{
		return;
	}
	OnPointerActionRequested.Broadcast(LastSlotView.SlotIndex, ECatInventorySlotPointerAction::DragStarted);
	UCatInventoryDragDropOperation* DragOperation = NewObject<UCatInventoryDragDropOperation>(this);
	OutOperation = DragOperation;
	if (OutOperation)
	{
		DragOperation->SourceSlot = LastSlotView;
		UTextBlock* DragVisual = NewObject<UTextBlock>(this);
		if (DragVisual)
		{
			DragVisual->SetText(LastSlotView.DisplayText);
			OutOperation->DefaultDragVisual = DragVisual;
		}
	}
}

// Drop 流程：
// 1. 只接受本类创建的 UCatInventoryDragDropOperation，其他 UMG 拖拽交回父类。
// 2. 源和目标都必须是 Items 容器槽；当前鱼竿槽不接收 Drop，避免把 Equipment 当容器 index 提交。
// 3. 本格只广播源和目标快照，不做本地数组搬运；服务器复制回来后 Model 会重建最终显示。
bool UCatInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	(void)InGeometry;
	(void)InDragDropEvent;
	const UCatInventoryDragDropOperation* DragOperation = Cast<UCatInventoryDragDropOperation>(InOperation);
	if (!DragOperation || LastSlotView.SlotSource != ECatInventorySlotSource::ContainerObject
		|| DragOperation->SourceSlot.SlotSource != ECatInventorySlotSource::ContainerObject
		|| !DragOperation->SourceSlot.bOccupied
		|| DragOperation->SourceSlot.ObjectKind == ECatContainedObjectKind::Unknown
		|| !DragOperation->SourceSlot.ObjectInstanceId.IsValid())
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	OnSlotDropRequested.Broadcast(DragOperation->SourceSlot, LastSlotView);
	return true;
}
