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

bool UCatFightPersonalityDefinition::IsRuntimeDefinitionReady() const
{
	const auto ValidRange = [](const FVector2D& Range)
	{
		return FMath::IsFinite(Range.X) && FMath::IsFinite(Range.Y) && Range.X > 0.0 && Range.Y >= Range.X;
	};
	return !FightPersonalityId.IsNone() && ValidRange(CalmDurationRangeSeconds) && ValidRange(StruggleDurationRangeSeconds)
		&& FMath::IsFinite(CalmMovementSpeedCentimetersPerSecond) && CalmMovementSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(StruggleMovementSpeedCentimetersPerSecond) && StruggleMovementSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(BaseDrainMultiplier) && BaseDrainMultiplier > 0.0
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier > BaseDrainMultiplier
		&& ValidRange(DirectionRetargetDurationRangeSeconds)
		&& FMath::IsFinite(MaximumTurnRateDegreesPerSecond) && MaximumTurnRateDegreesPerSecond > 0.0
		&& FMath::IsFinite(StruggleOutwardDirectionBias) && StruggleOutwardDirectionBias >= 0.0
		&& StruggleOutwardDirectionBias <= 1.0
		&& FMath::IsFinite(CalmInwardDirectionBias) && CalmInwardDirectionBias >= 0.0
		&& CalmInwardDirectionBias <= 1.0
		&& FMath::IsFinite(LateralMovementBias) && LateralMovementBias >= 0.0 && LateralMovementBias <= 1.0
		&& FMath::IsFinite(FeintProbability) && FeintProbability >= 0.0 && FeintProbability <= 1.0
		&& FMath::IsFinite(FullStaminaInwardProbability)
		&& FullStaminaInwardProbability >= 0.0 && FullStaminaInwardProbability <= 1.0
		&& FMath::IsFinite(ExhaustedInwardProbability)
		&& ExhaustedInwardProbability >= FullStaminaInwardProbability
		&& ExhaustedInwardProbability <= 1.0
		&& FMath::IsFinite(InwardProbabilityExponent)
		&& InwardProbabilityExponent >= 0.1 && InwardProbabilityExponent <= 4.0
		&& FMath::IsFinite(InwardConeHalfAngleDegrees)
		&& InwardConeHalfAngleDegrees >= 1.0 && InwardConeHalfAngleDegrees <= 89.0
		&& FMath::IsFinite(StrongConfrontationAlignmentThreshold)
		&& StrongConfrontationAlignmentThreshold > 0.0 && StrongConfrontationAlignmentThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationConfirmationSeconds)
		&& StrongConfrontationConfirmationSeconds >= 0.0 && StrongConfrontationConfirmationSeconds <= 2.0
		&& FMath::IsFinite(AngleStrengthExponent) && AngleStrengthExponent >= 0.1 && AngleStrengthExponent <= 4.0;
}
