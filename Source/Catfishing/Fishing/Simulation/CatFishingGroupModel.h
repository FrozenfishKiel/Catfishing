#pragma once

#include "CoreMinimal.h"

/** A frozen member snapshot. Identity and authoritative ASC ownership stay in the Runner. */
struct CATFISHING_API FCatFightGroupParticipantInput
{
	double FishingStrength = 0.0;
	double CurrentStamina = 0.0;
	double MaximumStamina = 0.0;
	double MassKilograms = 5.0;
	/** Ground-plane analog intent; magnitude is clamped to one. */
	FVector MoveIntentWorld = FVector::ZeroVector;
	double MaximumMoveSpeedCentimetersPerSecond = 0.0;
	bool bPrimary = false;
};

struct CATFISHING_API FCatFightGroupInput
{
	TArray<FCatFightGroupParticipantInput> Participants;
	/** Ground-plane direction away from the fish. Zero uses world -X. */
	FVector ResistanceDirectionWorld = -FVector::ForwardVector;
	double HelperStrengthMultiplier = 0.5;
};

struct CATFISHING_API FCatFightGroupParticipantResult
{
	/** Full role-adjusted strength at any positive stamina, exactly zero when exhausted. */
	double ActiveStrength = 0.0;
	double MovementIntentMagnitude = 0.0;
	/** Standing and movement share this single vector budget. Do not add another full support force. */
	FVector AppliedStrengthWorld = FVector::ZeroVector;
	FVector MovementStrengthWorld = FVector::ZeroVector;
	double SignedResistanceStrength = 0.0;
};

struct CATFISHING_API FCatFightGroupResult
{
	/** Same order and length as the frozen input; never an independently mutable membership list. */
	TArray<FCatFightGroupParticipantResult> Participants;
	double TotalCurrentStamina = 0.0;
	double TotalMaximumStamina = 0.0;
	double TotalMassKilograms = 0.0;
	double TotalActiveStrength = 0.0;
	int32 ActiveParticipantCount = 0;
	FVector AppliedStrengthWorld = FVector::ZeroVector;
	FVector MovementStrengthWorld = FVector::ZeroVector;
	/** Can be negative when members actively pull toward the fish. */
	double SignedResistanceStrength = 0.0;
	/** Weighted intent, not an additional force. Member count cannot multiply walking speed. */
	FVector DesiredVelocityCentimetersPerSecond = FVector::ZeroVector;
};

/** Personal movement uses final collision-resolved motion; no reel or rod-rotation charges here. */
struct CATFISHING_API FCatFightGroupMovementCostInput
{
	FVector MoveIntentWorld = FVector::ZeroVector;
	FVector ActualDisplacementCentimeters = FVector::ZeroVector;
	double MaximumMoveSpeedCentimetersPerSecond = 0.0;
	double FixedStepSeconds = 0.0;
	double ActiveStrength = 0.0;
	double StandardStrength = 10.0;
	double CostPerStrengthCentimeter = 0.002;
	double NormalizedLoad = 0.0;
	double UnloadedWorkMultiplier = 0.15;
	double LoadStaminaMultiplier = 1.0;
	double MovementStaminaMultiplier = 1.0;
	double SupportStaminaPerSecond = 2.0;
};

struct CATFISHING_API FCatFightGroupMovementCostResult
{
	double IntendedDistanceCentimeters = 0.0;
	double ActualProgressCentimeters = 0.0;
	double BlockedEffortRatio = 0.0;
	double WorkStaminaDrain = 0.0;
	double SupportStaminaDrain = 0.0;
	double StaminaDrain = 0.0;
};

struct CATFISHING_API FCatFightGroupStaminaInput
{
	/** Inputs and results must describe the same frozen step. */
	TArray<FCatFightGroupParticipantInput> Participants;
	TArray<FCatFightGroupParticipantResult> Contributions;
	/** Each member pays this amount first, capped by their own balance. */
	TArray<double> PersonalMovementDrains;
	/** Reel, rod rotation and deduplicated hold support, charged once to the whole group. */
	double SharedStaminaDrain = 0.0;
	/** Explicit caller-authorized recovery for each member, never a transferable team pool. */
	double RecoveryPerParticipant = 0.0;
};

struct CATFISHING_API FCatFightGroupStaminaParticipantResult
{
	double PersonalMovementDrain = 0.0;
	double SharedDrain = 0.0;
	double Recovery = 0.0;
	double StaminaDelta = 0.0;
	double RemainingStamina = 0.0;
};

struct CATFISHING_API FCatFightGroupStaminaResult
{
	TArray<FCatFightGroupStaminaParticipantResult> Participants;
	double TotalStaminaDrain = 0.0;
	double TotalRecovery = 0.0;
	double UnpaidSharedStaminaDrain = 0.0;
	double TotalRemainingStamina = 0.0;
};

/** Pure N-member calculation. Runtime owns capacity, membership, network validation and a single ASC write. */
class CATFISHING_API FCatFishingGroupModel
{
public:
	static bool ComputeForces(const FCatFightGroupInput& Input, FCatFightGroupResult& OutResult);
	/** Shared CMC/constraint lateral integration; friction is the effective braking coefficient in 1/s. */
	static bool IntegrateLateralVelocity(const FVector& CurrentLateral, const FVector& TargetLateral,
		const FVector& LateralAcceleration, double DeltaSeconds, double Friction,
		double BrakingDeceleration, FVector& OutVelocity);
	static bool ComputeMovementStaminaDrain(const FCatFightGroupMovementCostInput& Input,
		FCatFightGroupMovementCostResult& OutResult);
	/** Personal charges first; the common bill is evenly water-filled across contributors' remaining balances. */
	static bool SettleStamina(const FCatFightGroupStaminaInput& Input, FCatFightGroupStaminaResult& OutResult);
};
