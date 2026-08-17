#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatFishPersonalityDefinition.generated.h"

UCLASS(BlueprintType)
class CATFISHING_API UCatBitePersonalityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName BitePersonalityId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double ProbeDurationSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double TrueBiteWindowSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double PerfectHookWindowSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectFishStrengthMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectFishStaminaMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectInitialLineLengthMultiplier = 0.0;
};

UCLASS(BlueprintType)
class CATFISHING_API UCatFightPersonalityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName FightPersonalityId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double CalmMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double StruggleMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.01")) double BaseDrainMultiplier = 1.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) double StruggleDrainMultiplier = 0.0;
};
