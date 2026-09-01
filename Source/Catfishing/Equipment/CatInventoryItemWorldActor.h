#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CatInventoryItemWorldActor.generated.h"

/** 场景中代表库存物品实例的 Actor 合同；世界占用登记只依赖这两个身份，不依赖鱼竿、陷阱或其他具体玩法类。 */
UINTERFACE()
class CATFISHING_API UCatInventoryItemWorldActor : public UInterface
{
	GENERATED_BODY()
};

class CATFISHING_API ICatInventoryItemWorldActor
{
	GENERATED_BODY()

public:
	/** 读取这个场景 Actor 对应的库存实例身份；通用占用登记用它防止同一件物品同时存在于场上和普通库存。 */
	virtual FGuid GetInventoryItemInstanceIdForRegistry() const = 0;

	/** 读取这个场景 Actor 对应的物品定义身份；通用占用登记用它做坏数据拒绝和诊断，不参与具体玩法分支。 */
	virtual FName GetInventoryItemDefinitionIdForRegistry() const = 0;
};
