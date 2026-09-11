#pragma once

#include "CoreMinimal.h"
#include "Physics/Simulation/CatIntentMotionModel.h"

/** Personal movement observation. Rod rotation, reeling and rod support remain separate. */
struct CATFISHING_API FCatFightOperatorMovementCostInput
{
    FVector MoveIntentWorld = FVector::ZeroVector;
    FVector ActualDisplacementCentimeters = FVector::ZeroVector;
    double MaximumMoveSpeedCentimetersPerSecond = 0.0;
    double FixedStepSeconds = 0.0;
    double ActiveStrength = 0.0;
    double StaminaPerUnfulfilledMeter = 2.0;
    double MovementStaminaMultiplier = 1.0;
};

using FCatFightOperatorMovementCostResult = FCatIntentMotionResult;

/** Adapts primary movement to the same directional deficit model used by fish and physical helpers. */
class CATFISHING_API FCatFishingOperatorWorkModel
{
public:
    static bool ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
        FCatFightOperatorMovementCostResult& OutResult);
};
