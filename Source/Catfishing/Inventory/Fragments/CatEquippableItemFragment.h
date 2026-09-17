#pragma once
#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquippableItemFragment.generated.h"

class UCatEquippedDefinition;

/** 物品与装备配置的关联；物品定义通过组合声明装备能力，不再继承装备定义。 */
UCLASS(EditInlineNew, DefaultToInstanced, BlueprintType)
class CATFISHING_API UCatEquippableItemFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
public:
	/** 拿出本物品时采用的装备配置；策划选择资产，装备实例据此授予能力，宿主行为负责生成世界表现。 */
	UPROPERTY(EditDefaultsOnly, Category="装备", meta=(DisplayName="装备定义"))
	TObjectPtr<UCatEquippedDefinition> EquipmentDefinition;
	/** 缺少装备定义、无效能力集或显式世界类无法加载时拒绝运行，避免拿出后才发现配置失效。 */
	virtual bool IsRuntimeReady() const override;
};
