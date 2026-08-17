#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"

enum class ECatFightStepOutcome : uint8
{
	None,
	FishExhausted,
	CatStaminaExhausted,
	RodBroken,
	Escaped,
	NearShore
};

/** Frozen, asset-derived inputs for the deterministic fight step. */
struct CATFISHING_API FCatFightSimulationConfig
{
	double FixedStepSeconds = 0.0;
	double BaseDrainPerSecond = 0.0;
	double BaseDrainMultiplier = 0.0;
	double StruggleDrainMultiplier = 0.0;
	double ReelSpeedCentimetersPerSecond = 0.0;
	double FishCalmSpeedCentimetersPerSecond = 0.0;
	double FishStruggleSpeedCentimetersPerSecond = 0.0;
	double MaximumLineLengthCentimeters = 0.0;
	double RodWearPerTensionSecond = 0.0;
	double RodDurability = TNumericLimits<double>::Max();
	double EscapeSlackCentimeters = 100.0;
	double NearShoreLineLengthCentimeters = 100.0;

	bool IsValid() const;
};

/** Mutable numeric state. World position is copied from the encounter Actor at the start of each authority step. */
struct CATFISHING_API FCatFightSimulationState
{
	double CatStamina = 0.0;
	double FishStamina = 0.0;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector FishWorldPosition = FVector::ZeroVector;
	bool bReeling = false;
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
};

struct CATFISHING_API FCatFightStepResult
{
	bool bSucceeded = false;
	double CatStaminaDrain = 0.0;
	double FishStaminaDrain = 0.0;
	double TensionCentimeters = 0.0;
	double LineLengthCentimeters = 0.0;
	double AbsoluteRodWear = 0.0;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	ECatFightStepOutcome Outcome = ECatFightStepOutcome::None;
};

/** Stateless deterministic fight math. It never reads World time, Actors, assets, or random globals. */
class CATFISHING_API FCatFishingFightSimulator
{
public:
	static FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
		const FVector& OutwardDirection);
};
