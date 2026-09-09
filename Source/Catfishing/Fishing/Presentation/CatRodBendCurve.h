#pragma once

#include "CoreMinimal.h"

/** Pure cosmetic, arc-length sampled rod centreline. Distances are mesh-local centimetres; bend is radians. */
struct CATFISHING_API FCatRodBendCurve
{
	static constexpr int32 Segments = 48;
	bool Initialize(const FVector& InBase, const FVector& InTip, const FVector& BendRadians);
	FVector DeformPosition(const FVector& RestPosition) const;
	FQuat RotationAt(const FVector& RestPosition) const;
	static FVector TargetBend(const FVector& Axis, const FVector& ForceNewtons,
		double ReferenceForceNewtons, double MaximumAngleRadians);
	static FVector SmoothBend(const FVector& Current, const FVector& Target, double DeltaSeconds, double ResponseSeconds);

private:
	FVector Base = FVector::ZeroVector;
	FVector Axis = FVector::UpVector;
	FVector RotationAxis = FVector::ForwardVector;
	double Length = 1.0;
	double Angle = 0.0;
	FVector Centres[Segments + 1]{};
};
