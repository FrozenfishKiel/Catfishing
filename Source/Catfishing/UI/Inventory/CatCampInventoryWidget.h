#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatCampInventoryWidget.generated.h"

/** 营地公共仓库的独立页面基类；它只显示营地公共仓库自己的 Slots，不拼玩家随身背包。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatCampInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()

protected:
	/** 营地页的格子数据源固定代表公共仓库；同屏背包存在时也不能回退读取随身库存数组。 */
	virtual const TArray<FCatInventorySlotView>& GetInventorySlotsForWidget(
		const FCatInventoryViewState& ViewState) const override;

	/** 把营地页渲染后的 Slots 写回 CampInventorySlots，让蓝图扩展读取到本页自己的高亮结果。 */
	virtual void StoreDisplayedSlotsInViewState(FCatInventoryViewState& ViewState,
		const TArray<FCatInventorySlotView>& Slots) const override;
};
