#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Engine/DataTable.h"
#include "CatInventorySettings.generated.h"

/** 全项目物品的数字身份索引；定义资产持有内容，总表仅关联编号与资产。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatItemCatalogRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 永不随名称或排序改变的物品编号；策划登记、数字查询和校验读取，0 不代表任何物品。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1"))
	int32 ItemId = 0;

	/** 该编号唯一对应的静态定义；策划或迁移脚本登记，查询端读取其内容，总表不重复名字、图标和属性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UCatInventoryItemDefinition> ItemDefinition;
};

/**
 * 随身携带上限的分类；只有这两类消耗品有各自独立的随身总量（道具册：普通饵 8 份、窝料 5 份）。
 * 它不是物品的用途分类——分类事实仍在装备定义的能力判定里，这里只是「哪一条上限管这件东西」。
 */
UENUM()
enum class ECatInventoryCarryCategory : uint8
{
	/** 不受随身总量约束；竿、漂、抄网、鱼护、鱼与一切非饵非窝料的东西都在这一档。 */
	None = 0,
	/** 普通饵。 */
	Bait = 1,
	/** 窝料。 */
	Chum = 2
};

/** Catfishing 的正式库存目录设置；商店、拾取、营地和存档都从这里解析库存定义。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Inventory"))
class CATFISHING_API UCatInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 转换旧资产或存档中的英文物品引用；先整体预检，任一未知或冲突身份都会返回原因且不改对象。仅迁移工具与旧档加载调用。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Migration")
	static bool MigrateLegacyItemReferences(UObject* Object, FString& OutError);

	/** 枚举经过全表校验的定义并按编号排序；数字目录消费者读取，失败时不返回半份目录。 */
	bool GetItemDefinitions(TArray<UCatInventoryItemDefinition*>& OutDefinitions, FString& OutError) const;

	/** 玩家随身库存的项目默认格数；角色初始化、保存预检和 UI 空格渲染都读取同一个值。 */
	static constexpr int32 ProjectDefaultPlayerInventorySlotCapacity = 4;

	/** 数量型库存物品的项目默认单格容量；0 的语义是不限量堆叠。 */
	static constexpr int32 ProjectDefaultQuantityStackCapacity = 5;

	/** 按稳定 ID 查找唯一可运行的库存定义资产；重复、缺失或定义配置不一致时返回空。 */
	UCatInventoryItemDefinition* FindRuntimeDefinition(int32  ItemId) const;

	/** 按稳定 ID 查找指定定义类型；装备、鱼等上层系统用它从正式库存目录窄化自己认识的定义。 */
	template <typename DefinitionType>
	DefinitionType* FindRuntimeDefinition(int32  ItemId) const
	{
		return Cast<DefinitionType>(FindRuntimeDefinition(ItemId));
	}

	/** 读取玩家随身库存默认格数；角色初始化、保存预检和 UI 等待同步状态用它得到非负容量。 */
	int32 GetPlayerInventorySlotCapacity() const;

	/** 读取数量型物品的默认配置容量；返回原始非负配置，0 表示不设硬上限。 */
	int32 GetDefaultQuantityStackCapacity() const;

	/** 读取数量型物品的有效单格上限；配置为 0 时返回 MAX_int32，让容量预演和显示使用同一语义。 */
	int32 GetDefaultQuantityStackLimit() const;

	/** 读取普通饵的随身总量上限；<= 0 表示未设上限，调用方按 MAX_int32 处理。 */
	int32 GetBaitCarryLimit() const;

	/** 读取窝料的随身总量上限；<= 0 表示未设上限，调用方按 MAX_int32 处理。 */
	int32 GetChumCarryLimit() const;

	/**
	 * 判定一份物品定义受哪一条随身总量上限约束。
	 * 只读装备定义已有的能力判定（CanServeFishingBait / CanServeChumPlacement），不新增任何必填资产字段——
	 * 八款饵和四款窝料资产今天就能被认出来，不存在「资产没配过所以静默失效」。
	 */
	static ECatInventoryCarryCategory ResolveCarryCategory(const UCatInventoryItemDefinition& ItemDefinition);

	/** 读取某一分类的随身总量上限；None 分类与未配置都返回 MAX_int32。 */
	int32 GetCarryLimitForCategory(ECatInventoryCarryCategory Category) const;

public:
	/** 全部物品的数字索引表；项目配置指定，全部物品查询读取；鱼目录仅决定候选鱼集合。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TSoftObjectPtr<UDataTable> ItemCatalog;

	/** 玩家随身库存默认可见格数；服务器初始化正式背包，UI 在复制未到位时也用它渲染空格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 PlayerInventorySlotCapacity = ProjectDefaultPlayerInventorySlotCapacity;

	/** 数量型物品未在定义资产上声明 MaxStackSize 时采用的单格容量；0 表示同类数量物尽量堆进一个格。 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 DefaultQuantityStackCapacity = ProjectDefaultQuantityStackCapacity;

	/**
	 * 普通饵的随身携带上限，单位是份（道具册：8 份，2026-08-19 由 5 改）。
	 * 0 表示不设总量上限——这是「这条规则还没配」的安全值，不是「一份都不能带」。
	 * 判定谁算普通饵不新增资产字段：直接问装备定义的 CanServeFishingBait()，已有八款饵资产天然成立。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 BaitCarryLimit = 0;

	/**
	 * 窝料的随身携带上限，单位是份（道具册：5 份）。
	 * 0 的语义与 BaitCarryLimit 相同；判定谁算窝料同样只问 CanServeChumPlacement()，不新增必填字段。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "0"))
	int32 ChumCarryLimit = 0;

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

	/** 连续丢弃的执行间隔，单位秒；库存队列据此安排首件及后续单件，空队列中的独立单件请求仍立即执行。 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.05", Units = "s"))
	float DropIntervalSeconds = 0.35f;
};
