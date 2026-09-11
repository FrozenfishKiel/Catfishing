#include "Data/CatFishPersonalityDefinition.h"

bool UCatBitePersonalityDefinition::IsRuntimeDefinitionReady() const
{
	return !BitePersonalityId.IsNone() && FMath::IsFinite(ProbeDurationSeconds) && ProbeDurationSeconds > 0.0
		&& FMath::IsFinite(TrueBiteWindowSeconds) && TrueBiteWindowSeconds > 0.0
		&& FMath::IsFinite(PerfectHookWindowSeconds) && PerfectHookWindowSeconds > 0.0
		&& PerfectHookWindowSeconds <= TrueBiteWindowSeconds
		&& FMath::IsFinite(PerfectFishStrengthMultiplier) && PerfectFishStrengthMultiplier > 0.0 && PerfectFishStrengthMultiplier <= 1.0
		&& FMath::IsFinite(PerfectFishStaminaMultiplier) && PerfectFishStaminaMultiplier > 0.0 && PerfectFishStaminaMultiplier <= 1.0
		&& FMath::IsFinite(PerfectInitialLineLengthMultiplier) && PerfectInitialLineLengthMultiplier > 0.0
		&& PerfectInitialLineLengthMultiplier <= 1.0;
}

void UCatFightPersonalityDefinition::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyMotionSettings();
}

bool UCatFightPersonalityDefinition::MigrateLegacyMotionSettings()
{
	// 只迁移旧序列化格式；新版本里的非法配置不能回退旧值。
	if (AdaptiveMotionVersion != 0) return false;
	if (FullEffortMovementSpeedCentimetersPerSecond > 0.0)
	{
		AdaptiveMotionVersion = 1;
		return true;
	}
	const double LegacySpeed = FMath::Max(CalmMovementSpeedCentimetersPerSecond, StruggleMovementSpeedCentimetersPerSecond);
	if (!FMath::IsFinite(LegacySpeed) || LegacySpeed <= 0.0) return false;
	FullEffortMovementSpeedCentimetersPerSecond = LegacySpeed;
	AdaptiveSteeringConfig.OutwardDurationRangeSeconds = StruggleDurationRangeSeconds;
	AdaptiveSteeringConfig.EaseOffDurationRangeSeconds = CalmDurationRangeSeconds;
	AdaptiveSteeringConfig.RetargetDurationRangeSeconds = DirectionRetargetDurationRangeSeconds;
	AdaptiveSteeringConfig.MaximumTurnRateDegreesPerSecond = MaximumTurnRateDegreesPerSecond;
	// 旧扇区的几何展开仍使用角度单位；只迁移外冲分散程度，旧随机内游概率退出。
	AdaptiveSteeringConfig.OutwardAngularSpreadDegrees = (180.0 - InwardConeHalfAngleDegrees)
		* FMath::Lerp(0.2, 1.0, LateralMovementBias)
		* FMath::Lerp(1.0, 0.3, StruggleOutwardDirectionBias);
	AdaptiveMotionVersion = 1;
	return true;
}

bool UCatFightPersonalityDefinition::IsRuntimeDefinitionReady() const
{
	return !FightPersonalityId.IsNone() && AdaptiveSteeringConfig.IsValid()
		&& FMath::IsFinite(FullEffortMovementSpeedCentimetersPerSecond)
		&& FullEffortMovementSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(StrongConfrontationAlignmentThreshold)
		&& StrongConfrontationAlignmentThreshold > 0.0 && StrongConfrontationAlignmentThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationConfirmationSeconds)
		&& StrongConfrontationConfirmationSeconds >= 0.0 && StrongConfrontationConfirmationSeconds <= 2.0
		&& FMath::IsFinite(AngleStrengthExponent) && AngleStrengthExponent >= 0.1 && AngleStrengthExponent <= 4.0;
}
