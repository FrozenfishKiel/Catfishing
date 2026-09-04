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
		|| !FMath::IsFinite(Snapshot.IntentArcCentimeters) || Snapshot.IntentArcCentimeters < 0.0
		|| !FMath::IsFinite(Snapshot.ActualArcCentimeters) || Snapshot.ActualArcCentimeters < 0.0
		|| !FMath::IsFinite(Snapshot.IntegratedSeconds) || Snapshot.IntegratedSeconds < 0.0)
	{
		return Result;
	}
	if (Snapshot.Epoch != PreviousSnapshot.Epoch)
	{
		// 新 Epoch 的累计值全部属于新持有人/新搏斗，从零接入；旧 Epoch 的积压不能跟随交接。
		FCatFishingRodRotationEffortSnapshot NewBaseline;
		NewBaseline.Epoch = Snapshot.Epoch;
		Reset(NewBaseline);
	}
	if (Snapshot.IntentArcCentimeters < PreviousSnapshot.IntentArcCentimeters
		|| Snapshot.ActualArcCentimeters < PreviousSnapshot.ActualArcCentimeters
		|| Snapshot.IntegratedSeconds < PreviousSnapshot.IntegratedSeconds)
	{
		// 同一 Epoch 的计数必须单调；意外回退只重新建基线，不能生成负努力或重放历史。
		Reset(Snapshot);
		return Result;
	}
	PendingEffort.IntentArcCentimeters += Snapshot.IntentArcCentimeters - PreviousSnapshot.IntentArcCentimeters;
	PendingEffort.ActualArcCentimeters += Snapshot.ActualArcCentimeters - PreviousSnapshot.ActualArcCentimeters;
	PendingEffort.IntegratedSeconds += Snapshot.IntegratedSeconds - PreviousSnapshot.IntegratedSeconds;
	PreviousSnapshot = Snapshot;
	if (PendingEffort.IntegratedSeconds <= UE_DOUBLE_SMALL_NUMBER) return Result;

	Result.IntegratedSeconds = FMath::Min(StepSeconds, PendingEffort.IntegratedSeconds);
	const double Fraction = Result.IntegratedSeconds / PendingEffort.IntegratedSeconds;
	Result.IntentArcCentimeters = PendingEffort.IntentArcCentimeters * Fraction;
	Result.ActualArcCentimeters = PendingEffort.ActualArcCentimeters * Fraction;
	PendingEffort.IntentArcCentimeters -= Result.IntentArcCentimeters;
	PendingEffort.ActualArcCentimeters -= Result.ActualArcCentimeters;
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
		|| Input.PreviousSmoothedFishPullStrengthMeters.ContainsNaN()
		|| !FMath::IsFinite(Input.CatTorqueCapacity) || Input.CatTorqueCapacity < 0.0
		|| !FMath::IsFinite(Input.MaximumFishTorque) || Input.MaximumFishTorque < 0.0
		|| !FMath::IsFinite(Input.MaximumAngularSpeedDegreesPerSecond) || Input.MaximumAngularSpeedDegreesPerSecond <= 0.0
		|| !FMath::IsFinite(Input.ResponseSeconds) || Input.ResponseSeconds <= 0.0
		|| !FMath::IsFinite(Input.FishPullSmoothingSeconds) || Input.FishPullSmoothingSeconds <= 0.0
		|| !FMath::IsFinite(Input.DeltaSeconds) || Input.DeltaSeconds < 0.0) return Result;

	FVector Direction = Input.CurrentAim.Vector();
	const FVector RequestedDirection = Input.RequestedAim.Vector();
	const FVector TargetFishPull = Input.PullAxis.GetSafeNormal() * Input.MaximumFishTorque;
	Result.SmoothedFishPullStrengthMeters = Input.PreviousSmoothedFishPullStrengthMeters;
	const double MaximumSpeed = FMath::DegreesToRadians(Input.MaximumAngularSpeedDegreesPerSecond);
	const double Response = FMath::Max(Input.ResponseSeconds, 1.0 / 240.0);
	const double FishPullResponse = FMath::Max(Input.FishPullSmoothingSeconds, 1.0 / 240.0);
	// 亚步积分让低帧率与高帧率看到相同的受力平衡；大卡顿只推进最多 0.25 秒，避免视觉猛甩。
	double RemainingSeconds = FMath::Min(Input.DeltaSeconds, 0.25);
	while (RemainingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double StepSeconds = FMath::Min(RemainingSeconds, FMath::Min(1.0 / 120.0, Response * 0.25));
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
		// 粘性阻尼：不再把阶跃负载直接变成角速度，也不积累会反复过冲的转动惯量。
		const FVector AngularVelocity = (Result.NetTorque * (MaximumSpeed / TorqueScale)).GetClampedToMaxSize(MaximumSpeed);
		const double Speed = AngularVelocity.Size();
		// 只观察本次真实积分里的主动转矩，不由竿尖位移反推猫做功。
		// 一米参考力臂与 Evaluate 中猫力量的转矩解释一致；没有主动转矩时两项都为零。
		const double CatEffortFraction = Input.CatTorqueCapacity > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(CatTorque.Size() / Input.CatTorqueCapacity, 0.0, 1.0) : 0.0;
		const double IntentArc = MaximumSpeed * CatEffortFraction * StepSeconds * 100.0;
		const double ActualArc = FMath::Max(0.0, FVector::DotProduct(AngularVelocity, CatAxis))
			* StepSeconds * 100.0;
		Result.CatIntentArcCentimeters += IntentArc;
		Result.CatActualArcCentimeters += FMath::Min(IntentArc, ActualArc);
		Result.IntegratedSeconds += StepSeconds;
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
