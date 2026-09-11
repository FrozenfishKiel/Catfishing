#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"

bool FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
    FCatFightOperatorMovementCostResult& OutResult)
{
    OutResult = {};
    if (Input.MoveIntentWorld.ContainsNaN() || Input.ActualDisplacementCentimeters.ContainsNaN()) return false;
    for (double Value : {Input.MaximumMoveSpeedCentimetersPerSecond, Input.FixedStepSeconds,
        Input.ActiveStrength, Input.StaminaPerUnfulfilledMeter, Input.MovementStaminaMultiplier})
        if (!FMath::IsFinite(Value) || Value < 0) return false;
    FCatIntentMotionInput Intent;
    const FVector Direction = FVector(Input.MoveIntentWorld.X, Input.MoveIntentWorld.Y, 0).GetClampedToMaxSize(1);
    if (Input.ActiveStrength > 0)
        Intent.IntendedDisplacementCentimeters = Direction * Input.MaximumMoveSpeedCentimetersPerSecond * Input.FixedStepSeconds;
    Intent.ActualDisplacementCentimeters = Input.ActualDisplacementCentimeters;
    Intent.StaminaPerUnfulfilledMeter = Input.StaminaPerUnfulfilledMeter * Input.MovementStaminaMultiplier;
    return FCatIntentMotionModel::ComputeDrain(Intent, OutResult);
}
