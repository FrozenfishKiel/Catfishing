#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatInventorySettings.generated.h"

/** 库存目录中的一条稳定 ID 到物品定义资产映射；迁移期可直接挂现有 Equip_* 资产。 */
USTRUCT(BlueprintType)
struct FCatInventoryCatalogDefinition
{
	GENERATED_BODY()

	/** 项目内稳定物品 ID；商店、存档和旧 Equipment 快照迁移时都用它作为跨系统钥匙。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	FName DefinitionId = NAME_None;

	/** 这条稳定 ID 对应的库存物品定义资产；资产保存展示、堆叠和片段语义。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSoftObjectPtr<UCatInventoryItemDefinition> ItemDefinition;

	/** 目录项必须能加载到运行可用定义，且定义自己的稳定 ID 必须与目录 ID 一致。 */
	bool IsRuntimeReady() const;
};

/** Catfishing 的正式库存目录设置；后续商店、拾取、营地和存档都应从这里解析库存定义。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Inventory"))
class CATFISHING_API UCatInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查找唯一可运行的库存定义资产；重复、缺失或定义配置不一致时返回空。 */
	UCatInventoryItemDefinition* FindRuntimeDefinition(FName DefinitionId) const;

public:
	/** 正式库存物品目录；迁移期可与旧 EquipmentSettings 并存，商店、营地和随身物品应逐步改读它。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<FCatInventoryCatalogDefinition> Definitions;
};
