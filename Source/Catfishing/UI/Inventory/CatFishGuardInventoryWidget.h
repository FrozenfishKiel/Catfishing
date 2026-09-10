#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatFishGuardInventoryWidget.generated.h"

/** 正式鱼护 WBP 的原生父类；打开时绑定鱼护库存，格子显示和交互复用通用库存面板。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishGuardInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()
};