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

	FVector FlattenDirection(const FVector& Value, const FVector& Fallback)
	{
		const FVector Flat(Value.X, Value.Y, 0.0);
		const FVector FlatFallback(Fallback.X, Fallback.Y, 0.0);
		return Flat.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER,
			FlatFallback.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector));
	}

	FVector TurnToward2D(const FVector& Current, const FVector& Target, const double MaximumDegrees)
	{
		const FVector SafeCurrent = FlattenDirection(Current, Target);
		const FVector SafeTarget = FlattenDirection(Target, SafeCurrent);
		const double CurrentYaw = FMath::Atan2(SafeCurrent.Y, SafeCurrent.X);
		const double TargetYaw = FMath::Atan2(SafeTarget.Y, SafeTarget.X);
		const double MaximumRadians = FMath::DegreesToRadians(FMath::Max(0.0, MaximumDegrees));
		const double DeltaYaw = FMath::FindDeltaAngleRadians(CurrentYaw, TargetYaw);
		const double NewYaw = CurrentYaw + FMath::Clamp(DeltaYaw, -MaximumRadians, MaximumRadians);
		return FVector(FMath::Cos(NewYaw), FMath::Sin(NewYaw), 0.0);
	}

	FVector RotateAroundUp(const FVector& Direction, const double OffsetDegrees)
	{
		return FlattenDirection(Direction, FVector::ForwardVector)
			.RotateAngleAxis(OffsetDegrees, FVector::UpVector);
	}
}

bool FCatFishSteeringConfig::IsValid() const
{
	return FMath::IsFinite(RetargetDurationRangeSeconds.X) && RetargetDurationRangeSeconds.X > 0.0
		&& FMath::IsFinite(RetargetDurationRangeSeconds.Y)
		&& RetargetDurationRangeSeconds.Y >= RetargetDurationRangeSeconds.X
		&& FMath::IsFinite(MaximumTurnRateDegreesPerSecond) && MaximumTurnRateDegreesPerSecond > 0.0
		&& IsUnitInterval(StruggleOutwardBias) && IsUnitInterval(CalmInwardBias)
		&& IsUnitInterval(LateralMovementBias) && IsUnitInterval(FeintProbability)
		&& IsUnitInterval(FullStaminaInwardProbability)
		&& IsUnitInterval(ExhaustedInwardProbability)
		&& ExhaustedInwardProbability >= FullStaminaInwardProbability
		&& FMath::IsFinite(InwardProbabilityExponent)
		&& InwardProbabilityExponent >= 0.1 && InwardProbabilityExponent <= 4.0
		&& FMath::IsFinite(InwardConeHalfAngleDegrees)
		&& InwardConeHalfAngleDegrees >= 1.0 && InwardConeHalfAngleDegrees <= 89.0;
}

double FCatFishSteeringModel::ComputeInwardProbability(const FCatFishSteeringConfig& Config,
	const double FishStaminaRatio)
{
	if (!Config.IsValid() || !FMath::IsFinite(FishStaminaRatio))
	{
		return 0.0;
	}
	const double ExhaustionAlpha = FMath::Pow(1.0 - FMath::Clamp(FishStaminaRatio, 0.0, 1.0),
		Config.InwardProbabilityExponent);
	return FMath::Lerp(Config.FullStaminaInwardProbability,
		Config.ExhaustedInwardProbability, ExhaustionAlpha);
}

bool FCatFishSteeringModel::Initialize(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, const ECatFishMotionIntent MotionIntent,
	const double FishStaminaRatio, FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	if (!Config.IsValid() || !IsFiniteDirection(LineOutwardDirection)
		|| !FMath::IsFinite(FishStaminaRatio)
		|| LineOutwardDirection.IsNearlyZero() || MotionIntent == ECatFishMotionIntent::None
		|| MotionIntent == ECatFishMotionIntent::AutoHauling)
	{
		return false;
	}
	InOutState = FCatFishSteeringState{};
	InOutState.CurrentDirection = FlattenDirection(LineOutwardDirection, FVector::ForwardVector);
	InOutState.LastMotionIntent = MotionIntent;
	InOutState.bInitialized = true;
	return SelectTarget(Config, LineOutwardDirection, MotionIntent, FishStaminaRatio, Random, InOutState);
}

bool FCatFishSteeringModel::SelectTarget(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, const ECatFishMotionIntent MotionIntent,
	const double FishStaminaRatio, FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	// [FishLogic 2/5：性格选方向]
	// Random 只抽“下一段的目标方向和持续时间”；当前位置绝不随机跳变。
	// StrugglingOutward 以鱼线向外为锚，CalmOrInward 以朝竿尖为锚，再混合随机方向、左右横切和假动作。
	if (!Config.IsValid() || !IsFiniteDirection(LineOutwardDirection) || LineOutwardDirection.IsNearlyZero()
		|| !FMath::IsFinite(FishStaminaRatio))
	{
		return false;
	}
	const FVector Outward = FlattenDirection(LineOutwardDirection, InOutState.CurrentDirection);
	const bool bStruggling = MotionIntent == ECatFishMotionIntent::StrugglingOutward;
	const double StaminaDrivenInwardProbability = ComputeInwardProbability(Config, FishStaminaRatio);
	// 发力状态仍以外冲为主，FeintProbability 只允许其中一小部分采用体力驱动的向内概率；
	// 平静状态则直接使用完整概率。这样“低体力更容易向内”不会把发力状态变成反向游。
	const double EffectiveInwardProbability = bStruggling
		? StaminaDrivenInwardProbability * Config.FeintProbability : StaminaDrivenInwardProbability;
	const bool bChooseInward = Random.FRand() < EffectiveInwardProbability;
	const FVector Anchor = bChooseInward ? -Outward : Outward;
	const double DirectionalBias = bChooseInward ? Config.CalmInwardBias : Config.StruggleOutwardBias;
	// 向内选择严格落在朝竿尖的 ±InwardConeHalfAngleDegrees；向外选择使用剩余扇区，
	// 因而不会被随机横切重新推回“向内”分类。Bias 越高、横向性越低，实际偏角越靠近锚方向。
	const double BaseHalfAngle = bChooseInward ? Config.InwardConeHalfAngleDegrees
		: 180.0 - Config.InwardConeHalfAngleDegrees;
	const double SpreadScale = FMath::Lerp(0.2, 1.0, Config.LateralMovementBias)
		* FMath::Lerp(1.0, 0.3, DirectionalBias);
	const double OffsetDegrees = Random.FRandRange(-BaseHalfAngle, BaseHalfAngle) * SpreadScale;
	const FVector Target = RotateAroundUp(Anchor, OffsetDegrees);

	InOutState.TargetDirection = Target;
	InOutState.RetargetSecondsRemaining = Random.FRandRange(
		Config.RetargetDurationRangeSeconds.X, Config.RetargetDurationRangeSeconds.Y);
	InOutState.LastMotionIntent = MotionIntent;
	return FMath::IsFinite(InOutState.RetargetSecondsRemaining)
		&& InOutState.RetargetSecondsRemaining > 0.0;
}

bool FCatFishSteeringModel::Step(const FCatFishSteeringConfig& Config,
	const FVector& LineOutwardDirection, const ECatFishMotionIntent MotionIntent,
	const double FishStaminaRatio, const double DeltaSeconds,
	FRandomStream& Random, FCatFishSteeringState& InOutState, FVector& OutDesiredDirection)
{
	// [FishLogic 2/5：平滑转向]
	// 目标到期/运动意图改变时才重新抽方向；其他固定步只按最大角速度转过去，所以轨迹连续而非白噪声抖动。
	OutDesiredDirection = FVector::ZeroVector;
	if (!Config.IsValid() || !FMath::IsFinite(FishStaminaRatio)
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0
		|| !IsFiniteDirection(LineOutwardDirection) || LineOutwardDirection.IsNearlyZero()
		|| MotionIntent == ECatFishMotionIntent::None || MotionIntent == ECatFishMotionIntent::AutoHauling)
	{
		return false;
	}
	if (!InOutState.bInitialized
		&& !Initialize(Config, LineOutwardDirection, MotionIntent, FishStaminaRatio, Random, InOutState))
	{
		return false;
	}

	InOutState.RetargetSecondsRemaining -= DeltaSeconds;
	if (InOutState.LastMotionIntent != MotionIntent || InOutState.RetargetSecondsRemaining <= 0.0)
	{
		if (!SelectTarget(Config, LineOutwardDirection, MotionIntent, FishStaminaRatio, Random, InOutState))
		{
			return false;
		}
	}
	InOutState.CurrentDirection = TurnToward2D(InOutState.CurrentDirection, InOutState.TargetDirection,
		Config.MaximumTurnRateDegreesPerSecond * DeltaSeconds);
	OutDesiredDirection = InOutState.CurrentDirection;
	return IsFiniteDirection(OutDesiredDirection) && !OutDesiredDirection.IsNearlyZero();
}

bool FCatFishSteeringModel::RedirectFromWaterBoundary(const FCatFishSteeringConfig& Config,
	const FVector& WaterwardDirection, FRandomStream& Random, FCatFishSteeringState& InOutState)
{
	// [FishLogic 2/5：活鱼撞岸反馈]
	// 岸线求解只保留真实入水与沿岸位移，不把鱼瞬移到最近岸点或抛竿内缩点。
	// 同时修正 Steering，避免下一固定步继续朝陆地游；岸外间隙也能逐步回到水域内。
	if (!Config.IsValid() || !InOutState.bInitialized || !IsFiniteDirection(WaterwardDirection)
		|| WaterwardDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector Waterward = FlattenDirection(WaterwardDirection, InOutState.CurrentDirection);
	const FVector Current = FlattenDirection(InOutState.CurrentDirection, Waterward);
	const double IntoWaterDot = FVector::DotProduct(Current, Waterward);
	const FVector ShoreTangent(-Waterward.Y, Waterward.X, 0.0);
	if (IntoWaterDot > 0.05)
	{
		// 玩家持续收线也可能让候选点越界；若鱼当前已经朝水里/沿岸游，就不重复抽切向，
		// 只保证目标不再指向陆地并延长本段，避免每个固定步重新选左右造成抖动。
		const double TargetWaterwardDot = FVector::DotProduct(InOutState.TargetDirection, Waterward);
		InOutState.TargetDirection = FlattenDirection(InOutState.TargetDirection
			+ Waterward * FMath::Max(0.0, 0.2 - TargetWaterwardDot), Current);
		InOutState.RetargetSecondsRemaining = FMath::Max(InOutState.RetargetSecondsRemaining,
			Config.RetargetDurationRangeSeconds.Y);
		return FVector::DotProduct(InOutState.TargetDirection, Waterward) > 0.0;
	}

	// Waterward 是岸线指向水里的法线。当前方向指向岸上时 dot<0，按法线镜面反射；
	// 再加入稳定的沿岸切向，让持续按住左键时鱼仍会沿岸逃窜，而不是被径向收线压在一个点上。
	const FVector Reflected = IntoWaterDot < 0.0
		? Current - 2.0 * IntoWaterDot * Waterward : Current;
	const double ExistingLateral = FVector::DotProduct(Current, ShoreTangent);
	const double LateralSign = FMath::Abs(ExistingLateral) > 0.05
		? FMath::Sign(ExistingLateral) : (Random.RandBool() ? 1.0 : -1.0);
	const FVector AlongShore = ShoreTangent * LateralSign;
	InOutState.CurrentDirection = FlattenDirection(Reflected + Waterward * 0.25 + AlongShore * 0.5,
		Waterward);
	InOutState.TargetDirection = FlattenDirection(Waterward * 0.5
		+ AlongShore * (0.75 + Config.LateralMovementBias * 0.5), InOutState.CurrentDirection);
	// 至少保持一个最长换向周期，不让 CalmOrInward 在下一帧立刻重新抽到朝岸方向。
	InOutState.RetargetSecondsRemaining = FMath::Max(InOutState.RetargetSecondsRemaining,
		Config.RetargetDurationRangeSeconds.Y);
	return FVector::DotProduct(InOutState.CurrentDirection, Waterward) > 0.0
		&& FVector::DotProduct(InOutState.TargetDirection, Waterward) > 0.0;
}
