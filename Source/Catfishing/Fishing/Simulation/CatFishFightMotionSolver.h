#pragma once

#include "CoreMinimal.h"

struct CATFISHING_API FCatFishMotionSolveInput
{
	FVector RodTipWorldPosition = FVector::ZeroVector;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	FBox WaterBounds = FBox(ForceInit);
	double MaximumLineLengthCentimeters = 0.0;
};

struct CATFISHING_API FCatFishMotionSolveResult
{
	bool bSucceeded = false;
	FVector FishWorldPosition = FVector::ZeroVector;
};

/** Pure deterministic projection into the frozen water geometry and rod line reach. */
class CATFISHING_API FCatFishFightMotionSolver
{
public:
	static FCatFishMotionSolveResult Solve(const FCatFishMotionSolveInput& Input);
};
