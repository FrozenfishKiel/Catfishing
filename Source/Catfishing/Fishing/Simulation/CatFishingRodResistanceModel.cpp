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
