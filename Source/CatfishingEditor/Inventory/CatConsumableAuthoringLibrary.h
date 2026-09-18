#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatConsumableAuthoringLibrary.generated.h"

/** 七件道具的最小资产接线；复用正式物品总表与商店表，不生成第二套配置。 */
UCLASS()
class CATFISHINGEDITOR_API UCatConsumableAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 图标导入后创建缺失道具及可编辑 GA/GE/GC 资产，登记总表和四件商品，并迁移真鱼分类标签。既有道具资产不覆盖。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Inventory")
	static bool CreateMissingConsumables();
};
