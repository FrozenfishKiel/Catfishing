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
	/** 玩家随身库存的项目默认格数；迁移期旧 EquipmentSettings 只有在测试或诊断显式改值时才应覆盖它。 */
	static constexpr int32 ProjectDefaultPlayerInventorySlotCapacity = 24;

	/** 数量型库存物品的项目默认单格容量；0 的语义是不限量堆叠，旧配置迁移时以这个默认值识别未改动状态。 */
	static constexpr int32 ProjectDefaultQuantityStackCapacity = 5;

	/** 按稳定 ID 查找唯一可运行的库存定义资产；重复、缺失或定义配置不一致时返回空。 */
	UCatInventoryItemDefinition* FindRuntimeDefinition(FName DefinitionId) const;

	/** 读取玩家随身库存默认格数；角色初始化和 UI fallback 用它得到非负容量，不再直接读 EquipmentSettings。 */
	int32 GetPlayerInventorySlotCapacity() const;

	/** 读取数量型物品的默认配置容量；返回原始非负配置，0 保留为“尽量堆在一个格子里”的策划语义。 */
	int32 GetDefaultQuantityStackCapacity() const;

	/** 读取数量型物品的有效单格上限；配置为 0 时返回 MAX_int32，让容量预演和显示使用同一语义。 */
	int32 GetDefaultQuantityStackLimit() const;

public:
	/** 正式库存物品目录；迁移期可与旧 EquipmentSettings 并存，商店、营地和随身物品应逐步改读它。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<FCatInventoryCatalogDefinition> Definitions;

	/** 玩家随身库存默认可见格数；服务器初始化正式背包，UI 在复制未到位时也用它渲染空格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 PlayerInventorySlotCapacity = ProjectDefaultPlayerInventorySlotCapacity;

	/** 数量型物品未在定义资产上声明 MaxStackSize 时采用的单格容量；0 表示同类数量物尽量堆进一个格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 DefaultQuantityStackCapacity = ProjectDefaultQuantityStackCapacity;
};
