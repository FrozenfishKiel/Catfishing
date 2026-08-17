#include "Fishing/Simulation/CatFishingFightSimulator.h"

namespace
{
	bool IsFiniteFightVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
}

bool FCatFightSimulationConfig::IsValid() const
{
	return FMath::IsFinite(FixedStepSeconds) && FixedStepSeconds > 0.0
		&& FMath::IsFinite(BaseDrainPerSecond) && BaseDrainPerSecond > 0.0
		&& FMath::IsFinite(BaseDrainMultiplier) && BaseDrainMultiplier > 0.0
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier > 1.0
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(FishCalmSpeedCentimetersPerSecond) && FishCalmSpeedCentimetersPerSecond >= 0.0
		&& FMath::IsFinite(FishStruggleSpeedCentimetersPerSecond) && FishStruggleSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0
		&& FMath::IsFinite(RodWearPerTensionSecond) && RodWearPerTensionSecond >= 0.0
		&& FMath::IsFinite(RodDurability) && RodDurability > 0.0
		&& FMath::IsFinite(EscapeSlackCentimeters) && EscapeSlackCentimeters >= 0.0
		&& FMath::IsFinite(NearShoreLineLengthCentimeters) && NearShoreLineLengthCentimeters >= 0.0;
}

FCatFightStepResult FCatFishingFightSimulator::Step(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FVector& RodTipWorldPosition, const FVector& OutwardDirection)
{
	FCatFightStepResult Result;
	if (!Config.IsValid() || !FMath::IsFinite(State.CatStamina) || State.CatStamina < 0.0
		|| !FMath::IsFinite(State.FishStamina) || State.FishStamina < 0.0
		|| !FMath::IsFinite(State.LineLengthCentimeters) || State.LineLengthCentimeters < 0.0
		|| !FMath::IsFinite(State.AbsoluteRodWear) || State.AbsoluteRodWear < 0.0
		|| !IsFiniteFightVector(State.FishWorldPosition) || !IsFiniteFightVector(RodTipWorldPosition)
		|| !IsFiniteFightVector(OutwardDirection))
	{
		return Result;
	}

	const FVector SafeOutward = OutwardDirection.GetSafeNormal();
	const bool bStrugglingOutward = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward;
	const double DrainMultiplier = bStrugglingOutward
		? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
	if (State.bReeling)
	{
		const double Drain = Config.BaseDrainPerSecond * DrainMultiplier * Config.FixedStepSeconds;
		Result.CatStaminaDrain = FMath::Min(State.CatStamina, Drain);
		Result.FishStaminaDrain = FMath::Min(State.FishStamina, Drain);
	}

	Result.LineLengthCentimeters = FMath::Clamp(State.LineLengthCentimeters
		- (State.bReeling ? Config.ReelSpeedCentimetersPerSecond * Config.FixedStepSeconds : 0.0),
		0.0, Config.MaximumLineLengthCentimeters);
	const double FishSpeed = bStrugglingOutward
		? Config.FishStruggleSpeedCentimetersPerSecond : -Config.FishCalmSpeedCentimetersPerSecond;
	Result.ProposedFishWorldPosition = State.FishWorldPosition + SafeOutward * FishSpeed * Config.FixedStepSeconds;
	const double DistanceToRod = FVector::Distance(RodTipWorldPosition, Result.ProposedFishWorldPosition);
	Result.TensionCentimeters = FMath::Max(0.0, DistanceToRod - Result.LineLengthCentimeters);
	Result.AbsoluteRodWear = State.AbsoluteRodWear
		+ Result.TensionCentimeters * Config.RodWearPerTensionSecond * Config.FixedStepSeconds;

	const double CatRemaining = State.CatStamina - Result.CatStaminaDrain;
	const double FishRemaining = State.FishStamina - Result.FishStaminaDrain;
	if (Result.AbsoluteRodWear >= Config.RodDurability)
	{
		Result.Outcome = ECatFightStepOutcome::RodBroken;
	}
	else if (CatRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		Result.Outcome = ECatFightStepOutcome::CatStaminaExhausted;
	}
	else if (FishRemaining <= UE_DOUBLE_SMALL_NUMBER)
	{
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (DistanceToRod > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}
	else if (Result.LineLengthCentimeters <= Config.NearShoreLineLengthCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::NearShore;
	}

	Result.bSucceeded = true;
	return Result;
}
