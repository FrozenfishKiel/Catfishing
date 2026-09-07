#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatInventorySettings.generated.h"

/** 库存目录中的一条稳定 ID 到物品定义类映射；迁移期用它把旧 DefinitionId 引到新 InventoryDefinition。 */
USTRUCT(BlueprintType)
struct FCatInventoryCatalogDefinition
{
	GENERATED_BODY()

	/** 项目内稳定物品 ID；商店、存档和旧 Equipment 快照迁移时都用它作为跨系统钥匙。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	FName DefinitionId = NAME_None;

	/** 这条稳定 ID 对应的新库存物品定义类；类默认对象保存展示、堆叠和片段语义。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass = nullptr;

	/** 判断这条映射是否能进入运行目录；定义类必须存在，且定义默认对象的稳定 ID 必须与目录 ID 一致。 */
	bool IsRuntimeReady() const;
};

/** Catfishing 的正式库存目录设置；后续商店、拾取、营地和存档都应从这里解析库存定义。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Inventory"))
class CATFISHING_API UCatInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查找唯一可运行的库存定义类；重复、缺失或定义配置不一致时返回空。 */
	TSubclassOf<UCatInventoryItemDefinition> FindRuntimeDefinitionClass(FName DefinitionId) const;

	/** 按稳定 ID 读取库存定义类默认对象；调用方只能把返回值当静态配置。 */
	const UCatInventoryItemDefinition* FindRuntimeDefinition(FName DefinitionId) const;

public:
	/** 正式库存物品目录；迁移期可与旧 EquipmentSettings 并存，最终应成为非鱼随身物品的唯一目录。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<FCatInventoryCatalogDefinition> Definitions;
};
