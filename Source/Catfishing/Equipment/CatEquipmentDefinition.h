#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/CatChumFieldTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentDefinition.generated.h"

class AActor;
class UTexture2D;

/** 一条功能型装备/道具定义；字段只表达玩法用途，不含等级、战力、随机词条或强制升级。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatEquipmentDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 校验稳定 ID、类别和类别专属字段；任何 Unset 都阻止装配或消耗。 */
	bool IsRuntimeDefinitionReady() const;

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

	/** 这类物品通过 Use 部署到世界时生成的 Actor 类；鱼竿放置从实例定义读取它，不再共用全局鱼竿 Actor 配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	TSoftClassPtr<AActor> UseActorClass;

	/** 单格最大堆叠数；0 表示沿用项目默认规则，1 表示这类物品不可堆叠。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	int32 MaxStackSize = 0;

	/** 是否为本局数量型物品；Bait、Chum、Herb 会以数量栈进入随身库存，Rod、Float 和 ScoopNet 不能打开。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bRunConsumable = false;

	/** Bait 的特殊身份标记；它只区分偏好和失败惩罚语义，不再决定该饵是否需要一局数量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bSpecialBait = false;

	/** Rod 的最大耐久；非 Rod 必须为 0，具体损耗公式不在定义里。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumRodDurability = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double FishingStrength = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumLineLengthCentimeters = 0.0;

	/** 转矩公式使用的玩法杆长；不读取 Mesh Bounds，换皮和视觉缩放不会改变遛鱼手感。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "1.0", Units = "cm"))
	double RodPhysicsLengthCentimeters = 200.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double BaseDurabilityWearPerSecond = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
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
