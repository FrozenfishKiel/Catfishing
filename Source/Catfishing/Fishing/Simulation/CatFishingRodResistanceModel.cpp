#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

FCatFishingRodResistanceResult FCatFishingRodResistanceModel::Evaluate(
	const FCatFishingRodResistanceInput& Input)
{
	FCatFishingRodResistanceResult Result;
	if (!FMath::IsFinite(Input.CatStrength) || Input.CatStrength < 0.0
		|| !FMath::IsFinite(Input.FishStrength) || Input.FishStrength < 0.0
		|| !FMath::IsFinite(Input.RodPhysicsLengthCentimeters)
		|| Input.RodPhysicsLengthCentimeters <= 0.0
		|| !FMath::IsFinite(Input.NormalizedTension) || Input.NormalizedTension < 0.0
		|| !FMath::IsFinite(Input.NormalizedFishLineLoad) || Input.NormalizedFishLineLoad < 0.0
		|| !FMath::IsFinite(Input.RodLineAlignment))
	{
		return Result;
	}

	const double Tension = FMath::Clamp(Input.NormalizedTension, 0.0, 1.0);
	const double FishLineLoad = FMath::Clamp(Input.NormalizedFishLineLoad, 0.0, 1.0);
	const double Alignment = FMath::Clamp(Input.RodLineAlignment, 0.0, 1.0);
	const double PerpendicularLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - Alignment * Alignment));
	const double RodPhysicsLengthMeters = Input.RodPhysicsLengthCentimeters / 100.0;

	Result.FishResistingTorqueStrengthMeters = Input.FishStrength * FishLineLoad * Tension
		* PerpendicularLever * RodPhysicsLengthMeters;
	// 猫力量以一米参考力臂解释为可用转矩；配置杆长越长，鱼端杠杆越占优势。
	Result.CatTorqueCapacityStrengthMeters = Input.CatStrength;
	if (Result.CatTorqueCapacityStrengthMeters <= UE_DOUBLE_SMALL_NUMBER)
	{
		Result.TorqueLoadRatio = Result.FishResistingTorqueStrengthMeters > UE_DOUBLE_SMALL_NUMBER
			? 1.0 : 0.0;
	}
	else
	{
		Result.TorqueLoadRatio = FMath::Clamp(Result.FishResistingTorqueStrengthMeters
			/ Result.CatTorqueCapacityStrengthMeters, 0.0, 1.0);
	}
	Result.RotationSpeedMultiplier = 1.0 - Result.TorqueLoadRatio;
	Result.bRotationStalled = Result.RotationSpeedMultiplier <= UE_DOUBLE_KINDA_SMALL_NUMBER;
	Result.bSucceeded = true;
	return Result;
}
