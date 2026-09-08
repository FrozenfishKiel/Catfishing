#include "Fishing/Simulation/CatFishingFightWorkModel.h"

bool FCatFishingFightWorkModel::ComputeCatWorkDrain(const FCatFightCatWorkInput& Input, double& OutDrain)
{
	OutDrain = 0.0;
	for (const double Value : {Input.PositiveWorkUnits, Input.CostPerWorkUnit, Input.NormalizedLoad,
		Input.UnloadedWorkMultiplier, Input.LoadStaminaMultiplier, Input.ActionMultiplier})
	{
		if (!FMath::IsFinite(Value) || Value < 0.0) return false;
	}
	if (Input.NormalizedLoad > 1.0) return false;
	OutDrain = Input.PositiveWorkUnits * Input.CostPerWorkUnit * Input.ActionMultiplier
		* (Input.UnloadedWorkMultiplier + Input.NormalizedLoad * Input.LoadStaminaMultiplier);
	return FMath::IsFinite(OutDrain);
}

bool FCatFishingFightWorkModel::ComputeFishEffortDrain(const FCatFightFishEffortInput& Input, double& OutDrain)
{
	OutDrain = 0.0;
	if (!FMath::IsFinite(Input.EffortRatio) || Input.EffortRatio < 0.0 || Input.EffortRatio > 1.0
		|| !FMath::IsFinite(Input.OppositionRatio) || Input.OppositionRatio < 0.0 || Input.OppositionRatio > 1.0
		|| !FMath::IsFinite(Input.StaminaPerSecond) || Input.StaminaPerSecond < 0.0
		|| !FMath::IsFinite(Input.DeltaSeconds) || Input.DeltaSeconds < 0.0)
	{
		return false;
	}

	OutDrain = Input.StaminaPerSecond * FMath::Square(Input.EffortRatio)
		* Input.OppositionRatio * Input.DeltaSeconds;
	return FMath::IsFinite(OutDrain);
}
