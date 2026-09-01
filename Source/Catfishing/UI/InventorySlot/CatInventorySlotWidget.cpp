#include "UI/InventorySlot/CatInventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace
{
	// 库存格来源判断流程：把随身背包和营地公共仓库都视为同一类可整理的运行期库存格；具体写入哪个宿主由 PageController 再决定。
	bool IsRunInventorySlot(const FCatInventorySlotView& Slot)
	{
		if (Slot.SlotSource == ECatInventorySlotSource::InventoryObject)
		{
			return Slot.InventorySlotIndex != INDEX_NONE;
		}
		if (Slot.SlotSource == ECatInventorySlotSource::CampInventoryObject)
		{
			return Slot.CampInventorySlotIndex != INDEX_NONE;
		}
		return false;
	}

	// 库存 Drop 判断流程：格子 Widget 只确认源和目标都是运行期库存格；同源整理或跨源转移由 PageController 转成明确服务器命令。
	bool IsRunInventoryDropPair(const FCatInventorySlotView& SourceSlot, const FCatInventorySlotView& TargetSlot)
	{
		return SourceSlot.bOccupied
			&& IsRunInventorySlot(SourceSlot)
			&& IsRunInventorySlot(TargetSlot);
	}

	// 容器 Drop 判断流程：Items 容器仍只允许占用源格拖到容器目标格；目标空格也要能接住拖拽。
	bool IsContainerDropPair(const FCatInventorySlotView& SourceSlot, const FCatInventorySlotView& TargetSlot)
	{
		return TargetSlot.SlotSource == ECatInventorySlotSource::ContainerObject
			&& SourceSlot.SlotSource == ECatInventorySlotSource::ContainerObject
			&& SourceSlot.bOccupied
			&& SourceSlot.ObjectKind != ECatContainedObjectKind::Unknown
			&& SourceSlot.ObjectInstanceId.IsValid();
	}

	// 格子接收判断流程：DragOver 和 Drop 共用同一套判定，避免悬停时漏事件、落下时又试图补救。
	bool IsAcceptedDropPair(const FCatInventorySlotView& SourceSlot, const FCatInventorySlotView& TargetSlot)
	{
		return IsRunInventoryDropPair(SourceSlot, TargetSlot)
			|| IsContainerDropPair(SourceSlot, TargetSlot);
	}
}

// 渲染流程：
// 1. 缓存主界面的只读投影，再同步文本、数量、缩略图引用和选中状态给蓝图字段。
// 2. 可选图片控件会同步加载当前缩略图并写入 Brush；缺少资源时折叠图片控件，避免沿用上一次的显示内容。
// 3. 可选数量控件按 bBlueprintShowQuantity 自动显隐，最后触发蓝图扩展点；格子不持有容器数组或任何写入口。
void UCatInventorySlotWidget::RenderSlot(const FCatInventorySlotView& SlotView)
{
	LastSlotView = SlotView;
	BlueprintDisplayText = SlotView.DisplayText;
	BlueprintDisplayName = SlotView.DisplayName;
	BlueprintDescription = SlotView.Description;
	BlueprintThumbnail = SlotView.Thumbnail;
	BlueprintQuantity = SlotView.Quantity;
	BlueprintQuantityText = SlotView.QuantityText;
	bBlueprintShowQuantity = SlotView.bShowQuantity;
	bBlueprintStackable = SlotView.bStackable;
	bBlueprintOccupied = SlotView.bOccupied;
	bBlueprintSelected = SlotView.bSelected;
	if (DisplayTextBlock)
	{
		DisplayTextBlock->SetText(BlueprintDisplayText);
	}
	if (ThumbnailImage)
	{
		if (UTexture2D* LoadedThumbnail = BlueprintThumbnail.LoadSynchronous())
		{
			ThumbnailImage->SetBrushFromTexture(LoadedThumbnail, true);
			ThumbnailImage->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			ThumbnailImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (QuantityTextBlock)
	{
		QuantityTextBlock->SetText(BlueprintQuantityText);
		QuantityTextBlock->SetVisibility(bBlueprintShowQuantity
			                                 ? ESlateVisibility::Visible
			                                 : ESlateVisibility::Collapsed);
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
// 1. 左键只交给 Slate 检测拖拽，不在拖拽阈值确认前广播选择或刷新本页。
// 2. 右键只广播上下文意图，主界面按格子来源直接构造动作。
// 3. 其他按键不消费，避免格子截断父级面板快捷键。
FReply UCatInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnContextRequested.Broadcast(LastSlotView);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// 鼠标松开流程：只有未被拖拽消费的普通左键点击会走到这里；这时再更新本页本地选中，不会影响 Drop 命中。
FReply UCatInventorySlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	(void)InGeometry;
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotSelected.Broadcast(LastSlotView);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

// 拖拽流程：
// 1. 运行期库存和 Items 容器的占用格都能启动拖拽；空格可作为目标，但不能作为源。
// 2. 拖拽会冻结一份源格只读投影，后续 Drop 使用这份数据而不是源 Widget 指针。
// 3. 不可拖、源事实不完整或 Operation 创建失败时直接放弃，不广播拖拽开始。
// 4. Operation 先读取当前格子的实际几何尺寸，几何异常时回退到 72x72，再包住临时图片控件；图片只来自 DragSource.Thumbnail，不读文字，也不回退上一次控件 Brush。
// 5. 创建完成后不再额外通知上层刷新；真正列表变化只来自 Drop 后的后端结果。
void UCatInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
                                                   UDragDropOperation*& OutOperation)
{
	(void)InMouseEvent;
	OutOperation = nullptr;
	const bool bRunInventoryDrag = LastSlotView.bOccupied && IsRunInventorySlot(LastSlotView);
	const bool bContainerDrag = LastSlotView.SlotSource == ECatInventorySlotSource::ContainerObject
		&& LastSlotView.bOccupied && LastSlotView.ObjectKind != ECatContainedObjectKind::Unknown
		&& LastSlotView.ObjectInstanceId.IsValid();
	if (!LastSlotView.bCanDrag || (!bRunInventoryDrag && !bContainerDrag))
	{
		return;
	}
	const FCatInventorySlotView DragSource = LastSlotView;
	UCatInventoryDragDropOperation* DragOperation = NewObject<UCatInventoryDragDropOperation>(this);
	if (!DragOperation)
	{
		return;
	}
	OutOperation = DragOperation;
	DragOperation->SourceSlot = DragSource;
	DragOperation->Pivot = EDragPivot::CenterCenter;
	FVector2D ResolvedDragPreviewSize = InGeometry.GetLocalSize();
	if (ResolvedDragPreviewSize.X <= 1.0f || ResolvedDragPreviewSize.Y <= 1.0f)
	{
		ResolvedDragPreviewSize = FVector2D(72.0f, 72.0f);
	}
	USizeBox* DragVisualRoot = NewObject<USizeBox>(DragOperation);
	UImage* DragImage = DragVisualRoot ? NewObject<UImage>(DragVisualRoot) : nullptr;
	if (DragVisualRoot && DragImage)
	{
		DragVisualRoot->SetWidthOverride(ResolvedDragPreviewSize.X);
		DragVisualRoot->SetHeightOverride(ResolvedDragPreviewSize.Y);
		if (UTexture2D* LoadedThumbnail = DragSource.Thumbnail.LoadSynchronous())
		{
			DragImage->SetBrushFromTexture(LoadedThumbnail, false);
			DragImage->SetDesiredSizeOverride(ResolvedDragPreviewSize);
		}
		DragVisualRoot->AddChild(DragImage);
		OutOperation->DefaultDragVisual = DragVisualRoot;
	}
}

// 拖拽悬停流程：
// 1. 只识别本类创建的轻量载荷；其他 UMG 拖拽继续交回父类。
// 2. 当前格如果能作为目标，就在悬停阶段直接接住事件，避免空营地格被同屏背包页或父级面板抢走 Drop。
// 3. 本函数不改选择、不刷新列表、不提交服务器命令，只稳定 Slate 的目标命中。
bool UCatInventorySlotWidget::NativeOnDragOver(const FGeometry& InGeometry,
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UCatInventoryDragDropOperation* DragOperation = Cast<UCatInventoryDragDropOperation>(InOperation);
	if (DragOperation && IsAcceptedDropPair(DragOperation->SourceSlot, LastSlotView))
	{
		return true;
	}
	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

// Drop 流程：
// 1. 只接受本类创建的 UCatInventoryDragDropOperation，其他 UMG 拖拽交回父类。
// 2. 运行期库存格允许同源整理和跨源转移；Items 容器槽仍只接受容器到容器的移动。
// 3. 本格只广播源和目标快照，不做本地数组搬运；服务器复制回来后 Model 会重建最终显示。
bool UCatInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                           UDragDropOperation* InOperation)
{
	(void)InGeometry;
	(void)InDragDropEvent;
	const UCatInventoryDragDropOperation* DragOperation = Cast<UCatInventoryDragDropOperation>(InOperation);
	if (!DragOperation || !IsAcceptedDropPair(DragOperation->SourceSlot, LastSlotView))
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	OnSlotDropRequested.Broadcast(DragOperation->SourceSlot, LastSlotView);
	return true;
}
