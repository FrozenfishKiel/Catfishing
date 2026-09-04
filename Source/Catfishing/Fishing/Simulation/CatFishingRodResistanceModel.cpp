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
	const double Alignment = FMath::Clamp(Input.RodLineAlignment, -1.0, 1.0);
	const double PerpendicularLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - Alignment * Alignment));
	const double RodPhysicsLengthMeters = Input.RodPhysicsLengthCentimeters / 100.0;

	Result.MaximumFishTorqueStrengthMeters = Input.FishStrength * FishLineLoad * Tension * RodPhysicsLengthMeters;
	Result.FishResistingTorqueStrengthMeters = Result.MaximumFishTorqueStrengthMeters * PerpendicularLever;
	// 猫力量以一米参考力臂解释为可用转矩；配置杆长越长，鱼端杠杆越占优势。
	Result.CatTorqueCapacityStrengthMeters = Input.CatStrength;
	Result.bSucceeded = true;
	return Result;
}

FCatFishingRodRotationResult FCatFishingRodResistanceModel::StepRotation(
	const FCatFishingRodRotationInput& Input)
{
	FCatFishingRodRotationResult Result;
	if (Input.CurrentAim.ContainsNaN() || Input.RequestedAim.ContainsNaN()
		|| Input.PullAxis.ContainsNaN() || Input.PullAxis.IsNearlyZero()
		|| !FMath::IsFinite(Input.CatTorqueCapacity) || Input.CatTorqueCapacity < 0.0
		|| !FMath::IsFinite(Input.MaximumFishTorque) || Input.MaximumFishTorque < 0.0
		|| !FMath::IsFinite(Input.MaximumAngularSpeedDegreesPerSecond) || Input.MaximumAngularSpeedDegreesPerSecond <= 0.0
		|| !FMath::IsFinite(Input.ResponseSeconds) || Input.ResponseSeconds <= 0.0
		|| !FMath::IsFinite(Input.DeltaSeconds) || Input.DeltaSeconds < 0.0) return Result;

	FVector Direction = Input.CurrentAim.Vector();
	const FVector RequestedDirection = Input.RequestedAim.Vector();
	const FVector PullAxis = Input.PullAxis.GetSafeNormal();
	const double MaximumSpeed = FMath::DegreesToRadians(Input.MaximumAngularSpeedDegreesPerSecond);
	const double Response = FMath::Max(Input.ResponseSeconds, 1.0 / 240.0);
	const double TorqueScale = FMath::Max(UE_DOUBLE_SMALL_NUMBER,
		FMath::Max(Input.CatTorqueCapacity, Input.MaximumFishTorque));
	// 亚步积分让低帧率与高帧率看到相同的受力平衡；大卡顿只推进最多 0.25 秒，避免视觉猛甩。
	double RemainingSeconds = FMath::Min(Input.DeltaSeconds, 0.25);
	while (RemainingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double StepSeconds = FMath::Min(RemainingSeconds, FMath::Min(1.0 / 120.0, Response * 0.25));
		const double AimError = FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, RequestedDirection), -1.0, 1.0));
		const FVector CatAxis = FQuat::FindBetweenNormals(Direction, RequestedDirection).GetRotationAxis();
		// 猫朝瞄准意图施力，接近目标时连续减小。鱼线转矩有方向：外转受阻，回转得到助力。
		const FVector CatTorque = CatAxis * Input.CatTorqueCapacity
			* FMath::Clamp(AimError / (MaximumSpeed * Response), 0.0, 1.0);
		const FVector FishTorque = FVector::CrossProduct(Direction, PullAxis) * Input.MaximumFishTorque;
		Result.NetTorque = CatTorque + FishTorque;
		// 粘性阻尼：净转矩决定本步角速度；没有硬角度上限或锁定/解锁分支。
		const FVector AngularVelocity = (Result.NetTorque * (MaximumSpeed / TorqueScale)).GetClampedToMaxSize(MaximumSpeed);
		const double Speed = AngularVelocity.Size();
		if (Speed > UE_DOUBLE_SMALL_NUMBER)
		{
			Direction = FQuat(AngularVelocity / Speed, Speed * StepSeconds).RotateVector(Direction).GetSafeNormal();
		}
		Result.AngularSpeedDegreesPerSecond = FMath::RadiansToDegrees(Speed);
		RemainingSeconds -= StepSeconds;
	}
	Result.ActualAim = Direction.Rotation();
	Result.bSucceeded = !Result.ActualAim.ContainsNaN();
	return Result;
}
