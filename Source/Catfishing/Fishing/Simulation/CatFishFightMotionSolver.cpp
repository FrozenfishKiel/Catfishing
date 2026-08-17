#include "Fishing/Simulation/CatFishFightMotionSolver.h"

namespace
{
	bool IsFiniteMotionVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FVector ClampToBox(const FVector& Point, const FBox& Box)
	{
		return FVector(FMath::Clamp(Point.X, Box.Min.X, Box.Max.X),
			FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y), FMath::Clamp(Point.Z, Box.Min.Z, Box.Max.Z));
	}
}

FCatFishMotionSolveResult FCatFishFightMotionSolver::Solve(const FCatFishMotionSolveInput& Input)
{
	FCatFishMotionSolveResult Result;
	if (!Input.WaterBounds.IsValid || !IsFiniteMotionVector(Input.RodTipWorldPosition)
		|| !IsFiniteMotionVector(Input.ProposedFishWorldPosition)
		|| !FMath::IsFinite(Input.MaximumLineLengthCentimeters)
		|| Input.MaximumLineLengthCentimeters <= 0.0)
	{
		return Result;
	}

	FVector Projected = ClampToBox(Input.ProposedFishWorldPosition, Input.WaterBounds);
	const FVector FromRod = Projected - Input.RodTipWorldPosition;
	if (FromRod.SizeSquared() > FMath::Square(Input.MaximumLineLengthCentimeters))
	{
		Projected = Input.RodTipWorldPosition + FromRod.GetSafeNormal() * Input.MaximumLineLengthCentimeters;
		Projected = ClampToBox(Projected, Input.WaterBounds);
	}

	if (!Input.WaterBounds.IsInsideOrOn(Projected)
		|| FVector::DistSquared(Input.RodTipWorldPosition, Projected)
			> FMath::Square(Input.MaximumLineLengthCentimeters + 0.01))
	{
		return Result;
	}

	Result.bSucceeded = true;
	Result.FishWorldPosition = Projected;
	return Result;
}
