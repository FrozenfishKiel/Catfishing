#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "CatChumFieldSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Chum Fields"))
class CATFISHING_API UCatChumFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	bool IsRuntimeReady() const;
	bool TryGetInfluenceRadiusScale(double& OutRadiusScale) const;

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
