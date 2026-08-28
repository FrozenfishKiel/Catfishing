#include "UI/Inventory/CatFishGuardInventoryWidget.h"

// 鱼护数据源选择流程：鱼护 WBP 每次构建或收到 Model 广播时都读取外部容器自己的 Slots，避免显示随身背包或营地仓库格。
const TArray<FCatInventorySlotView>& UCatFishGuardInventoryWidget::GetInventorySlotsForWidget(
	const FCatInventoryViewState& ViewState) const
{
	return ViewState.ExternalContainerSlots;
}
