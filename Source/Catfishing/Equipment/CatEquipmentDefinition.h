#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentDefinition.generated.h"

class AActor;
class UTexture2D;

/** 一条功能型装备/道具定义；字段只表达玩法用途，不含等级、战力、随机词条或强制升级。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatEquipmentDefinition : public UCatInventoryItemDefinition
{
	GENERATED_BODY()

public:
	/** 库存目录读取装备资产时使用 EquipmentDefinitionId；商店和背包只消费库存定义的稳定身份。 */
	virtual FName GetInventoryDefinitionId() const override;

	/** 库存表现读取装备资产自己的显示名；背包和商店把缺省回退留在各自展示模型里。 */
	virtual FText GetInventoryDisplayName() const override;

	/** 库存详情读取装备资产自己的说明；钓鱼参数仍留给装备/钓鱼系统解释。 */
	virtual FText GetInventoryDescription() const override;

	/** 库存格读取装备资产自己的缩略图；运行实例和格子不保存表现资源。 */
	virtual TSoftObjectPtr<UTexture2D> GetInventoryThumbnail() const override;

	/** 装备资产进入库存目录时调用；只有装备定义自己的正式运行能力完整时才允许入库。 */
	virtual bool IsInventoryRuntimeDefinitionReady() const override;

	/** 装备资产默认生成装备适配实例；鱼竿耐久等专属状态不进入通用库存格。 */
	virtual TSubclassOf<UCatInventoryItemInstance> GetPreferredInstanceType() const override;

	/** 装备资产的库存堆叠上限；数量型默认读库存项目配置，工具和装备保持一格一件。 */
	virtual int32 GetMaxStackCount() const override;

	/** 校验通用身份、实例类型和全部片段；新增能力通过片段扩展，无须修改中央用途列表。 */
	bool IsRuntimeDefinitionReady() const;

	/** 鱼竿钓具槽的稳定数据 ID；钓具选择和存档用它对齐现有 Rod 槽，不承担物品用途分类。 */
	static FName FishingRodLoadoutSlotId();

	/** 鱼饵钓具槽的稳定数据 ID；Fishing 预算和装配槽用它对齐现有 Bait 槽。 */
	static FName FishingBaitLoadoutSlotId();

	/** 鱼漂钓具槽的稳定数据 ID；抛投裁决用它对齐现有 Float 槽。 */
	static FName FishingFloatLoadoutSlotId();

	/** 抄网钓具槽的稳定数据 ID；捕获和装配校验用它对齐现有 ScoopNet 槽。 */
	static FName ScoopNetLoadoutSlotId();

	/** 鱼竿入口要求同一份定义同时描述 Rod 槽、部署 Actor、耐久、线长和手部锚点；装配、部署、耐久和存档读模型都按这些字段组合接入。 */
	bool CanServeFishingRod() const;

	/** 鱼饵入口只接受本局数量物和咬钩倍率字段完整的定义；Fishing 使用冻结和失败预算用它排除鱼竿、鱼漂和普通部署物。 */
	bool CanServeFishingBait() const;

	/** 鱼漂入口读取射程、误差和信号稳定字段；抛投距离裁决和 Profile 装配用它排除没有这些字段语义的定义。 */
	bool CanServeFishingFloat() const;

	/** 抄网入口只需要有效范围字段；捕获命令和装配校验用它确认这份定义能进入抄取流程。 */
	bool CanServeScoopNet() const;

	/** 窝料入口要求非钓具槽、数量物和聚鱼影响配置同时成立；投放命令和调试补货按这些字段组合进入消耗流程。 */
	bool CanServeChumPlacement() const;

	/** 判断这份定义能否填入指定钓具槽；Profile、存档和读模型用它校验现有四个选择槽。 */
	bool CanServeFishingLoadoutSlot(FName SlotId) const;

	/** 装备/道具稳定 ID；Profile 选择、运行装配和鱼偏好只引用该值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName EquipmentDefinitionId = NAME_None;

	/** 跨局 Profile 选择使用的稳定槽位 ID；非装配型消耗品保持 None。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	FName LoadoutSlotId = NAME_None;

	/** 使用该定义需要的 Profile UnlockId；None 表示正式 starter 可用，不代表全定义免费。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	FName RequiredUnlockId = NAME_None;

	/** 玩家可见名称；库存格和商店表现读取它，空名称时显示稳定 ID。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	/** 玩家可见说明；库存详情、提示面板和后续商店详情只读它，不参与玩法判定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText Description;

	/** 库存格缩略图；WBP 通过 SlotView 读取它，后端库存格保存运行实例身份和数量，不保存表现资源。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** 这类物品通过 Use 部署到世界时生成的 Actor 类；策划数据写入它，具体玩法表现从自己的定义读取，鱼竿只是其中一种。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	TSoftClassPtr<AActor> UseActorClass;

	/** 单格最大堆叠数；0 表示沿用项目默认规则，1 表示这类物品不可堆叠。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	int32 MaxStackSize = 0;

	/** 是否为本局数量型物品；Bait、Chum 和片段型耗材会以数量栈进入随身库存，Rod、Float 和 ScoopNet 不是数量栈物品。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bRunConsumable = false;

	/** 公开的功能路线 ID；钓鱼/表现按稳定 ID 消费，不比较大小。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Function")
	FName FunctionalRouteId = NAME_None;

	/** 数据人员对正式定义的显式运行 gate；默认关闭。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Runtime")
	bool bEnableRuntimeDefinition = false;

};
