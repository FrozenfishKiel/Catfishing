#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatShopKioskAuthoringLibrary.generated.h"

/** 正式商店摊位的编辑器迁移入口；它把旧球体命中代理移出蓝图，并让可见模型成为唯一的准星交互表面。 */
UCLASS()
class CATFISHINGEDITOR_API UCatShopKioskAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 迁移并保存正式 BP_CatShopKiosk；仅在存在实际可见模型时改写组件挂接和射线碰撞，否则记录原因并保持资产不保存。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Shop")
	static bool MigrateFormalShopKioskBlueprint();
};
