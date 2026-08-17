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
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier >= 1.0;
}
