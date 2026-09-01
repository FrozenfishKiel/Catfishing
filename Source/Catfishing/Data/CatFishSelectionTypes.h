#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Framework/Core/CatRunContracts.h"

#include "CatFishSelectionTypes.generated.h"

class UCatFishDefinition;

/**
 * 鱼种候选的可扩展条件门。测试期可让未验收条件保持旁路；正式启用时只切换配置，
 * 不改变挑战档、窝料/鱼饵权重和最终归一化流程。
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
	int32 RandomSeed = 0;
};

USTRUCT()
struct FCatFishSelectionResult
{
	GENERATED_BODY()

	bool bSelected = false;
	FName FishDefinitionId = NAME_None;
	double WeightKilograms = 0.0;
	double SelectedFinalWeight = 0.0;
	double SelectedNormalizedProbability = 0.0;
	int32 EligibleCandidateCount = 0;
	int32 SelectedBandCandidateCount = 0;
};
