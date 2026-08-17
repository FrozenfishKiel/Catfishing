#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Framework/Core/CatRunContracts.h"

#include "CatFishSelectionTypes.generated.h"

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
};
