#include "Fishing/Simulation/CatFishingGroupModel.h"

#include "Fishing/Simulation/CatFishingFightWorkModel.h"

namespace
{
	bool IsGroupFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}

	bool IsGroupFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FVector MakeGroupGroundIntent(const FVector& Value)
	{
		return FVector(Value.X, Value.Y, 0.0).GetClampedToMaxSize(1.0);
	}

	bool IsValidGroupParticipant(const FCatFightGroupParticipantInput& Participant)
	{
		return IsGroupFiniteNonNegative(Participant.FishingStrength)
			&& IsGroupFiniteNonNegative(Participant.CurrentStamina)
			&& IsGroupFiniteNonNegative(Participant.MaximumStamina)
			&& Participant.CurrentStamina <= Participant.MaximumStamina
			&& FMath::IsFinite(Participant.MassKilograms) && Participant.MassKilograms > 0.0
			&& IsGroupFiniteNonNegative(Participant.MaximumMoveSpeedCentimetersPerSecond)
			&& IsGroupFiniteVector(Participant.MoveIntentWorld);
	}
}

bool FCatFishingGroupModel::ComputeForces(const FCatFightGroupInput& Input, FCatFightGroupResult& OutResult)
{
	OutResult = {};
	if (!IsGroupFiniteNonNegative(Input.HelperStrengthMultiplier) || Input.HelperStrengthMultiplier > 1.0
		|| !IsGroupFiniteVector(Input.ResistanceDirectionWorld)) return false;
	FCatFightGroupResult Result;
	Result.Participants.Reserve(Input.Participants.Num());
	const FVector ResistanceDirection = FVector(Input.ResistanceDirectionWorld.X,
		Input.ResistanceDirectionWorld.Y, 0.0).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, -FVector::ForwardVector);
	FVector WeightedVelocity = FVector::ZeroVector;
	for (const FCatFightGroupParticipantInput& Participant : Input.Participants)
	{
		if (!IsValidGroupParticipant(Participant)) return false;
		FCatFightGroupParticipantResult Contribution;
		// Do not turn tiny positive stamina into exhaustion or reduce power by the stamina ratio.
		Contribution.ActiveStrength = Participant.CurrentStamina > 0.0
			? Participant.FishingStrength * (Participant.bPrimary ? 1.0 : Input.HelperStrengthMultiplier) : 0.0;
		const FVector Intent = MakeGroupGroundIntent(Participant.MoveIntentWorld);
		Contribution.MovementIntentMagnitude = Intent.Size();
		Contribution.MovementStrengthWorld = Intent * Contribution.ActiveStrength;
		Contribution.AppliedStrengthWorld = Contribution.MovementStrengthWorld
			+ ResistanceDirection * Contribution.ActiveStrength * (1.0 - Contribution.MovementIntentMagnitude);
		Contribution.SignedResistanceStrength = FVector::DotProduct(Contribution.AppliedStrengthWorld, ResistanceDirection);
		Result.TotalActiveStrength += Contribution.ActiveStrength;
		Result.ActiveParticipantCount += Contribution.ActiveStrength > 0.0 ? 1 : 0;
		Result.TotalCurrentStamina += Participant.CurrentStamina;
		Result.TotalMaximumStamina += Participant.MaximumStamina;
		// Exhausted members retain their physical mass and their own maximum stamina.
		Result.TotalMassKilograms += Participant.MassKilograms;
		Result.AppliedStrengthWorld += Contribution.AppliedStrengthWorld;
		Result.MovementStrengthWorld += Contribution.MovementStrengthWorld;
		Result.SignedResistanceStrength += Contribution.SignedResistanceStrength;
		WeightedVelocity += Intent * Participant.MaximumMoveSpeedCentimetersPerSecond * Contribution.ActiveStrength;
		Result.Participants.Add(Contribution);
	}
	if (!IsGroupFiniteNonNegative(Result.TotalActiveStrength) || !IsGroupFiniteNonNegative(Result.TotalCurrentStamina)
		|| !IsGroupFiniteNonNegative(Result.TotalMaximumStamina) || !IsGroupFiniteNonNegative(Result.TotalMassKilograms)
		|| !IsGroupFiniteVector(Result.AppliedStrengthWorld) || !IsGroupFiniteVector(Result.MovementStrengthWorld)
		|| !FMath::IsFinite(Result.SignedResistanceStrength) || !IsGroupFiniteVector(WeightedVelocity)) return false;
	if (Result.TotalActiveStrength > 0.0)
	{
		Result.DesiredVelocityCentimetersPerSecond = WeightedVelocity / Result.TotalActiveStrength;
	}
	if (!IsGroupFiniteVector(Result.DesiredVelocityCentimetersPerSecond)) return false;
	OutResult = MoveTemp(Result);
	return true;
}

bool FCatFishingGroupModel::IntegrateLateralVelocity(const FVector& CurrentLateral, const FVector& TargetLateral,
	const FVector& LateralAcceleration, const double DeltaSeconds, const double Friction,
	const double BrakingDeceleration, FVector& OutVelocity)
{
	OutVelocity = FVector::ZeroVector;
	if (!IsGroupFiniteVector(CurrentLateral) || !IsGroupFiniteVector(TargetLateral) || !IsGroupFiniteVector(LateralAcceleration)
		|| !IsGroupFiniteNonNegative(DeltaSeconds) || !IsGroupFiniteNonNegative(Friction)
		|| !IsGroupFiniteNonNegative(BrakingDeceleration)) return false;
	FVector Velocity = CurrentLateral;
	double RemainingSeconds = DeltaSeconds;
	while (RemainingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double H = FMath::Min(RemainingSeconds, 1.0 / 120.0);
		if (TargetLateral.IsNearlyZero())
		{
			const double Speed = Velocity.Size();
			if (!FMath::IsFinite(Speed)) return false;
			if (Speed > 0.0)
			{
				const double RemainingSpeed = FMath::Max(0.0, Speed - (Friction * Speed + BrakingDeceleration) * H);
				Velocity *= RemainingSpeed / Speed;
			}
		}
		else
		{
			Velocity = (Velocity + LateralAcceleration * H).GetClampedToMaxSize(TargetLateral.Size());
		}
		if (!IsGroupFiniteVector(Velocity)) return false;
		RemainingSeconds -= H;
	}
	OutVelocity = Velocity;
	return true;
}

bool FCatFishingGroupModel::ComputeMovementStaminaDrain(const FCatFightGroupMovementCostInput& Input,
	FCatFightGroupMovementCostResult& OutResult)
{
	OutResult = {};
	if (!IsGroupFiniteVector(Input.MoveIntentWorld) || !IsGroupFiniteVector(Input.ActualDisplacementCentimeters)) return false;
	for (const double Value : {Input.MaximumMoveSpeedCentimetersPerSecond, Input.FixedStepSeconds,
		Input.ActiveStrength, Input.StandardStrength, Input.CostPerStrengthCentimeter, Input.NormalizedLoad,
		Input.UnloadedWorkMultiplier, Input.LoadStaminaMultiplier, Input.MovementStaminaMultiplier,
		Input.SupportStaminaPerSecond})
	{
		if (!IsGroupFiniteNonNegative(Value)) return false;
	}
	if (Input.NormalizedLoad > 1.0) return false;
	const FVector Intent = MakeGroupGroundIntent(Input.MoveIntentWorld);
	const double IntentMagnitude = Intent.Size();
	if (Input.ActiveStrength <= 0.0 || IntentMagnitude <= 0.0
		|| Input.FixedStepSeconds <= 0.0 || Input.MaximumMoveSpeedCentimetersPerSecond <= 0.0) return true;
	FCatFightGroupMovementCostResult Result;
	Result.IntendedDistanceCentimeters = IntentMagnitude * Input.MaximumMoveSpeedCentimetersPerSecond * Input.FixedStepSeconds;
	if (!FMath::IsFinite(Result.IntendedDistanceCentimeters)) return false;
	const double SignedProgress = FVector::DotProduct(Input.ActualDisplacementCentimeters, Intent / IntentMagnitude);
	if (!FMath::IsFinite(SignedProgress)) return false;
	Result.ActualProgressCentimeters = FMath::Clamp(SignedProgress, 0.0, Result.IntendedDistanceCentimeters);
	Result.BlockedEffortRatio = Result.IntendedDistanceCentimeters > 0.0
		? IntentMagnitude * (1.0 - Result.ActualProgressCentimeters / Result.IntendedDistanceCentimeters) : 0.0;
	FCatFightCatWorkInput Work;
	Work.PositiveWorkUnits = Input.StandardStrength * Result.ActualProgressCentimeters;
	Work.CostPerWorkUnit = Input.CostPerStrengthCentimeter;
	Work.NormalizedLoad = Input.NormalizedLoad;
	Work.UnloadedWorkMultiplier = Input.UnloadedWorkMultiplier;
	Work.LoadStaminaMultiplier = Input.LoadStaminaMultiplier;
	Work.ActionMultiplier = Input.MovementStaminaMultiplier;
	if (!FCatFishingFightWorkModel::ComputeCatWorkDrain(Work, Result.WorkStaminaDrain)) return false;
	// Opposite efforts can spend stamina with zero net travel. Reuse the existing timed-support price,
	// without inventing a conflict penalty or charging somebody for passive displacement.
	Result.SupportStaminaDrain = Input.SupportStaminaPerSecond * Input.FixedStepSeconds
		* FMath::Square(Result.BlockedEffortRatio) * Input.MovementStaminaMultiplier;
	Result.StaminaDrain = Result.WorkStaminaDrain + Result.SupportStaminaDrain;
	if (!IsGroupFiniteNonNegative(Result.StaminaDrain)) return false;
	OutResult = Result;
	return true;
}

bool FCatFishingGroupModel::SettleStamina(const FCatFightGroupStaminaInput& Input,
	FCatFightGroupStaminaResult& OutResult)
{
	OutResult = {};
	const int32 Count = Input.Participants.Num();
	if (Input.Contributions.Num() != Count || Input.PersonalMovementDrains.Num() != Count
		|| !IsGroupFiniteNonNegative(Input.SharedStaminaDrain) || !IsGroupFiniteNonNegative(Input.RecoveryPerParticipant)) return false;
	FCatFightGroupStaminaResult Result;
	Result.Participants.SetNum(Count);
	TArray<int32> Payers;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FCatFightGroupParticipantInput& Participant = Input.Participants[Index];
		const double ActiveStrength = Input.Contributions[Index].ActiveStrength;
		if (!IsValidGroupParticipant(Participant) || !IsGroupFiniteNonNegative(ActiveStrength)
			|| !IsGroupFiniteNonNegative(Input.PersonalMovementDrains[Index])
			|| (Participant.CurrentStamina == 0.0 && ActiveStrength > 0.0)) return false;
		FCatFightGroupStaminaParticipantResult& Settlement = Result.Participants[Index];
		Settlement.PersonalMovementDrain = ActiveStrength > 0.0
			? FMath::Min(Participant.CurrentStamina, Input.PersonalMovementDrains[Index]) : 0.0;
		Settlement.RemainingStamina = Participant.CurrentStamina - Settlement.PersonalMovementDrain;
		if (ActiveStrength > 0.0 && Settlement.RemainingStamina > 0.0) Payers.Add(Index);
	}
	// Saturate the smallest balances first. This is deterministic, equal-share allocation rather
	// than strength-proportional billing, and does not depend on member ordering.
	Payers.Sort([&](const int32 Left, const int32 Right)
	{
		const double LeftBalance = Result.Participants[Left].RemainingStamina;
		const double RightBalance = Result.Participants[Right].RemainingStamina;
		return LeftBalance == RightBalance ? Left < Right : LeftBalance < RightBalance;
	});
	double RemainingBill = Input.SharedStaminaDrain;
	for (int32 PayerIndex = 0; PayerIndex < Payers.Num(); ++PayerIndex)
	{
		FCatFightGroupStaminaParticipantResult& Settlement = Result.Participants[Payers[PayerIndex]];
		const double EqualShare = RemainingBill / (Payers.Num() - PayerIndex);
		Settlement.SharedDrain = FMath::Min(Settlement.RemainingStamina, EqualShare);
		Settlement.RemainingStamina -= Settlement.SharedDrain;
		RemainingBill = FMath::Max(0.0, RemainingBill - Settlement.SharedDrain);
	}
	Result.UnpaidSharedStaminaDrain = RemainingBill;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FCatFightGroupStaminaParticipantResult& Settlement = Result.Participants[Index];
		Settlement.Recovery = FMath::Min(Input.RecoveryPerParticipant,
			Input.Participants[Index].MaximumStamina - Settlement.RemainingStamina);
		Settlement.RemainingStamina += Settlement.Recovery;
		Settlement.StaminaDelta = Settlement.Recovery - Settlement.PersonalMovementDrain - Settlement.SharedDrain;
		Result.TotalStaminaDrain += Settlement.PersonalMovementDrain + Settlement.SharedDrain;
		Result.TotalRecovery += Settlement.Recovery;
		Result.TotalRemainingStamina += Settlement.RemainingStamina;
	}
	if (!IsGroupFiniteNonNegative(Result.TotalStaminaDrain) || !IsGroupFiniteNonNegative(Result.TotalRecovery)
		|| !IsGroupFiniteNonNegative(Result.TotalRemainingStamina)) return false;
	OutResult = MoveTemp(Result);
	return true;
}
