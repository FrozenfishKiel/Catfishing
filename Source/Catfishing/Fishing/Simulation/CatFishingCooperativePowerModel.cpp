#include "Fishing/Simulation/CatFishingCooperativePowerModel.h"

bool FCatFightPowerTuning::IsValid() const
{
	return FMath::IsFinite(ChargeSeconds) && ChargeSeconds > 0.0
		&& FMath::IsFinite(DecaySeconds) && DecaySeconds > 0.0
		&& FMath::IsFinite(HelperMaximumPowerAlpha)
		&& HelperMaximumPowerAlpha > 0.0 && HelperMaximumPowerAlpha <= 1.0
		&& FMath::IsFinite(PrimaryStaminaDrainPerSecondAtFullPower)
		&& PrimaryStaminaDrainPerSecondAtFullPower >= 0.0
		&& FMath::IsFinite(HelperStaminaDrainMultiplier) && HelperStaminaDrainMultiplier >= 1.0
		&& FMath::IsFinite(DisruptionPrimaryDrainShare)
		&& DisruptionPrimaryDrainShare >= 0.0 && DisruptionPrimaryDrainShare <= 1.0;
}

FCatFightPowerStepResult FCatFishingCooperativePowerModel::StepParticipant(
	const FCatFightPowerTuning& Tuning, const double FixedStepSeconds,
	const double CurrentPowerAlpha, const bool bPullHeld, const bool bPrimary,
	const double BaseFishingStrength)
{
	FCatFightPowerStepResult Result;
	if (!Tuning.IsValid() || !FMath::IsFinite(FixedStepSeconds) || FixedStepSeconds <= 0.0
		|| !FMath::IsFinite(CurrentPowerAlpha) || CurrentPowerAlpha < 0.0
		|| !FMath::IsFinite(BaseFishingStrength) || BaseFishingStrength <= 0.0)
	{
		return Result;
	}

	const double MaximumPowerAlpha = bPrimary ? 1.0 : Tuning.HelperMaximumPowerAlpha;
	const double ClampedCurrent = FMath::Clamp(CurrentPowerAlpha, 0.0, MaximumPowerAlpha);
	const double Delta = FixedStepSeconds / (bPullHeld ? Tuning.ChargeSeconds : Tuning.DecaySeconds);
	Result.PowerAlpha = FMath::Clamp(ClampedCurrent + (bPullHeld ? Delta : -Delta),
		0.0, MaximumPowerAlpha);
	Result.StrengthContribution = BaseFishingStrength * Result.PowerAlpha;
	Result.StaminaDrainPerSecond = Tuning.PrimaryStaminaDrainPerSecondAtFullPower
		* Result.PowerAlpha * (bPrimary ? 1.0 : Tuning.HelperStaminaDrainMultiplier);
	Result.bSucceeded = true;
	return Result;
}

double FCatFishingCooperativePowerModel::ComputePrimaryDisruptionDrainPerSecond(
	const FCatFightPowerTuning& Tuning, const double PrimaryPowerAlpha,
	const double CombinedHelperDrainPerSecond)
{
	if (!Tuning.IsValid() || !FMath::IsFinite(PrimaryPowerAlpha)
		|| !FMath::IsFinite(CombinedHelperDrainPerSecond) || CombinedHelperDrainPerSecond <= 0.0
		|| PrimaryPowerAlpha > UE_DOUBLE_SMALL_NUMBER)
	{
		return 0.0;
	}
	return CombinedHelperDrainPerSecond * Tuning.DisruptionPrimaryDrainShare;
}
