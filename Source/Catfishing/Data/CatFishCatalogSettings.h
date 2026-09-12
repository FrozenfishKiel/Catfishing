#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
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

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，只读取显式登记的鱼种资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	TSoftObjectPtr<class UCurveFloat> ChumSaturationCurve;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double ChumAffinityHalfSaturation = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Selection")
	double MaximumChumModifier = 0.0;

	/** 测试期默认关闭；开启后才按 FishDefinition.TimeOfDay 过滤候选鱼。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableTimeOfDayEligibilityFilter = false;

	/** 测试期默认关闭；开启后才按 FishDefinition.Weather 过滤候选鱼。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Eligibility")
	bool bEnableWeatherEligibilityFilter = false;

	/** 挑战度不高于该值的鱼归入轻松带；挑战度以力量比为下限，并由力量/体力调和均值连续抬升。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double ComfortChallengeMaximumRatio = 0.0;

	/** 挑战度不高于该值且高于轻松带上限的鱼归入势均力敌带。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MatchedChallengeMaximumRatio = 0.0;

	/** 可进入抽取池的挑战度安全上限；允许略强于当前玩家的鱼出现，超过此值仍 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MaximumChallengeRatio = 0.0;

	/** 连续挑战权重的峰值位置；越接近该比例，鱼在所属难度带内的相对权重越高。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double TargetChallengeRatio = 0.0;

	/** 轻松带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double ComfortChallengeBandWeight = 0.0;

	/** 势均力敌带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double MatchedChallengeBandWeight = 0.0;

	/** 高风险带的抽取权重；只在该带存在候选时参与归一化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0"))
	double RiskyChallengeBandWeight = 0.0;

	/** 难度离目标最远时仍保留的正倍率，避免挑战匹配抹掉鱼饵、窝料和稀有度的生态作用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection|Challenge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumChallengeWeightMultiplier = 0.0;

	/**
	 * 取「稀有鱼」完美系数的稀有度档 ID 清单；不在清单里的档一律按「普通鱼」取（钓鱼规则 §3.4 末句）。
	 * 鱼册五档 普通／少见／稀有／珍稀／事件，最高一档＝珍稀；清单为空时全部按普通鱼取。
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
