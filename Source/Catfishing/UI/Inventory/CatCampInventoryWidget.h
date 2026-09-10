#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatCampInventoryWidget.generated.h"

/** 正式营地 WBP 的原生父类；数据源由打开页面时注入，嵌套背包绑定自己的库存，不再按聚合数组分流。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatCampInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()
};