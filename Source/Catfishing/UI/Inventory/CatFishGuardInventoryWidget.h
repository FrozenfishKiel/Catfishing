#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatFishGuardInventoryWidget.generated.h"

/** 鱼护箱子的单页 UI；它复用库存交互契约，但只展示本次打开的鱼护容器格，不在 C++ 里拼玩家背包。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishGuardInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()

public:
	/** 接收库存 Model 的完整投影，只保留鱼护容器格后交给普通库存渲染流程。 */
	virtual void RenderInventory(const FCatInventoryViewState& ViewState) override;

private:
	/** 构造鱼护页自己的只读投影；格子身份保留 Model 原始下标，选中下标改成过滤后数组位置给蓝图读取。 */
	static FCatInventoryViewState MakeFishGuardViewState(const FCatInventoryViewState& SourceState);
};
