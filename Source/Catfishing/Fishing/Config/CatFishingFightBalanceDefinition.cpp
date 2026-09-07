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
		&& FMath::IsFinite(ForcePerStrengthNewtons) && ForcePerStrengthNewtons > 0.0
		&& FMath::IsFinite(CatBodyMassKilograms) && CatBodyMassKilograms > 0.0
		&& FMath::IsFinite(ExhaustedReelForceNewtons) && ExhaustedReelForceNewtons > 0.0
		&& IsFishingFightBalanceFiniteNonNegative(ExhaustedCatTowAccelerationCentimetersPerSecondSquared)
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& IsFishingFightBalanceFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
		&& IsFishingFightBalanceFiniteNonNegative(CatRodStaminaCostPerStrengthRadian)
		&& IsFishingFightBalanceFiniteNonNegative(CatUnloadedWorkMultiplier)
		&& IsFishingFightBalanceFiniteNonNegative(CatSupportStaminaPerSecond)
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
		&& FMath::IsFinite(DisplayTensionNewtons) && DisplayTensionNewtons > 0.0
		&& IsFishingFightBalanceFiniteNonNegative(EscapeSlackCentimeters)
		&& IsFishingFightBalanceFiniteNonNegative(StalemateRodWearPerFishStrength)
		&& FMath::IsFinite(HeldRodMinimumLeverageMultiplier)
		&& HeldRodMinimumLeverageMultiplier > 0.0 && HeldRodMinimumLeverageMultiplier <= 1.0
		&& FMath::IsFinite(MaximumFishConstraintCorrectionSpeedCentimetersPerSecond)
		&& MaximumFishConstraintCorrectionSpeedCentimetersPerSecond > 0.0;
}
