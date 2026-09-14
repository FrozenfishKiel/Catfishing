#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishSelectionTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatFishCatalogSettings.generated.h"

class UCatFishDefinition;

/**
 * 完美提竿的入场削减系数（钓鱼规则 §3.4）；按鱼册稀有度档取，入场时直接乘到本场实际值，
 * 此后运动求解、负载、消耗、碾压判定全用削后值。未配置的档位退回 1.0，只会不削减，绝不放大。
 */
USTRUCT(BlueprintType)
struct FCatPerfectHookReduction
{
	GENERATED_BODY()

	/** 鱼力量倍率；普通鱼 0.8、稀有鱼 0.85。 */
	UPROPERTY(BlueprintReadOnly)
	double FishStrengthMultiplier = 1.0;

	/** 鱼体力倍率；普通鱼 0.85、稀有鱼 0.9。 */
	UPROPERTY(BlueprintReadOnly)
	double FishStaminaMultiplier = 1.0;

	/** 初始线长倍率；三项削减的第三项，不按稀有度分档，单值挂参数页。 */
	UPROPERTY(BlueprintReadOnly)
	double InitialLineLengthMultiplier = 1.0;
};

/**
 * 无窝料基础池的一个名额（鱼表格子表「基础池」）。
 * 基础池是「候选为空或总权重为零」时的兜底名册（2026-09-08 补缺口轮，设计修改记录.md:263）：
 * 它不参与窝料/鱼饵/挑战度那套加权，只按自己的固定概率抽。
 */
USTRUCT()
struct FCatFishBasePoolEntry
{
	GENERATED_BODY()

	/** 名册成员的鱼种稳定 ID；必须能在 Definitions 里解析出一条就绪鱼定义，否则该名额被跳过。 */
	UPROPERTY(EditAnywhere, Config)
	FName FishDefinitionId = NAME_None;

	/** 该成员在基础池里的相对概率（子表「基础池概率（占位）」列）；<= 0 的名额不参与抽取。 */
	UPROPERTY(EditAnywhere, Config, meta = (ClampMin = "0.0"))
	double Probability = 0.0;
};

/** 正式鱼表资产目录；它是运行时鱼定义的唯一枚举入口，不从文件名或 Content 扫描猜内容。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fish Catalog"))
class CATFISHING_API UCatFishCatalogSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 查找完整且启用的鱼定义；重复 ID 或加载失败返回空。 */
	UCatFishDefinition* FindRuntimeDefinition(FName FishDefinitionId) const;

	FCatFishSelectionResult SelectRuntimeDefinition(const FCatFishSelectionContext& Context) const;

	/**
	 * 按本鱼的 RarityTierId 取完美提竿削减系数；命中 RarePerfectHookRarityTierIds 的按稀有鱼取，其余档按普通鱼取。
	 * 削减系数不挂性格模板（四套 Bite_* 模板 2026-09-09 裁为测试用），也不逐鱼配。
	 */
	FCatPerfectHookReduction ResolvePerfectHookReduction(const UCatFishDefinition& Definition) const;

	/**
	 * 按食性取段末向外概率；未裁（配置为 0 或越界）与 Unset 食性都返回 0，
	 * 调用方据此退回测试期性格模板，不得把 0 当成「这条鱼永远向内游」。
	 */
	double ResolveDietOutwardSegmentProbability(ECatFishDiet Diet) const;

	/**
	 * 从无窝料基础池抽一条鱼；SelectRuntimeDefinition 在「候选为空」与「总权重为零」两处调用它，
	 * 是这两种情形下唯一的出鱼路径。它不读窝料/鱼饵/挑战度，只按名册自己的概率抽。
	 */
	FCatFishSelectionResult SelectFromBasePool(const FCatFishSelectionContext& Context,
		const TCHAR* FallbackReason) const;

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，只读取显式登记的鱼种资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	TSoftObjectPtr<class UCurveFloat> ChumSaturationCurve;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double ChumAffinityHalfSaturation = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double MaximumChumModifier = 0.0;

	/**
	 * 测试期默认关闭；开启后才按 FishDefinition.TimeOfDay 过滤候选鱼。
	 * T23：开启后鱼定义 TimeOfDay 为空数组＝未配置，拒绝并 Warning（不得当作不受约束），
	 * 候选被筛空时还有基础池兜底。默认值仍是 False——开启节点待策划给（台账 D-31）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableTimeOfDayEligibilityFilter = false;

	/** 测试期默认关闭；开启后才按 FishDefinition.Weather 过滤候选鱼。空数组语义与时段门相同。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableWeatherEligibilityFilter = false;

	/**
	 * 无窝料基础池名册（鱼表格子表「基础池」）。
	 * 候选为空或总权重为零时从这里抽一条，保证「空窝不空钩」（2026-09-08 李前臻裁）。
	 * 名册本身是设计侧待办（张佳），默认空——空名册时选鱼仍然返回未选中，只是会多记一条 Warning。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|BasePool")
	TArray<FCatFishBasePoolEntry> BasePool;

	/**
	 * Fish_*.uasset 的体力列是否还是 2026-09-08 之前的「每鱼种一份定额体力」。
	 *
	 * 为什么是一个总开关而不是按值猜：算过了，猜不出来。旧定额 ＝ 系数 × 重量中点，
	 * 而重量中点在鱼表里横跨 0.22~27.5 kg，于是旧定额（15~261）与正式系数（9.5~250）两个区间完全重叠——
	 * 小银鱼的旧定额 15 比它的系数 67 小，湖心巨影的旧定额 261 比它的系数 9.5 大。
	 * 任何「超过某个数就当成旧定额」的阈值都会既漏判又误判。
	 * 但这 16 份资产是同一批、按同一个表 revision 生成的，迁与不迁是整体状态，所以用一个显式开关表达。
	 *
	 * 为 true 时取值按裁决给的占位口径「原定额 ÷ 重量中点」现场折算并记 Warning（设计修改记录.md:275）；
	 * 它只是让没迁数据的工程能开起来，不是正式数值来源。
	 * 张佳终版鱼表重生成资产之后，把这一行改成 False，过渡逻辑整条失效。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Migration")
	bool bFishAssetsStillHoldLegacyFlatFightStamina = true;

	/**
	 * 食性 → 段末「下一段向外」的基础概率（鱼的行为 §2 的 P_base）。三个值都在 (0,1) 内才生效。
	 * 默认 0 ＝ 未裁：食性列仍然读，但不改变行为，鱼按测试期性格模板跑。
	 * 为什么不直接写死 70/50/35：那三个数只出现在「鱼的行为」页，该页页头写明「待整页重写，实现不读本页」，
	 * 也没有进裁决账本——由策划落到本配置或参数页之后再启用。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CarnivoreOutwardSegmentProbability = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double OmnivoreOutwardSegmentProbability = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double HerbivoreOutwardSegmentProbability = 0.0;

	// 墓碑（2026-09-13，D-29）：三带阈值、选带权重和连续挑战倍率配置已删除，保留硬安全门。
	/** 可进入抽取池的挑战度安全上限；允许略强于当前玩家的鱼出现，超过此值仍 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MaximumChallengeRatio = 0.0;

	/**
	 * 取「稀有鱼」完美系数的稀有度档 ID 清单；不在清单里的档一律按「普通鱼」取（钓鱼规则 §3.4 末句）。
	 * 墓碑（2026-09-14）：五档含事件的旧口径退役（设计修改记录 2026-09-13 裁决⑤）。
	 * 现行四档最高为珍稀，实际资产 ID 为 Rare；巨影按普通系数，清单为空时也全部按普通鱼取。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook")
	TArray<FName> RarePerfectHookRarityTierIds;

	/** 普通鱼完美提竿的鱼力量倍率（设计 0.8）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CommonPerfectFishStrengthMultiplier = 0.0;

	/** 普通鱼完美提竿的鱼体力倍率（设计 0.85）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double CommonPerfectFishStaminaMultiplier = 0.0;

	/** 稀有鱼完美提竿的鱼力量倍率（设计 0.85）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double RarePerfectFishStrengthMultiplier = 0.0;

	/** 稀有鱼完美提竿的鱼体力倍率（设计 0.9）；0 或越界表示未配置，取值时退回 1.0。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double RarePerfectFishStaminaMultiplier = 0.0;

	/** 完美线长系数：完美中鱼时初始线长再乘它，不按稀有度分档；0 或越界表示未配置，取值时退回 1.0（即不缩线）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Perfect Hook", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double PerfectInitialLineLengthMultiplier = 0.0;
};
