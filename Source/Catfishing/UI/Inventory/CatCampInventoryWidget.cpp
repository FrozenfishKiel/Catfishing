#include "UI/Inventory/CatCampInventoryWidget.h"

// 营地数据源选择流程：营地 WBP 每次构建或收到 Model 广播时都读取公共仓库自己的 Slots，避免显示随身背包格。
const TArray<FCatInventorySlotView>& UCatCampInventoryWidget::GetInventorySlotsForWidget(
	const FCatInventoryViewState& ViewState) const
{
	return ViewState.CampInventorySlots;
}
