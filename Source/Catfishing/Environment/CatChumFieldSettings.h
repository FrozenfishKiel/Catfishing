#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "Environment/CatWaterTypes.h"

#include "CatChumFieldSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Chum Fields"))
class CATFISHING_API UCatChumFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	bool IsRuntimeReady() const;
	bool TryGetInfluenceRadiusScale(double& OutRadiusScale) const;
	bool IsGatheringConfigurationValid() const;
	bool MeetsGatheringThreshold(const FCatChumVector& Concentration) const;

	UPROPERTY(Config, EditAnywhere, Category = "Gathering")
	bool bEnableFishGathering = false;
	/** 浓度单位，三轴各自达标；不是旧库存模型的鱼条数。 */
	UPROPERTY(Config, EditAnywhere, Category = "Gathering")
	FCatChumVector GatheringConcentrationThreshold;
	UPROPERTY(Config, EditAnywhere, Category = "Gathering", meta=(ClampMin="0", ClampMax="1"))
	double GatheringTriggerProbability = 0.08;
	UPROPERTY(Config, EditAnywhere, Category = "Gathering")
	double GatheringDurationSeconds = 45.0;
	UPROPERTY(Config, EditAnywhere, Category = "Gathering")
	double GatheringBiteSpeedMultiplier = 2.5;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableChumFieldRuntime = false;

	/** 对所有窝料定义基础圆面积的全局倍率；半径在运行时按 sqrt(倍率) 缩放。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.0"))
	double InfluenceAreaMultiplier = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Budget")
	int32 MaxActiveFieldsPerRegion = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Budget")
	double MaxRawContributionPerRegion = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Placement")
	double MaxPlacementRangeCentimeters = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Placement")
	double MaxAimDeviationDegrees = 0.0;

	UPROPERTY(Config, EditAnywhere, Category = "Placement")
	TEnumAsByte<ECollisionChannel> PlacementLineOfSightChannel = ECC_Visibility;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	double ExpiredCleanupIntervalSeconds = 0.0;
};
