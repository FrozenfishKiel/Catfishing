#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatFishUseAuthoringLibrary.generated.h"

/** 已有鱼定义的食用效果接线；只迁移现有资产，不创建新鱼或改变数值。 */
UCLASS()
class CATFISHINGEDITOR_API UCatFishUseAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 为已有可食用鱼补唯一即时经验片段并保存；既有有效配置保留，冲突配置明确失败。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Inventory")
	static bool MigrateExistingFishUseEffects();
};
