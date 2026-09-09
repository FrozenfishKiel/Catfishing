#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatFishGuardInventoryWidget.generated.h"

/** 鱼护箱子的单页 UI；它复用库存交互契约，但只显示当前外部容器自己的 Slots。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishGuardInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()

protected:
	/** 鱼护页的格子数据源固定代表当前外部容器；同屏背包或营地页存在时不能共享它们的 Slots。 */
	virtual const TArray<FCatInventorySlotView>& GetInventorySlotsForWidget(
		const FCatInventoryViewState& ViewState) const override;

	/** 把鱼护页渲染后的 Slots 写回 ExternalContainerSlots，让蓝图扩展读取到本页自己的高亮结果。 */
	virtual void StoreDisplayedSlotsInViewState(FCatInventoryViewState& ViewState,
		const TArray<FCatInventorySlotView>& Slots) const override;
};
