#include "UI/InventorySlot/CatInventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"
#include "Engine/LocalPlayer.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/ItemTooltip/CatItemTooltipController.h"

// 先撤销旧悬停来源并保存明确的库存和格位，再按条目更新图片与数量；空格或定义尚未到达时清掉旧图。
void UCatInventorySlotWidget::SetSlotContext(const int32 InSlotIndex, UCatInventoryComponent* InInventory,
	const FCatInventoryEntry& InEntry)
{
	CancelTooltip();
	SlotIndex = InSlotIndex;
	SourceInventory = InInventory;
	InventoryEntry = InEntry;
	const UCatInventoryItemDefinition* Definition = InventoryEntry.Instance ? InventoryEntry.Instance->GetItemDefinition() : nullptr;
	UTexture2D* Thumbnail = Definition && InventoryEntry.StackCount > 0 ? Definition->GetInventoryThumbnail().LoadSynchronous() : nullptr;
	if (ThumbnailImage)
	{
		ThumbnailImage->SetBrushFromTexture(Thumbnail, true);
		ThumbnailImage->SetVisibility(Thumbnail ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (QuantityTextBlock)
	{
		QuantityTextBlock->SetText(FText::AsNumber(InventoryEntry.StackCount));
		QuantityTextBlock->SetVisibility(InventoryEntry.StackCount > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// 只暴露本格的显示副本；蓝图可据此渲染状态，但库存事实仍以服务器按 SourceInventory 和 SlotIndex 重读为准。
const FCatInventoryEntry& UCatInventorySlotWidget::GetInventoryEntry() const
{
	return InventoryEntry;
}

// 本地格子只提交库存宿主和位置；不等待上一动作，也不拿 UI 数量作为操作前提，服务器统一复核权限和真实物品。
void UCatInventorySlotWidget::RequestUseItem()
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	if (!Controller || !SourceInventory || SlotIndex == INDEX_NONE || !InventoryEntry.Instance || InventoryEntry.StackCount <= 0)
	{
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_use_submitted World=%s NetMode=%d Request=%s SourceHost=%s SourceIndex=%d"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(Controller->GetNetMode()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(SourceInventory->GetOwner()), SlotIndex);
	Controller->ServerUseInventoryItemFromHost(RequestId, SourceInventory->GetOwner(), SlotIndex);
}

// 允许格子接收鼠标和键盘；其他初始化继续使用 UUserWidget。
void UCatInventorySlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

// owning LocalPlayer 决定提示属于哪个玩家；子系统尚未装配时返回空，格子保持正常点击与拖放。
UCatItemTooltipController* UCatInventorySlotWidget::ResolveTooltipController() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	return UI ? UI->GetItemTooltipController() : nullptr;
}

// 先保留 WBP 的悬停表现，再提交本格中心的屏幕绝对坐标；不从当前鼠标位置构造跟随提示。
void UCatInventorySlotWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (UCatItemTooltipController* Tooltip = ResolveTooltipController())
	{
		Tooltip->ShowTooltip(this, InGeometry.LocalToAbsolute(InGeometry.GetLocalSize() * 0.5f));
	}
}

// 离开先撤销本格来源，再交给父类恢复原 WBP 悬停颜色；来源校验在 Controller 内完成。
void UCatInventorySlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	CancelTooltip();
	Super::NativeOnMouseLeave(InMouseEvent);
}

// 重建或销毁不能依赖 Slate 额外发送 Leave；主动撤销后再释放控件生命周期。
void UCatInventorySlotWidget::NativeDestruct()
{
	CancelTooltip();
	Super::NativeDestruct();
}

// 只把当前格身份交给已存在的控制器；另一格已经接管时 Hide 会忽略这次迟到清理。
void UCatInventorySlotWidget::CancelTooltip()
{
	if (UCatItemTooltipController* Tooltip = ResolveTooltipController()) Tooltip->HideTooltip(this);
}

// 左键只检测拖拽，避免按下时重建控件中断鼠标捕获；右键复用唯一使用入口。
FReply UCatInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		RequestUseItem();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// 普通点击在松开时选择父页使用按钮所指的格位；拖拽由 Slate 的独立 Drop 事件结束。
FReply UCatInventorySlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotSelected.Broadcast(SlotIndex);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

// 先撤销悬停，再只为非空库存格创建载荷；源位置保存到操作对象，预览取当前定义图片，临时控件随拖拽操作释放。
void UCatInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	OutOperation = nullptr;
	CancelTooltip();
	if (!SourceInventory || SlotIndex == INDEX_NONE || !InventoryEntry.Instance || InventoryEntry.StackCount <= 0)
	{
		return;
	}
	UCatInventoryDragDropOperation* Operation = NewObject<UCatInventoryDragDropOperation>(this);
	Operation->SourceInventory = SourceInventory;
	Operation->SourceSlotIndex = SlotIndex;
	Operation->Pivot = EDragPivot::CenterCenter;
	FVector2D PreviewSize = InGeometry.GetLocalSize();
	if (PreviewSize.X <= 1.0f || PreviewSize.Y <= 1.0f)
	{
		PreviewSize = FVector2D(72.0f, 72.0f);
	}
	USizeBox* Preview = NewObject<USizeBox>(Operation);
	UImage* Image = NewObject<UImage>(Preview);
	Preview->SetWidthOverride(PreviewSize.X);
	Preview->SetHeightOverride(PreviewSize.Y);
	if (const UCatInventoryItemDefinition* Definition = InventoryEntry.Instance->GetItemDefinition())
	{
		Image->SetBrushFromTexture(Definition->GetInventoryThumbnail().LoadSynchronous(), false);
	}
	Preview->AddChild(Image);
	Operation->DefaultDragVisual = Preview;
	OutOperation = Operation;
}

// 空格也能接收库存拖拽；这里只决定 Slate 事件归属，库存内容完全不变。
bool UCatInventorySlotWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const UCatInventoryDragDropOperation* Operation = Cast<UCatInventoryDragDropOperation>(InOperation);
	if (Operation && Operation->SourceInventory && Operation->SourceSlotIndex != INDEX_NONE && SourceInventory && SlotIndex != INDEX_NONE)
	{
		return true;
	}
	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

// 和 AOInventoryUI 一样从源/目标库存及格位提交交换；同格无操作，其他请求由 owning Controller 送到服务器。
// 服务器可同步触发本机 Model 更新并重建 UI，因此提交前固定全部参数，提交后不再读取本格。
bool UCatInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const UCatInventoryDragDropOperation* Operation = Cast<UCatInventoryDragDropOperation>(InOperation);
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwningPlayer());
	if (!Operation || !Controller || !Operation->SourceInventory || !SourceInventory
		|| Operation->SourceSlotIndex == INDEX_NONE || SlotIndex == INDEX_NONE)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	if (Operation->SourceInventory == SourceInventory && Operation->SourceSlotIndex == SlotIndex)
	{
		return true;
	}
	const FGuid RequestId = FGuid::NewGuid();
	AActor* SourceHost = Operation->SourceInventory->GetOwner();
	AActor* TargetHost = SourceInventory->GetOwner();
	const int32 SourceIndex = Operation->SourceSlotIndex;
	const int32 TargetIndex = SlotIndex;
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_slot_drop_submitted World=%s NetMode=%d Request=%s SourceHost=%s SourceIndex=%d TargetHost=%s TargetIndex=%d"),
		*GetPathNameSafe(GetWorld()), static_cast<int32>(Controller->GetNetMode()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(SourceHost), SourceIndex, *GetNameSafe(TargetHost), TargetIndex);
	Controller->ServerMoveInventoryItemBetweenHosts(RequestId, SourceHost, SourceIndex, TargetHost, TargetIndex);
	return true;
}
