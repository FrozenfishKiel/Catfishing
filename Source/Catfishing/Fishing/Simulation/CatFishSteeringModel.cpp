#include "Fishing/Simulation/CatFishSteeringModel.h"

namespace
{
	bool IsFiniteDirection(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsUnitInterval(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0 && Value <= 1.0;
	}

	bool IsPositiveRange(const FVector2D& Range)
	{
		return FMath::IsFinite(Range.X) && FMath::IsFinite(Range.Y) && Range.X > 0.0 && Range.Y >= Range.X;
	}

	bool IsEffortRange(const FVector2D& Range)
	{
		return IsUnitInterval(Range.X) && IsUnitInterval(Range.Y) && Range.Y >= Range.X;
	}

	bool IsLiveBehavior(const ECatFishBehavior Behavior)
	{
		return Behavior == ECatFishBehavior::OutwardRush || Behavior == ECatFishBehavior::LateralArc
			|| Behavior == ECatFishBehavior::EaseOff;
	}

	FVector FlattenDirection(const FVector& Value, const FVector& Fallback)
	{
		return FVector(Value.X, Value.Y, 0.0).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER,
			FVector(Fallback.X, Fallback.Y, 0.0).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector));
	}

	FVector TurnToward2D(const FVector& Current, const FVector& Target, const double MaximumDegrees)
	{
		const FVector SafeCurrent = FlattenDirection(Current, Target);
		const FVector SafeTarget = FlattenDirection(Target, SafeCurrent);
		const double CurrentYaw = FMath::Atan2(SafeCurrent.Y, SafeCurrent.X);
		const double TargetYaw = FMath::Atan2(SafeTarget.Y, SafeTarget.X);
		const double MaximumRadians = FMath::DegreesToRadians(FMath::Max(0.0, MaximumDegrees));
		const double NewYaw = CurrentYaw + FMath::Clamp(FMath::FindDeltaAngleRadians(CurrentYaw, TargetYaw),
			-MaximumRadians, MaximumRadians);
		return FVector(FMath::Cos(NewYaw), FMath::Sin(NewYaw), 0.0);
	}

	FVector ConstrainTargetToWater(const FVector& Target, const FCatFishSteeringState& State)
	{
		if (State.BoundaryAvoidanceSecondsRemaining <= 0.0) return Target;
		const FVector Waterward = State.BoundaryWaterwardDirection;
		return FlattenDirection(Target + Waterward * FMath::Max(0.0,
			0.2 - FVector::DotProduct(Target, Waterward)), Waterward);
	}
}

bool FCatFishSteeringConfig::IsValid() const
{
	return IsPositiveRange(RetargetDurationRangeSeconds)
		&& FMath::IsFinite(MaximumTurnRateDegreesPerSecond) && MaximumTurnRateDegreesPerSecond > 0.0
		&& FMath::IsFinite(OutwardAngularSpreadDegrees) && OutwardAngularSpreadDegrees >= 0.0
		&& OutwardAngularSpreadDegrees <= 75.0 && IsUnitInterval(LateralOutwardBias)
		&& IsUnitInterval(EaseOffInwardBias) && IsEffortRange(OutwardEffortRange)
		&& IsEffortRange(LateralEffortRange) && IsEffortRange(EaseOffEffortRange)
		&& FMath::IsFinite(EffortRisePerSecond) && EffortRisePerSecond > 0.0
		&& FMath::IsFinite(EffortFallPerSecond) && EffortFallPerSecond > 0.0
		&& IsPositiveRange(OutwardDurationRangeSeconds) && IsPositiveRange(LateralDurationRangeSeconds)
		&& IsPositiveRange(EaseOffDurationRangeSeconds)
		&& FMath::IsFinite(MinimumBehaviorDurationSeconds) && MinimumBehaviorDurationSeconds >= 0.0
		&& IsUnitInterval(LowStaminaRatio)
		&& FMath::IsFinite(LowStaminaActiveDurationMultiplier) && LowStaminaActiveDurationMultiplier > 0.0
		&& LowStaminaActiveDurationMultiplier <= 1.0
		&& FMath::IsFinite(LowStaminaEaseOffDurationMultiplier) && LowStaminaEaseOffDurationMultiplier >= 1.0
		&& IsUnitInterval(BlockedLoadThreshold) && IsUnitInterval(BlockedProgressFraction)
		&& FMath::IsFinite(BlockedConfirmationSeconds) && BlockedConfirmationSeconds >= 0.0
		&& FMath::IsFinite(LoadSmoothingSeconds) && LoadSmoothingSeconds >= 0.0;
}

bool FCatFishSteeringModel::Initialize(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, const ECatFishBehavior Behavior, const double FishStaminaRatio,
	FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	if (!Config.IsValid() || !IsFiniteDirection(LineOutwardDirection) || LineOutwardDirection.IsNearlyZero()
		|| !IsUnitInterval(FishStaminaRatio) || !IsLiveBehavior(Behavior)) return false;
	InOutState = FCatFishSteeringState{};
	InOutState.CurrentDirection = FlattenDirection(LineOutwardDirection, FVector::ForwardVector);
	InOutState.TargetDirection = InOutState.CurrentDirection;
	InOutState.bInitialized = true;
	return BeginBehavior(Config, LineOutwardDirection, Behavior, FishStaminaRatio, Random, InOutState);
}

bool FCatFishSteeringModel::BeginBehavior(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, const ECatFishBehavior Behavior, const double FishStaminaRatio,
	FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	if (!Config.IsValid() || !IsFiniteDirection(LineOutwardDirection) || LineOutwardDirection.IsNearlyZero()
		|| !IsUnitInterval(FishStaminaRatio) || !IsLiveBehavior(Behavior)) return false;
	if (!InOutState.bInitialized)
		return Initialize(Config, LineOutwardDirection, Behavior, FishStaminaRatio, Random, InOutState);
	if (!IsUnitInterval(InOutState.CurrentEffortRatio) || !IsFiniteDirection(InOutState.CurrentDirection)) return false;

	const FVector2D& EffortRange = Behavior == ECatFishBehavior::OutwardRush ? Config.OutwardEffortRange
		: Behavior == ECatFishBehavior::LateralArc ? Config.LateralEffortRange : Config.EaseOffEffortRange;
	const FVector2D& DurationRange = Behavior == ECatFishBehavior::OutwardRush ? Config.OutwardDurationRangeSeconds
		: Behavior == ECatFishBehavior::LateralArc ? Config.LateralDurationRangeSeconds : Config.EaseOffDurationRangeSeconds;
	const double DurationMultiplier = FishStaminaRatio <= Config.LowStaminaRatio
		? (Behavior == ECatFishBehavior::EaseOff ? Config.LowStaminaEaseOffDurationMultiplier
			: Config.LowStaminaActiveDurationMultiplier) : 1.0;

	// 随机只在树真正进入行为时冻结目标出力/时长；不从模型选择下一状态。
	InOutState.Behavior = Behavior;
	InOutState.TargetEffortRatio = Random.FRandRange(EffortRange.X, EffortRange.Y);
	InOutState.BehaviorDurationSeconds = FMath::Max(Config.MinimumBehaviorDurationSeconds,
		Random.FRandRange(DurationRange.X, DurationRange.Y) * DurationMultiplier);
	InOutState.BehaviorElapsedSeconds = 0.0;
	InOutState.BlockedSeconds = 0.0;
	InOutState.FishStaminaRatio = FishStaminaRatio;

	if (Behavior == ECatFishBehavior::LateralArc)
	{
		const FVector Outward = FlattenDirection(LineOutwardDirection, InOutState.CurrentDirection);
		const FVector Tangent(-Outward.Y, Outward.X, 0.0);
		const double CurrentLateral = FVector::DotProduct(InOutState.CurrentDirection, Tangent);
		InOutState.LateralSign = FMath::Abs(CurrentLateral) > 0.15
			? FMath::Sign(CurrentLateral) : (Random.RandBool() ? 1.0 : -1.0);
	}
	InOutState.DirectionOffsetDegrees = Random.FRandRange(-Config.OutwardAngularSpreadDegrees,
		Config.OutwardAngularSpreadDegrees);
	InOutState.RetargetSecondsRemaining = Random.FRandRange(
		Config.RetargetDurationRangeSeconds.X, Config.RetargetDurationRangeSeconds.Y);
	UpdateTargetDirection(Config, LineOutwardDirection, InOutState);
	return true;
}

bool FCatFishSteeringModel::AdvanceFeedback(const FCatFishSteeringConfig& Config,
	const FCatFishBehaviorFeedback& Feedback, const double DeltaSeconds, FCatFishSteeringState& InOutState)
{
	if (!Config.IsValid() || !InOutState.bInitialized || !IsLiveBehavior(InOutState.Behavior)
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0
		|| !FMath::IsFinite(Feedback.NormalizedLineLoad) || Feedback.NormalizedLineLoad < 0.0
		|| !IsFiniteDirection(Feedback.ActualFishVelocityCentimetersPerSecond)
		|| !IsFiniteDirection(Feedback.ActiveSwimDirection) || Feedback.ActiveSwimDirection.IsNearlyZero()
		|| !FMath::IsFinite(Feedback.ExpectedFreeSpeedCentimetersPerSecond)
		|| Feedback.ExpectedFreeSpeedCentimetersPerSecond < 0.0 || !IsUnitInterval(Feedback.FishStaminaRatio)
		|| !FMath::IsFinite(InOutState.BehaviorElapsedSeconds) || !FMath::IsFinite(InOutState.BlockedSeconds)
		|| !FMath::IsFinite(InOutState.SmoothedLineLoad)) return false;

	InOutState.BehaviorElapsedSeconds += DeltaSeconds;
	InOutState.FishStaminaRatio = Feedback.FishStaminaRatio;
	const double Alpha = Config.LoadSmoothingSeconds <= 0.0 ? 1.0 : 1.0 - FMath::Exp(-DeltaSeconds / Config.LoadSmoothingSeconds);
	InOutState.SmoothedLineLoad = FMath::Lerp(InOutState.SmoothedLineLoad,
		FMath::Clamp(Feedback.NormalizedLineLoad, 0.0, 1.0), Alpha);
	const double ActiveProgressSpeed = FVector::DotProduct(Feedback.ActualFishVelocityCentimetersPerSecond,
		FlattenDirection(Feedback.ActiveSwimDirection, InOutState.CurrentDirection));
	// 先确认真实承载，再比较主动方向上的进展；乘法阈值避免低出力归一化被放大。
	const bool bBlocked = Feedback.bLineTaut && InOutState.SmoothedLineLoad >= Config.BlockedLoadThreshold
		&& Feedback.ExpectedFreeSpeedCentimetersPerSecond > UE_DOUBLE_KINDA_SMALL_NUMBER
		&& ActiveProgressSpeed < Feedback.ExpectedFreeSpeedCentimetersPerSecond * Config.BlockedProgressFraction;
	InOutState.BlockedSeconds = bBlocked ? InOutState.BlockedSeconds + DeltaSeconds : 0.0;
	return true;
}

bool FCatFishSteeringModel::TestCondition(const FCatFishSteeringConfig& Config,
	const FCatFishSteeringState& State, const ECatFishBehaviorCondition Condition)
{
	if (!Config.IsValid() || !State.bInitialized || !IsLiveBehavior(State.Behavior)) return false;
	switch (Condition)
	{
	case ECatFishBehaviorCondition::MinimumDurationElapsed:
		return State.BehaviorElapsedSeconds + UE_DOUBLE_SMALL_NUMBER >= Config.MinimumBehaviorDurationSeconds;
	case ECatFishBehaviorCondition::DurationExpired:
		return State.BehaviorElapsedSeconds + UE_DOUBLE_SMALL_NUMBER >= State.BehaviorDurationSeconds;
	case ECatFishBehaviorCondition::SustainedBlocked:
		return State.BlockedSeconds > 0.0
			&& State.BlockedSeconds + UE_DOUBLE_SMALL_NUMBER >= Config.BlockedConfirmationSeconds;
	case ECatFishBehaviorCondition::LowStamina:
		return State.FishStaminaRatio <= Config.LowStaminaRatio;
	default:
		return false;
	}
}

void FCatFishSteeringModel::UpdateTargetDirection(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, FCatFishSteeringState& InOutState)
{
	const FVector Outward = FlattenDirection(LineOutwardDirection, InOutState.CurrentDirection);
	const FVector Tangent = FVector(-Outward.Y, Outward.X, 0.0) * InOutState.LateralSign;
	FVector Target = Outward;
	switch (InOutState.Behavior)
	{
	case ECatFishBehavior::OutwardRush:
		Target = Outward.RotateAngleAxis(InOutState.DirectionOffsetDegrees, FVector::UpVector);
		break;
	case ECatFishBehavior::LateralArc:
		Target = FlattenDirection(Tangent + Outward * Config.LateralOutwardBias, Tangent);
		break;
	case ECatFishBehavior::EaseOff:
		Target = FlattenDirection(Tangent * (1.0 - Config.EaseOffInwardBias)
			- Outward * Config.EaseOffInwardBias, Tangent);
		break;
	default:
		break;
	}
	InOutState.TargetDirection = ConstrainTargetToWater(Target, InOutState);
}

bool FCatFishSteeringModel::Step(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
	const double DeltaSeconds, FRandomStream& Random, FCatFishSteeringState& InOutState,
	FVector& OutDesiredDirection, const bool bForceOutward)
{
	OutDesiredDirection = FVector::ZeroVector;
	if (!Config.IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0
		|| !IsFiniteDirection(LineOutwardDirection) || LineOutwardDirection.IsNearlyZero()) return false;
	if (!InOutState.bInitialized)
	{
		if (!bForceOutward) return false;
		// 强拖可以从未初始化夹具启动，也不抽取一次普通策略随机数。
		InOutState = FCatFishSteeringState{};
		InOutState.CurrentDirection = FlattenDirection(LineOutwardDirection, FVector::ForwardVector);
		InOutState.bInitialized = true;
	}
	if (!IsFiniteDirection(InOutState.CurrentDirection) || !IsFiniteDirection(InOutState.TargetDirection)
		|| !IsUnitInterval(InOutState.CurrentEffortRatio) || !IsUnitInterval(InOutState.TargetEffortRatio)
		|| !FMath::IsFinite(InOutState.RetargetSecondsRemaining)
		|| !FMath::IsFinite(InOutState.BoundaryAvoidanceSecondsRemaining)) return false;

	double DesiredEffort = InOutState.TargetEffortRatio;
	if (bForceOutward)
	{
		InOutState.TargetDirection = ConstrainTargetToWater(
			FlattenDirection(LineOutwardDirection, InOutState.CurrentDirection), InOutState);
		DesiredEffort = 1.0;
		// 强拖物理特例本步已按满力结算，执行记忆同步该事实；恢复普通策略后从1连续降回原目标。
		InOutState.CurrentEffortRatio = 1.0;
	}
	else
	{
		if (!IsLiveBehavior(InOutState.Behavior)) return false;
		InOutState.RetargetSecondsRemaining -= DeltaSeconds;
		if (InOutState.RetargetSecondsRemaining <= 0.0)
		{
			InOutState.DirectionOffsetDegrees = Random.FRandRange(-Config.OutwardAngularSpreadDegrees,
				Config.OutwardAngularSpreadDegrees);
			InOutState.RetargetSecondsRemaining = Random.FRandRange(
				Config.RetargetDurationRangeSeconds.X, Config.RetargetDurationRangeSeconds.Y);
		}
		// 弧线跟随实际线方向转动，但左右侧在整个命令中冻结；不会每个固定步抛硬币。
		UpdateTargetDirection(Config, LineOutwardDirection, InOutState);
	}
	const double EffortRate = DesiredEffort > InOutState.CurrentEffortRatio
		? Config.EffortRisePerSecond : Config.EffortFallPerSecond;
	InOutState.CurrentEffortRatio += FMath::Clamp(DesiredEffort - InOutState.CurrentEffortRatio,
		-EffortRate * DeltaSeconds, EffortRate * DeltaSeconds);
	InOutState.CurrentDirection = TurnToward2D(InOutState.CurrentDirection, InOutState.TargetDirection,
		Config.MaximumTurnRateDegreesPerSecond * DeltaSeconds);
	InOutState.BoundaryAvoidanceSecondsRemaining = FMath::Max(0.0,
		InOutState.BoundaryAvoidanceSecondsRemaining - DeltaSeconds);
	OutDesiredDirection = InOutState.CurrentDirection;
	return true;
}

bool FCatFishSteeringModel::RedirectFromWaterBoundary(const FCatFishSteeringConfig& Config,
	const FVector& WaterwardDirection, FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	if (!Config.IsValid() || !InOutState.bInitialized || !IsFiniteDirection(WaterwardDirection)
		|| WaterwardDirection.IsNearlyZero() || !IsFiniteDirection(InOutState.CurrentDirection)) return false;
	const FVector Waterward = FlattenDirection(WaterwardDirection, InOutState.CurrentDirection);
	InOutState.BoundaryWaterwardDirection = Waterward;
	InOutState.BoundaryAvoidanceSecondsRemaining = Config.RetargetDurationRangeSeconds.Y;
	const FVector Current = FlattenDirection(InOutState.CurrentDirection, Waterward);
	const FVector ShoreTangent(-Waterward.Y, Waterward.X, 0.0);
	const double ExistingLateral = FVector::DotProduct(Current, ShoreTangent);
	if (FVector::DotProduct(Current, Waterward) > 0.05)
	{
		InOutState.TargetDirection = ConstrainTargetToWater(InOutState.TargetDirection, InOutState);
	}
	else
	{
		// 岸线只修正目标，实际方向仍由同一个限角速度入口连续转向。
		const double LateralSign = FMath::Abs(ExistingLateral) > 0.05
			? FMath::Sign(ExistingLateral)
			: (FVector::DotProduct(InOutState.TargetDirection, ShoreTangent) >= 0.0 ? 1.0 : -1.0);
		InOutState.TargetDirection = FlattenDirection(Waterward * 0.5 + ShoreTangent * LateralSign,
			Waterward);
	}
	(void)Random; // 岸线反馈不得每帧消耗随机数或抖动左右侧。
	InOutState.RetargetSecondsRemaining = FMath::Max(InOutState.RetargetSecondsRemaining,
		Config.RetargetDurationRangeSeconds.Y);
	return FVector::DotProduct(InOutState.TargetDirection, Waterward) > 0.0;
}
