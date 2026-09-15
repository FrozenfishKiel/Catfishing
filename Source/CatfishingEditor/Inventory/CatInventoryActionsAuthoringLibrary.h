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

	/** 扫描正式装备定义并把片段已表达的 Use 行为写回 PreferredInstanceType；运行时不会替资产猜测作者选择。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateFormalEquipmentUseInstanceTypes();

	/** 从当前 AbilitySettings 的默认集合拆出鱼竿操作和窝料来源集合，并把引用写回正式装备定义。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateEquipmentAbilitySetGrants();
};
