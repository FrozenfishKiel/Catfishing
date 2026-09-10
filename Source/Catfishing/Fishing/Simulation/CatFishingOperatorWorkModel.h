#pragma once

#include "CoreMinimal.h"

/** Personal movement uses final collision-resolved motion; no reel or rod-rotation charges here. */
struct CATFISHING_API FCatFightOperatorMovementCostInput
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

struct CATFISHING_API FCatFightOperatorMovementCostResult
{
	double IntendedDistanceCentimeters = 0.0;
	double ActualProgressCentimeters = 0.0;
	double BlockedEffortRatio = 0.0;
	double WorkStaminaDrain = 0.0;
	double SupportStaminaDrain = 0.0;
	double StaminaDrain = 0.0;
};

/** Sole operator pricing; helping cats are ordinary physical bodies outside the fishing ledger. */
class CATFISHING_API FCatFishingOperatorWorkModel
{
public:
	static bool ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
		FCatFightOperatorMovementCostResult& OutResult);
};