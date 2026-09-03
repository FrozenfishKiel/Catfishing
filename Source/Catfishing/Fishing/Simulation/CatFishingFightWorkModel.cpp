#include "Fishing/Simulation/CatFishingFightWorkModel.h"

bool FCatFishingFightWorkModel::ComputeDrain(const FCatFightWorkInput& Input, double& OutDrain,
	double& OutEffectiveEffortDistanceCentimeters)
{
	OutDrain = 0.0;
	OutEffectiveEffortDistanceCentimeters = 0.0;
	if (!FMath::IsFinite(Input.Strength) || Input.Strength < 0.0
		|| !FMath::IsFinite(Input.IntendedLineDistanceCentimeters) || Input.IntendedLineDistanceCentimeters < 0.0
		|| !FMath::IsFinite(Input.ActualLineDistanceCentimeters) || Input.ActualLineDistanceCentimeters < 0.0
		|| !FMath::IsFinite(Input.IsometricEffortMultiplier) || Input.IsometricEffortMultiplier < 0.0
		|| !FMath::IsFinite(Input.CostPerStrengthCentimeter) || Input.CostPerStrengthCentimeter < 0.0
		|| !FMath::IsFinite(Input.PhaseMultiplier) || Input.PhaseMultiplier < 0.0)
	{
		return false;
	}

	const double Realized = FMath::Min(Input.ActualLineDistanceCentimeters,
		Input.IntendedLineDistanceCentimeters);
	const double Blocked = FMath::Max(0.0, Input.IntendedLineDistanceCentimeters - Realized);
	OutEffectiveEffortDistanceCentimeters = Realized + Blocked * Input.IsometricEffortMultiplier;
	OutDrain = Input.Strength * OutEffectiveEffortDistanceCentimeters
		* Input.CostPerStrengthCentimeter * Input.PhaseMultiplier;
	return FMath::IsFinite(OutDrain);
}
