#include "Fishing/Config/CatFishingFightBalanceDefinition.h"

namespace
{
	bool IsFishingFightBalanceFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}
}

bool UCatFishingFightBalanceDefinition::IsRuntimeDefinitionReady() const
{
	return bEnableRuntimeDefinition && !BalanceDefinitionId.IsNone()
		&& FMath::IsFinite(StrengthPerKilogram) && StrengthPerKilogram > 0.0
		&& FMath::IsFinite(AccelerationPerStrength) && AccelerationPerStrength > 0.0
		&& FMath::IsFinite(DriveResponseSeconds) && DriveResponseSeconds > 0.0
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& IsFishingFightBalanceFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
		&& FMath::IsFinite(ExhaustedCatEscapeSpeedMultiplier) && ExhaustedCatEscapeSpeedMultiplier >= 1.0
		&& IsFishingFightBalanceFiniteNonNegative(FishStaminaCostPerStrengthCentimeter)
		&& IsFishingFightBalanceFiniteNonNegative(CatMovementStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(CatReelStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(CatRodStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(CatHoldStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(CatLoadStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(FishLoadStaminaMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(IsometricEffortMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(SlackStaminaRegenPerSecond)
		&& FMath::IsFinite(FishExhaustionThreshold)
		&& FishExhaustionThreshold >= 0.0 && FishExhaustionThreshold <= 1.0
		&& FMath::IsFinite(LowStaminaRestThreshold)
		&& LowStaminaRestThreshold >= 0.0 && LowStaminaRestThreshold <= 1.0
		&& FMath::IsFinite(LowStaminaRestMultiplier) && LowStaminaRestMultiplier >= 1.0
		&& FMath::IsFinite(TensionResponseRangeCentimeters) && TensionResponseRangeCentimeters > 0.0
		&& IsFishingFightBalanceFiniteNonNegative(EscapeSlackCentimeters)
		&& IsFishingFightBalanceFiniteNonNegative(StalemateRodWearPerFishStrength)
		&& FMath::IsFinite(HeldRodMinimumLeverageMultiplier)
		&& HeldRodMinimumLeverageMultiplier > 0.0 && HeldRodMinimumLeverageMultiplier <= 1.0
		&& FMath::IsFinite(MaximumFishConstraintCorrectionSpeedCentimetersPerSecond)
		&& MaximumFishConstraintCorrectionSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(MinimumCarrierAwaySpeedMultiplier)
		&& MinimumCarrierAwaySpeedMultiplier >= 0.0 && MinimumCarrierAwaySpeedMultiplier <= 1.0;
}
