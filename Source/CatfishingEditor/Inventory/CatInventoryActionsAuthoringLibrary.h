#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryActionsAuthoringLibrary.generated.h"

/** 正式库存定义动作清单的一次性编辑器迁移入口；它按现有实例语义写入资产，不把默认值覆盖逻辑放进运行时加载路径。 */
UCLASS()
class CATFISHINGEDITOR_API UCatInventoryActionsAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 扫描 Asset Registry 中所有 UCatInventoryItemDefinition 及其子类资产，写入与既有实例能力一致的有序动作清单并保存发生变化的包。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateFormalInventoryDefinitionActions();
};
