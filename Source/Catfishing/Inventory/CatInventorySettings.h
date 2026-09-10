#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatInventorySettings.generated.h"

/** 库存目录中的一条稳定 ID 到物品定义资产映射；所有入库、移动、保存和加载都通过这里解析定义。 */
USTRUCT(BlueprintType)
struct FCatInventoryCatalogDefinition
{
	GENERATED_BODY()

	/** 项目内稳定物品 ID；商店、存档和 Equipment 读模型都用它作为跨系统钥匙。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	FName DefinitionId = NAME_None;

	/** 这条稳定 ID 对应的库存物品定义资产；资产保存展示、堆叠和片段语义。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSoftObjectPtr<UCatInventoryItemDefinition> ItemDefinition;

	/** 目录项必须能加载到运行可用定义，且定义自己的稳定 ID 必须与目录 ID 一致。 */
	bool IsRuntimeReady() const;
};

/** Catfishing 的正式库存目录设置；商店、拾取、营地和存档都从这里解析库存定义。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Inventory"))
class CATFISHING_API UCatInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 玩家随身库存的项目默认格数；角色初始化、保存预检和 UI 空格渲染都读取同一个值。 */
	static constexpr int32 ProjectDefaultPlayerInventorySlotCapacity = 24;

	/** 数量型库存物品的项目默认单格容量；0 的语义是不限量堆叠。 */
	static constexpr int32 ProjectDefaultQuantityStackCapacity = 5;

	/** 按稳定 ID 查找唯一可运行的库存定义资产；重复、缺失或定义配置不一致时返回空。 */
	UCatInventoryItemDefinition* FindRuntimeDefinition(FName DefinitionId) const;

	/** 按稳定 ID 查找指定定义类型；装备、草药等上层系统用它从正式库存目录窄化自己认识的定义。 */
	template <typename DefinitionType>
	DefinitionType* FindRuntimeDefinition(FName DefinitionId) const
	{
		return Cast<DefinitionType>(FindRuntimeDefinition(DefinitionId));
	}

	/** 读取玩家随身库存默认格数；角色初始化、保存预检和 UI 等待同步状态用它得到非负容量。 */
	int32 GetPlayerInventorySlotCapacity() const;

	/** 读取数量型物品的默认配置容量；返回原始非负配置，0 表示不设硬上限。 */
	int32 GetDefaultQuantityStackCapacity() const;

	/** 读取数量型物品的有效单格上限；配置为 0 时返回 MAX_int32，让容量预演和显示使用同一语义。 */
	int32 GetDefaultQuantityStackLimit() const;

public:
	/** 正式库存物品目录；商店、营地和随身物品都从这里读取定义。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<FCatInventoryCatalogDefinition> Definitions;

	/** 玩家随身库存默认可见格数；服务器初始化正式背包，UI 在复制未到位时也用它渲染空格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 PlayerInventorySlotCapacity = ProjectDefaultPlayerInventorySlotCapacity;

	/** 数量型物品未在定义资产上声明 MaxStackSize 时采用的单格容量；0 表示同类数量物尽量堆进一个格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 DefaultQuantityStackCapacity = ProjectDefaultQuantityStackCapacity;

	/** 自动放置可搜索的最远水平距离，单位厘米；服务器与本地操作提示读取，不能借客户端落点越过该范围。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "1.0", Units = "cm"))
	double PlacementRangeCentimeters = 150.0;

	/** 候选地面相对猫脚底允许的最大高低差，单位厘米；放置检测用它拒绝远高台阶和悬崖下方。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.0", Units = "cm"))
	double PlacementHeightDifferenceCentimeters = 30.0;

	/** 可以稳定放置物品的地面最大坡度，单位度；服务器检查接触面，过陡时继续寻找其他候选位置。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	double PlacementSlopeDegrees = 30.0;

	/** 丢弃时沿角色水平朝向的初速度，单位厘米每秒；只施加一次，不在 Tick 中修正弹道。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.0"))
	double DropForwardSpeed = 250.0;

	/** 丢弃时竖直向上的初速度，单位厘米每秒；与重力共同形成轻抛，不影响固定放置。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.0"))
	double DropUpwardSpeed = 200.0;
};
