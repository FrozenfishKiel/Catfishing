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
	/** 重存已经完成拆分的正式物品资产并保留物品编号；缺少有效装备片段即失败，不再从旧字段创建装备资产。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateEquipmentDefinitions();
	/** 扫描 Asset Registry 中所有 UCatInventoryItemDefinition 及其子类资产，写入与既有实例能力一致的有序动作清单并保存发生变化的包。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateFormalInventoryDefinitionActions();

	/** 扫描正式装备物品定义，把片段已表达的 Use 行为写回 PreferredInstanceType，使正式资产不依赖运行时的未配置回退。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateFormalEquipmentUseInstanceTypes();

	/** 从当前 AbilitySettings 的默认集合拆出鱼竿操作和窝料集合，再通过物品的装备片段把引用写入独立装备资产。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateEquipmentAbilitySetGrants();
};
