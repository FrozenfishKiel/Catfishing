#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingViewTypes.generated.h"

USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingViewState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly) ECatFishingPhase Phase = ECatFishingPhase::Created;
	UPROPERTY(BlueprintReadOnly) ECatFishingOutcome Outcome = ECatFishingOutcome::None;
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) double NormalizedFishStamina = 0.0;
	UPROPERTY(BlueprintReadOnly) bool bReeling = false;
	UPROPERTY(BlueprintReadOnly) bool bSlacking = false;
	UPROPERTY(BlueprintReadOnly) bool bPerfectHook = false;
	UPROPERTY(BlueprintReadOnly) ECatFishMotionIntent FishMotionIntent = ECatFishMotionIntent::None;
	UPROPERTY(BlueprintReadOnly) float FishLineAlignment = 0.0f;
	UPROPERTY(BlueprintReadOnly) float NormalizedLineLoad = 0.0f;
	UPROPERTY(BlueprintReadOnly) bool bStrongConfrontation = false;

	static FCatFishingViewState FromSnapshot(const FCatFishingSessionSnapshot& Snapshot);
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCatFishingViewStateChanged, const FCatFishingViewState&);
