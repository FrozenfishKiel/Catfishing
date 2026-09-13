#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Framework/Core/CatRunContracts.h"

#include "CatFishSelectionTypes.generated.h"

class UCatFishDefinition;

/**
 * 鱼种候选的可扩展条件门。测试期可让未验收条件保持旁路；正式启用时只切换配置，
 * 不改变挑战档、窝料/鱼饵权重和最终归一化流程。
 * 时段与天气两门的空数组读作「不受该轴约束」，见 .cpp 的流程注释。
 */
struct CATFISHING_API FCatFishEligibilityPolicy
{
	static bool PassesTimeOfDay(const UCatFishDefinition& Definition,
		ECatEnvironmentTimeOfDay TimeOfDay, bool bFilterEnabled);
	static bool PassesWeather(const UCatFishDefinition& Definition,
		ECatEnvironmentWeather Weather, bool bFilterEnabled);
	static bool PassesActivePlayerCount(const UCatFishDefinition& Definition, int32 ActivePlayerCount);
};

USTRUCT(BlueprintType)
struct FCatBaitWeightMultiplier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName BaitDefinitionId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	double Multiplier = 0.0;
};

USTRUCT()
struct FCatFishSelectionContext
{
	GENERATED_BODY()

	FCatWaterRegionHandle WaterRegion;
	FCatChumSample ChumSample;
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;
	FName BaitDefinitionId = NAME_None;
	int32 ActivePlayerCount = 0;
	double CombinedFishingStrength = 0.0;
	double CombinedFightStamina = 0.0;
	/**
	 * 已退出鱼力量主链：鱼的个体力量改按 UCatFishDefinition::FishStrengthPerKilogram 逐鱼换算
	 *（2026-09-09 八问④撤回工程自补的全局 K）。本字段只剩搏斗侧做功计价的同源校验用途，
	 * 选鱼链不再读它；等 Fishing 侧改用自己的平衡资产字段后可整条删除。
	 */
	double StrengthPerKilogram = 0.0;
	int32 RandomSeed = 0;
};

USTRUCT()
struct FCatFishSelectionResult
{
	GENERATED_BODY()

	bool bSelected = false;
	FName FishDefinitionId = NAME_None;
	double WeightKilograms = 0.0;
	/** 与 WeightKilograms 同一次确定性抽样对应的鱼力量（＝重量 × 该鱼力量系数K），进入搏斗后只再叠加完美中鱼倍率。 */
	double BaseFishStrength = 0.0;
	double SelectedFinalWeight = 0.0;
	double SelectedNormalizedProbability = 0.0;
	int32 EligibleCandidateCount = 0;
	// 墓碑（2026-09-13，D-29）：不再统计选中带；此值为参与最终归一化的正权重候选数。
	int32 PositiveWeightCandidateCount = 0;
	/**
	 * 本次是否走的无窝料基础池兜底（候选为空或总权重为零）。
	 * 走兜底时窝料/鱼饵/挑战度三项都没参与，日志与调试面板需要能区分这两条路。
	 */
	bool bFromBasePool = false;
};
