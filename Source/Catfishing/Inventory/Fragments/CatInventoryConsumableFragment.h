#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatInventoryConsumableFragment.generated.h"

/** 纯库存层面的消耗品语义；它只声明右键使用会扣多少库存，不在库存核心里直接触发 GAS 或 Condition。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatInventoryConsumableFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 消耗品开关只决定能否进入库存扣量；避免库存层顺手执行治疗、窝料或 GAS 效果。 */
	bool HasUsableInventoryUse() const;

	/** 读取一次库存 Use 要消耗的数量；非法配置被压到 1，避免调用方重复兜底。 */
	int32 GetConsumeCount() const;

public:
	/** 是否允许这类物品从背包格直接发起 Use；具体效果仍由草药、窝料或其他玩法系统在库存提交后处理。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	bool bAllowUseFromInventory = true;

	/** 一次 Use 从同一格扣除的数量；普通草药、窝料、鱼饵通常是 1，批量道具可以显式提高。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable", meta = (ClampMin = "1"))
	int32 ConsumeCount = 1;
};
