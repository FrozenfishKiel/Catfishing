#include "UI/Inventory/CatFishGuardInventoryWidget.h"

// 渲染流程：
// 1. 先从完整库存投影中裁出本次交互鱼护容器格，避免 C++ 承担页面组合或额外面板逻辑。
// 2. 再调用普通库存页渲染流程，让同一个 WBP 继续使用关闭、格子、吃鱼和献祭按钮绑定。
// 3. SlotIndex、ContainerId 和 Revision 保留 Model 原始值，拖拽与动作提交仍回到同一条服务器链路。
// 4. ViewState 的 SelectedSlotIndex 改成过滤后 Slots 数组下标，保证蓝图读当前选中格时不会越界。
void UCatFishGuardInventoryWidget::RenderInventory(const FCatInventoryViewState& ViewState)
{
	Super::RenderInventory(MakeFishGuardViewState(ViewState));
}

// 鱼护投影构造流程：
// 1. 复制 Model 的只读状态，只留下本次外部容器里属于地面鱼护的容器和格子。
// 2. 鱼护格自己的 SlotIndex 仍保留 Model 原始下标，点击和拖拽事件要靠它回到同一份后端状态。
// 3. 如果当前选择属于鱼护格，把 SelectedSlotIndex 改成过滤后数组下标；如果不属于，清空选择和动作提交位。
// 4. 把摘要、钓具摘要和空选择提示改成鱼护箱子自己的口径，防止 WBP 绑定父类文本时又显示玩家信息。
// 5. 保留打开态、结果文本和 ToggleKey，让关闭、反馈和服务器复核继续沿用普通库存链路。
FCatInventoryViewState UCatFishGuardInventoryWidget::MakeFishGuardViewState(
	const FCatInventoryViewState& SourceState)
{
	FCatInventoryViewState FishGuardState = SourceState;
	FishGuardState.Containers.Reset();
	FishGuardState.Slots.Reset();

	for (const FCatInventoryContainerView& Container : SourceState.Containers)
	{
		if (Container.Snapshot.Kind == ECatContainerKind::FishGuard)
		{
			FishGuardState.Containers.Add(Container);
		}
	}
	FishGuardState.bHasExternalContainers = !FishGuardState.Containers.IsEmpty();

	bool bSelectionBelongsToFishGuard = false;
	int32 OccupiedFishGuardSlots = 0;
	for (const FCatInventorySlotView& Slot : SourceState.Slots)
	{
		if (Slot.SlotSource == ECatInventorySlotSource::ContainerObject
			&& Slot.ContainerKind == ECatContainerKind::FishGuard)
		{
			const int32 FilteredSlotIndex = FishGuardState.Slots.Num();
			if (Slot.SlotIndex == SourceState.SelectedSlotIndex)
			{
				bSelectionBelongsToFishGuard = true;
				FishGuardState.SelectedSlotIndex = FilteredSlotIndex;
			}
			if (Slot.bOccupied)
			{
				++OccupiedFishGuardSlots;
			}
			FishGuardState.Slots.Add(Slot);
		}
	}
	FishGuardState.SlotCount = FishGuardState.Slots.Num();
	FishGuardState.SummaryText = FText::FromString(FString::Printf(TEXT("鱼护箱子：%d/%d"),
		OccupiedFishGuardSlots,
		FishGuardState.SlotCount));
	FishGuardState.EquipmentText = FText::GetEmpty();
	FishGuardState.InventoryItemsText = FText::GetEmpty();

	if (!bSelectionBelongsToFishGuard)
	{
		FishGuardState.SelectedSlotIndex = INDEX_NONE;
		FishGuardState.SelectedObject = FCatContainedObjectInstance();
		FishGuardState.bHasSelectedObject = false;
		FishGuardState.bSelectedObjectInFishGuard = false;
		FishGuardState.SelectedFish = FCatFishInstance();
		FishGuardState.bHasSelectedFish = false;
		FishGuardState.bSelectedFishInFishGuard = false;
		FishGuardState.bCanSubmitAction = false;
		FishGuardState.SelectedFishText = FText::FromString(
			TEXT("鱼护操作：点击格子可查看，拖拽鱼护格可整理箱子内容。"));
	}

	return FishGuardState;
}
