#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CatInventoryWorldItem.generated.h"

class UCatInventoryItemInstance;

/** 库存物品生成世界 Actor 时的接收契约；普通拾取物、鱼和鱼护沿用各自 Actor，不改变蓝图继承关系。 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCatInventoryWorldItem : public UInterface
{
	GENERATED_BODY()
};

/** 生成端只交付实例与数量；接收 Actor 自己恢复对应表现，成功前不会扣除来源库存。 */
class CATFISHING_API ICatInventoryWorldItem
{
	GENERATED_BODY()

public:
	/** 在延迟生成期间接收落地载荷；实例已归本 Actor 所有，失败时生成端销毁 Actor 并保留原库存。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) = 0;
};
