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
	/** 返回当前外部容器数据源的 Slots；鱼护页构建和刷新时只用这份数组创建格子。 */
	virtual const TArray<FCatInventorySlotView>& GetInventorySlotsForWidget(
		const FCatInventoryViewState& ViewState) const override;
};
