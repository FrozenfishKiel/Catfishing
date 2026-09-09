#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

void FCatFishingRodEffortSampler::Reset(const FCatFishingRodRotationEffortSnapshot& Snapshot)
{
	PreviousSnapshot = Snapshot;
	PendingEffort = FCatFishingRodRotationEffortSnapshot{};
	PendingEffort.Epoch = Snapshot.Epoch;
}

FCatFishingRodRotationEffortSnapshot FCatFishingRodEffortSampler::Consume(
	const FCatFishingRodRotationEffortSnapshot& Snapshot, const double StepSeconds)
{
	FCatFishingRodRotationEffortSnapshot Result;
	Result.Epoch = Snapshot.Epoch;
	if (!FMath::IsFinite(StepSeconds) || StepSeconds <= 0.0
		|| !FMath::IsFinite(Snapshot.ExertionSquaredSeconds) || Snapshot.ExertionSquaredSeconds < 0.0
		|| !FMath::IsFinite(Snapshot.PositiveWorkRadians) || Snapshot.PositiveWorkRadians < 0.0
		|| !FMath::IsFinite(Snapshot.IntegratedSeconds) || Snapshot.IntegratedSeconds < 0.0)
	{
		return Result;
	}
	if (Snapshot.ExertionSquaredSeconds > Snapshot.IntegratedSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER) return Result;
	if (Snapshot.Epoch != PreviousSnapshot.Epoch)
	{
		// 新 Epoch 的累计值全部属于新持有人/新搏斗，从零接入；旧 Epoch 的积压不能跟随交接。
		FCatFishingRodRotationEffortSnapshot NewBaseline;
		NewBaseline.Epoch = Snapshot.Epoch;
		Reset(NewBaseline);
	}
	if (Snapshot.ExertionSquaredSeconds < PreviousSnapshot.ExertionSquaredSeconds
		|| Snapshot.PositiveWorkRadians < PreviousSnapshot.PositiveWorkRadians
		|| Snapshot.IntegratedSeconds < PreviousSnapshot.IntegratedSeconds)
	{
		// 同一 Epoch 的计数必须单调；意外回退只重新建基线，不能生成负努力或重放历史。
		Reset(Snapshot);
		return Result;
	}
	PendingEffort.ExertionSquaredSeconds += Snapshot.ExertionSquaredSeconds - PreviousSnapshot.ExertionSquaredSeconds;
	PendingEffort.PositiveWorkRadians += Snapshot.PositiveWorkRadians - PreviousSnapshot.PositiveWorkRadians;
	PendingEffort.IntegratedSeconds += Snapshot.IntegratedSeconds - PreviousSnapshot.IntegratedSeconds;
	PreviousSnapshot = Snapshot;
	if (PendingEffort.IntegratedSeconds <= UE_DOUBLE_SMALL_NUMBER) return Result;

	Result.IntegratedSeconds = FMath::Min(StepSeconds, PendingEffort.IntegratedSeconds);
	const double Fraction = Result.IntegratedSeconds / PendingEffort.IntegratedSeconds;
	Result.ExertionSquaredSeconds = PendingEffort.ExertionSquaredSeconds * Fraction;
	Result.PositiveWorkRadians = PendingEffort.PositiveWorkRadians * Fraction;
	PendingEffort.ExertionSquaredSeconds -= Result.ExertionSquaredSeconds;
	PendingEffort.PositiveWorkRadians -= Result.PositiveWorkRadians;
	PendingEffort.IntegratedSeconds -= Result.IntegratedSeconds;
	if (PendingEffort.IntegratedSeconds <= UE_DOUBLE_SMALL_NUMBER)
	{
		PendingEffort = FCatFishingRodRotationEffortSnapshot{};
		PendingEffort.Epoch = Snapshot.Epoch;
	}
	return Result;
}

FCatFishingRodResistanceResult FCatFishingRodResistanceModel::Evaluate(
	const FCatFishingRodResistanceInput& Input)
{
	FCatFishingRodResistanceResult Result;
	if (!FMath::IsFinite(Input.CatStrength) || Input.CatStrength < 0.0
		|| !FMath::IsFinite(Input.LineTensionNewtons) || Input.LineTensionNewtons < 0.0
		|| !FMath::IsFinite(Input.ForcePerStrengthNewtons) || Input.ForcePerStrengthNewtons <= 0.0
		|| !FMath::IsFinite(Input.RodPhysicsLengthCentimeters)
		|| Input.RodPhysicsLengthCentimeters <= 0.0
		|| !FMath::IsFinite(Input.RodLineAlignment))
	{
		return Result;
	}

	const double Alignment = FMath::Clamp(Input.RodLineAlignment, -1.0, 1.0);
	const double PerpendicularLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - Alignment * Alignment));
	const double RodPhysicsLengthMeters = Input.RodPhysicsLengthCentimeters / 100.0;

	Result.MaximumFishTorqueStrengthMeters = Input.LineTensionNewtons / Input.ForcePerStrengthNewtons * RodPhysicsLengthMeters;
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
		|| Input.PreviousSmoothedFishPullStrengthMeters.ContainsNaN()
		|| Input.PreviousAngularVelocityRadiansPerSecond.ContainsNaN()
		|| !FMath::IsFinite(Input.CatTorqueCapacity) || Input.CatTorqueCapacity < 0.0
		|| !FMath::IsFinite(Input.MaximumFishTorque) || Input.MaximumFishTorque < 0.0
		|| !FMath::IsFinite(Input.MaximumAngularSpeedDegreesPerSecond) || Input.MaximumAngularSpeedDegreesPerSecond <= 0.0
		|| !FMath::IsFinite(Input.ResponseSeconds) || Input.ResponseSeconds <= 0.0
		|| !FMath::IsFinite(Input.AngularInertiaSeconds) || Input.AngularInertiaSeconds <= 0.0
		|| !FMath::IsFinite(Input.FishPullSmoothingSeconds) || Input.FishPullSmoothingSeconds <= 0.0
		|| !FMath::IsFinite(Input.LoadedAngularDampingRatio) || Input.LoadedAngularDampingRatio < 0.0
		|| !FMath::IsFinite(Input.MinimumPitchDegrees) || Input.MinimumPitchDegrees < -89.0
		|| !FMath::IsFinite(Input.MaximumPitchDegrees) || Input.MaximumPitchDegrees > 89.0
		|| Input.MinimumPitchDegrees > Input.MaximumPitchDegrees
		|| !FMath::IsFinite(Input.DeltaSeconds) || Input.DeltaSeconds < 0.0) return Result;

	FVector Direction = Input.CurrentAim.Vector();
	const FVector RequestedDirection = Input.RequestedAim.Vector();
	const FVector TargetFishPull = Input.PullAxis.GetSafeNormal() * Input.MaximumFishTorque;
	Result.SmoothedFishPullStrengthMeters = Input.PreviousSmoothedFishPullStrengthMeters;
	Result.AngularVelocityRadiansPerSecond = Input.PreviousAngularVelocityRadiansPerSecond;
	const double MaximumSpeed = FMath::DegreesToRadians(Input.MaximumAngularSpeedDegreesPerSecond);
	const double Response = FMath::Max(Input.ResponseSeconds, 1.0 / 240.0);
	const double Inertia = FMath::Max(Input.AngularInertiaSeconds, 1.0 / 240.0);
	// 空载近目标时的转矩斜率为 1/Response；至少临界阻尼，避免惯性使瞄准反复越过目标。
	const double MinimumDamping = 2.0 * FMath::Sqrt(Inertia / Response);
	const double FishPullResponse = FMath::Max(Input.FishPullSmoothingSeconds, 1.0 / 240.0);
	const auto ConstrainVelocity = [&](const FVector& RodDirection, const FVector& Velocity)
	{
		// 该模型只有杆向的两个自由度，不存沿杆轴的无效自转。
		FVector Allowed = FVector::VectorPlaneProject(Velocity, RodDirection).GetClampedToMaxSize(MaximumSpeed);
		const FVector PitchAxis = FVector::CrossProduct(RodDirection, FVector::UpVector).GetSafeNormal();
		const double PitchSpeed = FVector::DotProduct(Allowed, PitchAxis);
		const double Pitch = RodDirection.Rotation().Pitch;
		if ((Pitch <= Input.MinimumPitchDegrees + UE_DOUBLE_SMALL_NUMBER && PitchSpeed < 0.0)
			|| (Pitch >= Input.MaximumPitchDegrees - UE_DOUBLE_SMALL_NUMBER && PitchSpeed > 0.0))
		{
			Allowed -= PitchAxis * PitchSpeed;
			Result.bHitPitchLimit = true;
		}
		return Allowed;
	};
	// 亚步积分让低帧率与高帧率看到相同的受力平衡；大卡顿只推进最多 0.25 秒，避免视觉猛甩。
	double RemainingSeconds = FMath::Min(Input.DeltaSeconds, 0.25);
	while (RemainingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double StepSeconds = FMath::Min(RemainingSeconds,
			FMath::Min(1.0 / 240.0, FMath::Min(Response, Inertia) * 0.25));
		const FVector PreviousVelocity = Result.AngularVelocityRadiansPerSecond;
		const FVector StartVelocity = ConstrainVelocity(Direction, PreviousVelocity);
		// 固定步鱼负载会因游向、松绷线切换而跳变。插值有向负载而不是欧拉角或最终 Mesh，
		// 同时平滑大小和方向（含反向/过零），让实际竿尖与鱼线求解消费同一连续姿态。
		// 中点负载用于本亚步积分，终点负载留给下一帧；指数响应不依赖渲染帧率。
		const double HalfAlpha = 1.0 - FMath::Exp(-0.5 * StepSeconds / FishPullResponse);
		const FVector AppliedFishPull = FMath::Lerp(Result.SmoothedFishPullStrengthMeters, TargetFishPull, HalfAlpha);
		Result.SmoothedFishPullStrengthMeters = FMath::Lerp(AppliedFishPull, TargetFishPull, HalfAlpha);
		const double TorqueScale = FMath::Max(UE_DOUBLE_SMALL_NUMBER,
			FMath::Max(Input.CatTorqueCapacity, AppliedFishPull.Size()));
		const double AimError = FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, RequestedDirection), -1.0, 1.0));
		const FVector CatAxis = FQuat::FindBetweenNormals(Direction, RequestedDirection).GetRotationAxis();
		// 猫朝瞄准意图施力，接近目标时连续减小。鱼线转矩有方向：外转受阻，回转得到助力。
		const FVector CatTorque = CatAxis * Input.CatTorqueCapacity
			* FMath::Clamp(AimError / (MaximumSpeed * Response), 0.0, 1.0);
		const FVector FishTorque = FVector::CrossProduct(Direction, AppliedFishPull);
		Result.NetTorque = CatTorque + FishTorque;
		// 归一化转矩沿用原力量尺度，静态平衡不变；它现在驱动角加速度，不再直接覆盖转速。
		// Inertia * dw/dt = Drive - Damping * w。负载或猫力量阶跃只改变加速度。
		const double LoadFraction = AppliedFishPull.Size() / TorqueScale;
		Result.AppliedAngularDampingMultiplier = FMath::Max(MinimumDamping,
			1.0 + Input.LoadedAngularDampingRatio * LoadFraction);
		const FVector Drive = (Result.NetTorque * (MaximumSpeed / TorqueScale)).GetClampedToMaxSize(MaximumSpeed);
		const FVector TerminalVelocity = Drive / Result.AppliedAngularDampingMultiplier;
		const double DecayRate = Result.AppliedAngularDampingMultiplier / Inertia;
		const double Decay = FMath::Exp(-DecayRate * StepSeconds);
		const FVector EndVelocity = TerminalVelocity + (StartVelocity - TerminalVelocity) * Decay;
		// 同一亚步用解析速度积分求实际转角，不能用末端速度乘 dt 虚增加速阶段位移与费用。
		const FVector RotationVector = TerminalVelocity * StepSeconds
			+ (StartVelocity - TerminalVelocity) * ((1.0 - Decay) / DecayRate);
		FVector NextDirection = Direction;
		const double Angle = RotationVector.Size();
		if (Angle > UE_DOUBLE_SMALL_NUMBER)
		{
			NextDirection = FQuat(RotationVector / Angle, Angle).RotateVector(Direction).GetSafeNormal();
		}
		FRotator NextAim = NextDirection.Rotation();
		const double AllowedPitch = FMath::Clamp(NextAim.Pitch, Input.MinimumPitchDegrees, Input.MaximumPitchDegrees);
		if (AllowedPitch != NextAim.Pitch)
		{
			NextAim.Pitch = AllowedPitch;
			NextDirection = NextAim.Vector();
			Result.bHitPitchLimit = true;
		}
		Result.AngularVelocityRadiansPerSecond = ConstrainVelocity(NextDirection, EndVelocity);
		Result.AngularAccelerationRadiansPerSecondSquared =
			(Result.AngularVelocityRadiansPerSecond - PreviousVelocity) / StepSeconds;
		// 只观察实际积分，不改变转矩或运动。支撑按归一化用力平方的时间积分，
		// 做功按主动转矩方向的真实转角加权；受阻不能再用最大转速伪造已完成弧长。
		const double CatEffortFraction = Input.CatTorqueCapacity > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(CatTorque.Size() / Input.CatTorqueCapacity, 0.0, 1.0) : 0.0;
		Result.CatExertionSquaredSeconds += FMath::Square(CatEffortFraction) * StepSeconds;
		const FVector ActualAxis = FVector::CrossProduct(Direction, NextDirection);
		const double ActualSine = ActualAxis.Size();
		const double ActualAngle = FMath::Atan2(ActualSine, FVector::DotProduct(Direction, NextDirection));
		const FVector ActualRotation = ActualSine > UE_DOUBLE_SMALL_NUMBER
			? ActualAxis * (ActualAngle / ActualSine) : FVector::ZeroVector;
		Result.CatPositiveWorkRadians += FMath::Max(0.0, FVector::DotProduct(ActualRotation, CatAxis)) * CatEffortFraction;
		Result.IntegratedSeconds += StepSeconds;
		Direction = NextDirection;
		RemainingSeconds -= StepSeconds;
	}
	Result.ActualAim = Direction.Rotation();
	Result.AngularSpeedDegreesPerSecond = FMath::RadiansToDegrees(Result.AngularVelocityRadiansPerSecond.Size());
	Result.bSucceeded = !Result.ActualAim.ContainsNaN() && !Result.AngularVelocityRadiansPerSecond.ContainsNaN()
		&& !Result.AngularAccelerationRadiansPerSecondSquared.ContainsNaN();
	return Result;
}
