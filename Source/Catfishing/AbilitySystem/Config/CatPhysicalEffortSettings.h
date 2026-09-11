#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatPhysicalEffortSettings.generated.h"

/** Personal cat motion prices. Distances are cm; prices are stamina points per metre. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Physical Effort"))
class CATFISHING_API UCatPhysicalEffortSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Effort", meta=(ClampMin="0"))
	double StaminaPerUnfulfilledMeter = 2.0;
	/** Equivalent full-effort intent for a stationary stance, independent of walk speed. */
	UPROPERTY(Config, EditAnywhere, Category="Effort", meta=(ClampMin="0"))
	double SupportReferenceSpeedCmS = 100.0;
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0"))
	double RecoveryPerSecond = 5.0;
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0"))
	double RecoveryDelaySeconds = 2.0;
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0.01", ClampMax="1"))
	double ExhaustionResumeRatio = 0.2;
	bool IsValid() const;
};
