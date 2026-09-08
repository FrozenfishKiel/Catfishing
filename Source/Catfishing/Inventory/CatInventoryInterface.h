#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CatInventoryInterface.generated.h"

class UCatInventoryComponent;

UINTERFACE(MinimalAPI)
class UCatInventoryInterface : public UInterface
{
	GENERATED_BODY()
};

/** 暴露对象当前可被统一收货和格子交互系统识别的库存组件。 */
class CATFISHING_API ICatInventoryInterface
{
	GENERATED_BODY()

public:
	/** 返回实现者承认的库存事实源；统一收货入口只通过这个组件读写格子，不猜测宿主的具体类型。 */
	virtual UCatInventoryComponent* GetInventoryComponent() = 0;
};
