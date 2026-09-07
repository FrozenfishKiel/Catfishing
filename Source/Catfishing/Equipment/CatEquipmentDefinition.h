#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Equipment/CatEquipmentTypes.h"
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
	/** 库存目录读取装备资产时使用 EquipmentDefinitionId；这样商店和背包不再需要知道 EquipmentSettings 的字段名。 */
	virtual FName GetInventoryDefinitionId() const override;

	/** 库存表现读取装备资产自己的显示名；避免迁移期维护第二份 InventoryDisplayName。 */
	virtual FText GetInventoryDisplayName() const override;

	/** 库存详情读取装备资产自己的说明；钓鱼参数仍留给装备/钓鱼系统解释。 */
	virtual FText GetInventoryDescription() const override;

	/** 库存格读取装备资产自己的缩略图；运行实例和格子不保存表现资源。 */
	virtual TSoftObjectPtr<UTexture2D> GetInventoryThumbnail() const override;

	/** 装备资产进入库存运行目录仍沿用原运行 gate；失败时库存、商店和钓鱼都应拒绝使用。 */
	virtual bool IsInventoryRuntimeDefinitionReady() const override;

	/** 装备资产默认生成装备适配实例；鱼竿耐久等专属状态不进入通用库存格。 */
	virtual TSubclassOf<UCatInventoryItemInstance> GetPreferredInstanceType() const override;

	/** 装备资产的库存堆叠上限；数量型默认读库存项目配置，工具和装备保持一格一件。 */
	virtual int32 GetMaxStackCount() const override;

	/** 把装备旧配置解析为库存层 Use 策略；资产仍保留旧字段，运行裁决读库存统一口径。 */
	virtual ECatInventoryItemUseEffect GetInventoryUseEffect() const override;

	/** 校验这条定义能否进入运行目录；服务器目录读取它做 fail-closed，失败会阻止装配、Use 裁决和消耗事务。 */
	bool IsRuntimeDefinitionReady() const;

	/** 统一物品入口在提交库存事务前调用的 Use 裁决；它只读取当前实例、数量和定义数据，返回值决定 Equipment 是否继续移动或扣量。 */
	virtual ECatDomainCommandError Use(const FCatRunInventorySlot& Item, int32 Quantity) const;

	/** 统一停止使用入口在放回活动实例前调用的 UnUse 裁决；失败会让 Equipment 保持活动记录，不按定义重新生成物品。 */
	virtual ECatDomainCommandError UnUse(const FCatRunInventorySlot& Item) const;

	/** 这类物品 Use 成功后是否由活动记录暂存整份实例；Equipment 读取它区分部署型物品和 no-op/扣量型物品。 */
	virtual bool KeepsInventoryInstanceWhileUsed() const override;

	/** 这类物品 Use 成功后是否直接扣库存数量；Equipment 读取它处理已经完成玩法前置裁决的数量耗材。 */
	virtual bool ConsumesInventoryQuantityOnUse() const override;

	/** 读取旧装备资产声明或兼容推导出的库存影响策略；运行代码随后会映射到库存层策略，保留它只为旧资产和测试字段。 */
	virtual ECatEquipmentUseInventoryEffect GetUseInventoryEffect() const;

	/** 装备/道具稳定 ID；Profile 选择、运行装配和鱼偏好只引用该值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName EquipmentDefinitionId = NAME_None;

	/** 功能类别；不映射数值强弱。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	ECatEquipmentKind Kind = ECatEquipmentKind::Unknown;

	/** 跨局 Profile 选择使用的稳定槽位 ID；非装配型消耗品保持 None。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	FName LoadoutSlotId = NAME_None;

	/** 使用该定义需要的 Profile UnlockId；None 表示正式 starter 可用，不代表全定义免费。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	FName RequiredUnlockId = NAME_None;

	/** 玩家可见名称；库存格和商店表现优先读取它，未配置时才回退到稳定 ID。 */
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

	/** 旧装备资产声明的库存影响；当前运行会映射成 ECatInventoryItemUseEffect，字段暂留以保护 DataAsset 和历史测试。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	ECatEquipmentUseInventoryEffect UseInventoryEffect = ECatEquipmentUseInventoryEffect::Auto;

	/** 单格最大堆叠数；0 表示沿用项目默认规则，1 表示这类物品不可堆叠。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	int32 MaxStackSize = 0;

	/** 是否为本局数量型物品；Bait、Chum、Herb 会以数量栈进入随身库存，Rod、Float 和 ScoopNet 不能打开。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bRunConsumable = false;

	/** Bait 的特殊身份标记；它只区分偏好和失败惩罚语义，不再决定该饵是否需要一局数量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bSpecialBait = false;

	/** 鱼竿实例耐久上限；仅新物品与营地维修补满，钓鱼磨损跨会话保留。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "鱼竿耐久上限"))
	double MaximumRodDurability = 0.0;

	/** 旧承载阈值，仅保留既有资产/蓝图字段兼容；运行就绪与搏斗不再读取。 */
	UPROPERTY(BlueprintReadOnly, Category = "Deprecated", meta = (DeprecatedProperty,
		DeprecationMessage = "旧承载阈值已停用；力量差由双端约束处理，鱼竿损坏由实例耐久决定。"))
	double FishingStrength = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumLineLengthCentimeters = 0.0;

	/** 转矩公式使用的玩法杆长；不读取 Mesh Bounds，换皮和视觉缩放不会改变遛鱼手感。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "1.0", Units = "cm"))
	double RodPhysicsLengthCentimeters = 200.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "鱼竿基础磨损每秒"))
	double BaseDurabilityWearPerSecond = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "绷线磨损倍率"))
	double HighTensionWearMultiplier = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform RodTipLocalTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform StandLocalTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform GripLocalTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double MaximumCastDistanceCentimeters = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double CastErrorStandardDeviationCentimeters = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double MaximumCastErrorRadiusCentimeters = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double BiteSignalStability = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bait", meta = (ClampMin = "0.0"))
	double BiteRateMultiplier = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bait", meta = (ClampMin = "0.0"))
	double MinimumBiteDelayMultiplier = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scoop", meta = (ClampMin = "0.0"))
	double ScoopReachCentimeters = 0.0;

	/** 公开的功能路线 ID；钓鱼/表现按稳定 ID消费，不比较大小。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Function")
	FName FunctionalRouteId = NAME_None;

	/** Chum 耗材提交给共享 WaterRegion 的三轴增量；其他类别必须保持零值，客户端不能覆盖该数据。 */
	/** Chum placement 的空间影响定义；运行时冻结曲线 LUT，数量只放大三轴贡献。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chum")
	FCatChumInfluenceSpec ChumInfluence;

	/** 数据人员对正式定义的显式运行 gate；默认关闭。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Runtime")
	bool bEnableRuntimeDefinition = false;
};
