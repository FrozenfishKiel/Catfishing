#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/CatWaterTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentDefinition.generated.h"

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

	/**
	 * 是否为一局消耗品，也就是会进入角色随身耗材栈、按份领取和扣减的那类东西。
	 * 飞书钓鱼规则 §3.4（2026-08-18 拍定）写明"进入咬钩后无论结局均消耗 1 份饵——上鱼也扣"，普通饵携带上限 5 份、营地免费自取，
	 * 所以 Bait 不论普通还是特殊都必须为 true；Chum 同样为 true；Rod 必须为 false。旧口径"普通饵无限、不是消耗品"已经作废。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bRunConsumable = false;

	/**
	 * Bait 是不是特殊饵。它只区分普通饵与特殊饵这两种玩法身份（鱼偏好、每日进货限量、丢饵失败预算只认特殊饵），
	 * 不再承担"是否消耗"的判断——两种饵现在都是一局消耗品。非 Bait 类别必须保持 false。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bSpecialBait = false;

	/** Rod 的最大耐久；非 Rod 必须为 0，具体损耗公式不在定义里。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumRodDurability = 0.0;

	/**
	 * Rod 的强度：一根竿在遛鱼力量判定里能扛住多大的对抗力，是一局内不变的静态值，和会磨损归零的耐久是两个不同概念。
	 * 飞书装备册（2026-08-18 拍定）三档为树枝竿 25／玩具竿 60／逗猫棒竿 130；遛鱼判定表 4.3 第①条用它：向外游时拖竿、强度 ≤ min(猫力, 鱼力) 就断竿。
	 * Rod 必须为正，非 Rod 必须为 0；只由 FishingSession 在服务器读取。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double RodStrength = 0.0;

	/**
	 * Rod 的放线上限 L_max，单位米：遛鱼时已放线长 L 最多能放到多少；到顶后右键放线失效、只能拖（飞书钓鱼规则 4.3 最后一行）。
	 * 飞书装备册三档为 60／80／100 米。Rod 必须为正，非 Rod 必须为 0；只由 FishingSession 在服务器读取。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumLineLengthMeters = 0.0;

	/**
	 * 鱼漂的射程，单位米：这只漂最远能把浮漂送到离猫多远的水面。
	 * 飞书装备册（2026-08-18 拍定）三种漂为羽毛漂 3／毛线球漂 5／铃铛漂 7 米。
	 * 抛竿规则写的是"点击距离 ≤ 浮漂射程才可抛出"，但项目还没有点击瞄准输入，
	 * 所以当前由 Fishing 用"猫的朝向 × 这个射程"直接定落点，射程因此同时决定了遛鱼开局的 D₀。
	 * Float 必须为正，非 Float 必须为 0；只由服务器读取，进入装备目录 ContentHash。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double FloatCastRangeMeters = 0.0;

	/**
	 * 鱼漂的精准度，用落点随机偏移的半径表达，单位米：数越小越准，落点越贴近瞄的位置。
	 * 飞书装备册只给了"高／中／低"三档定性描述，没给数值（数值归数值阶段），所以三种漂上的具体半径是工程暂定（决策记录 D-23）。
	 * Float 必须为正，非 Float 必须为 0；只由服务器读取，进入装备目录 ContentHash。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double FloatAccuracyOffsetRadiusMeters = 0.0;

	/** 公开的功能路线 ID；钓鱼/表现按稳定 ID消费，不比较大小。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Function")
	FName FunctionalRouteId = NAME_None;

	/** Chum 耗材提交给共享 WaterRegion 的三轴增量；其他类别必须保持零值，客户端不能覆盖该数据。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Function")
	FCatChumVector ChumContribution;

	/** 数据人员对正式定义的显式运行 gate；默认关闭。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Runtime")
	bool bEnableRuntimeDefinition = false;
};
