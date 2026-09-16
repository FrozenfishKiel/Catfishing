#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Framework/Core/CatRunContracts.h"

#include "CatFishSelectionTypes.generated.h"

class UCatFishDefinition;

/**
 * 鱼种候选的可扩展条件门。测试期可让未验收条件保持旁路；正式启用时只切换配置，
 * 不改变窝料选类、类内鱼饵权重和最终归一化流程。
 * T23：时段与天气过滤开启后，空数组按未配置拒绝并 Warning；关闭开关仍遵守 D-31。
 */
struct CATFISHING_API FCatFishEligibilityPolicy
{
	static bool PassesTimeOfDay(const UCatFishDefinition& Definition,
		ECatEnvironmentTimeOfDay TimeOfDay, bool bFilterEnabled);
	static bool PassesWeather(const UCatFishDefinition& Definition,
		ECatEnvironmentWeather Weather, bool bFilterEnabled);
	static bool PassesActivePlayerCount(const UCatFishDefinition& Definition, int32 ActivePlayerCount);
};

/** 本鱼对某种鱼饵的选鱼权重配置；策划按物品总表编号登记，选鱼读取倍率而非改写基础权重。 */
USTRUCT(BlueprintType)
struct FCatBaitWeightMultiplier
{
	GENERATED_BODY()

	/** 这条倍率配置关联的鱼饵种类编号；策划填写、鱼定义校验和选鱼读取，0 表示未配置且不能通过定义校验。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32  BaitItemId = 0;
	/** 使用该鱼饵时乘到本鱼权重上的倍率；策划填写、选鱼读取，必须有限且大于 0，1 表示不改变权重。 */
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	double Multiplier = 0.0;


	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName BaitDefinitionId = NAME_None;
};

USTRUCT()
struct FCatFishSelectionContext
{
	GENERATED_BODY()

	FCatWaterRegionHandle WaterRegion;
	FCatChumSample ChumSample;
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;
	/** 原抛竿者当前鱼饵的数字物品编号；会话组装输入、选鱼读取，0 无法匹配正式倍率配置而使用中性倍率。 */
	int32  BaitItemId = 0;
	int32 ActivePlayerCount = 0;
	double CombinedFishingStrength = 0.0;
	double CombinedFightStamina = 0.0;
	/** 猫方计价/等效质量换算的冻结值，仅供 Session 开场同源校验；选鱼不读取此字段。 */
	double StrengthPerKilogram = 0.0;
	/** 抛竿者成长的重量上浮比例；抽样时夹到本鱼种上限，后续力量与实物共用该重量。 */
	double CatchWeightBonus = 0.0;
	int32 RandomSeed = 0;
};

USTRUCT()
struct FCatFishSelectionResult
{
	GENERATED_BODY()

	/** 本次先选中的窝料类：0腥/1香/2酵；基础池为 INDEX_NONE。 */
	int32 SelectedChumClass = INDEX_NONE;
	double SelectedChumClassProbability = 0.0;
	bool bSelected = false;
	/** 本次选中的鱼种数字物品编号；选鱼成功时写入，会话据此解析鱼定义，默认 0 表示尚无选中鱼种，不是实例 GUID。 */
	int32  ItemId = 0;
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
