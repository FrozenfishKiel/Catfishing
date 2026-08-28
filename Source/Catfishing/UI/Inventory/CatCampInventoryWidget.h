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
	/** 返回营地公共仓库数据源的 Slots；本页构建和刷新时只用这份数组创建格子。 */
	virtual const TArray<FCatInventorySlotView>& GetInventorySlotsForWidget(
		const FCatInventoryViewState& ViewState) const override;
};
