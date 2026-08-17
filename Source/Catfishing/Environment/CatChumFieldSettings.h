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

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableChumFieldRuntime = false;

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
