#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"
#include "Fishing/Simulation/CatFishingFightWorkModel.h"

namespace
{
	bool IsWorkFiniteNonNegative(double Value) { return FMath::IsFinite(Value) && Value >= 0.0; }
	bool IsWorkFiniteVector(const FVector& Value) { return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z); }
	FVector MakeWorkGroundIntent(const FVector& Value) { return FVector(Value.X, Value.Y, 0.0).GetClampedToMaxSize(1.0); }
}
bool FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
	FCatFightOperatorMovementCostResult& OutResult)
{
	OutResult = {};
	if (!IsWorkFiniteVector(Input.MoveIntentWorld) || !IsWorkFiniteVector(Input.ActualDisplacementCentimeters)) return false;
	for (const double Value : {Input.MaximumMoveSpeedCentimetersPerSecond, Input.FixedStepSeconds,
		Input.ActiveStrength, Input.StandardStrength, Input.CostPerStrengthCentimeter, Input.NormalizedLoad,
		Input.UnloadedWorkMultiplier, Input.LoadStaminaMultiplier, Input.MovementStaminaMultiplier,
		Input.SupportStaminaPerSecond})
	{
		if (!IsWorkFiniteNonNegative(Value)) return false;
	}
	if (Input.NormalizedLoad > 1.0) return false;
	const FVector Intent = MakeWorkGroundIntent(Input.MoveIntentWorld);
	const double IntentMagnitude = Intent.Size();
	if (Input.ActiveStrength <= 0.0 || IntentMagnitude <= 0.0
		|| Input.FixedStepSeconds <= 0.0 || Input.MaximumMoveSpeedCentimetersPerSecond <= 0.0) return true;
	FCatFightOperatorMovementCostResult Result;
	Result.IntendedDistanceCentimeters = IntentMagnitude * Input.MaximumMoveSpeedCentimetersPerSecond * Input.FixedStepSeconds;
	if (!FMath::IsFinite(Result.IntendedDistanceCentimeters)) return false;
	const double SignedProgress = FVector::DotProduct(Input.ActualDisplacementCentimeters, Intent / IntentMagnitude);
	if (!FMath::IsFinite(SignedProgress)) return false;
	Result.ActualProgressCentimeters = FMath::Clamp(SignedProgress, 0.0, Result.IntendedDistanceCentimeters);
	Result.BlockedEffortRatio = Result.IntendedDistanceCentimeters > 0.0
		? IntentMagnitude * (1.0 - Result.ActualProgressCentimeters / Result.IntendedDistanceCentimeters) : 0.0;
	FCatFightCatWorkInput Work;
	Work.PositiveWorkUnits = Input.StandardStrength * Result.ActualProgressCentimeters;
	Work.CostPerWorkUnit = Input.CostPerStrengthCentimeter;
	Work.NormalizedLoad = Input.NormalizedLoad;
	Work.UnloadedWorkMultiplier = Input.UnloadedWorkMultiplier;
	Work.LoadStaminaMultiplier = Input.LoadStaminaMultiplier;
	Work.ActionMultiplier = Input.MovementStaminaMultiplier;
	if (!FCatFishingFightWorkModel::ComputeCatWorkDrain(Work, Result.WorkStaminaDrain)) return false;
	// Opposite efforts can spend stamina with zero net travel. Reuse the existing timed-support price,
	// without inventing a conflict penalty or charging somebody for passive displacement.
	Result.SupportStaminaDrain = Input.SupportStaminaPerSecond * Input.FixedStepSeconds
		* FMath::Square(Result.BlockedEffortRatio) * Input.MovementStaminaMultiplier;
	Result.StaminaDrain = Result.WorkStaminaDrain + Result.SupportStaminaDrain;
	if (!IsWorkFiniteNonNegative(Result.StaminaDrain)) return false;
	OutResult = Result;
	return true;
}
