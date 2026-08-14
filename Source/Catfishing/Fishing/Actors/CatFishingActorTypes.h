#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingActorTypes.generated.h"

class APlayerState;

UENUM(BlueprintType)
enum class ECatFishingHookPresentationPhase : uint8
{
	Unconfigured,
	CastFlight,
	Landed,
	Failed
};

USTRUCT(BlueprintType)
struct FCatFishingRodPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RodActorId;
	UPROPERTY(BlueprintReadOnly) int64 RodActorRevision = 0;
	UPROPERTY(BlueprintReadOnly) FName RodDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName RodSkinDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> OwnerPlayerState = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> OperatorPlayerState = nullptr;
	UPROPERTY(BlueprintReadOnly) bool bDeployed = false;
	UPROPERTY(BlueprintReadOnly) bool bBroken = false;
};

USTRUCT(BlueprintType)
struct FCatFishingHookPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) ECatFishingHookPresentationPhase Phase = ECatFishingHookPresentationPhase::Unconfigured;
};

USTRUCT(BlueprintType)
struct FCatFishEncounterPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	UPROPERTY(BlueprintReadOnly) double CurrentLineLength = 0.0;
};
