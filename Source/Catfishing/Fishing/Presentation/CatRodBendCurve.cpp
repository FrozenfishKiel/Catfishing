#include "Fishing/Presentation/CatRodBendCurve.h"

bool FCatRodBendCurve::Initialize(const FVector& InBase, const FVector& InTip, const FVector& BendRadians)
{
	if (InBase.ContainsNaN() || InTip.ContainsNaN() || BendRadians.ContainsNaN()) return false;
	Length = FVector::Distance(InBase, InTip);
	if (Length < 1.0) return false;
	Base = InBase;
	Axis = (InTip - Base) / Length;
	const FVector Transverse = BendRadians - Axis * FVector::DotProduct(BendRadians, Axis);
	Angle = FMath::Min(Transverse.Size(), UE_PI * 0.5);
	RotationAxis = FVector::CrossProduct(Axis, Transverse).GetSafeNormal();
	if (RotationAxis.IsNearlyZero()) RotationAxis = FVector::ForwardVector;
	Centres[0] = Base;
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const double T = (Index - 0.5) / Segments;
		// The grip remains rigid; curvature grows towards the tapered, more flexible tip.
		Centres[Index] = Centres[Index - 1] + FQuat(RotationAxis, Angle * T * T).RotateVector(Axis) * (Length / Segments);
	}
	return true;
}

FQuat FCatRodBendCurve::RotationAt(const FVector& RestPosition) const
{
	const double T = FMath::Clamp(FVector::DotProduct(RestPosition - Base, Axis) / Length, 0.0, 1.0);
	return FQuat(RotationAxis, Angle * T * T);
}

FVector FCatRodBendCurve::DeformPosition(const FVector& RestPosition) const
{
	const double Along = FVector::DotProduct(RestPosition - Base, Axis);
	if (Along <= 0.0 || Angle < UE_DOUBLE_SMALL_NUMBER) return RestPosition;
	const double Sample = FMath::Clamp(Along / Length, 0.0, 1.0) * Segments;
	const int32 Index = FMath::Min(FMath::FloorToInt(Sample), Segments - 1);
	FVector Centre = FMath::Lerp(Centres[Index], Centres[Index + 1], Sample - Index);
	const FQuat Rotation = RotationAt(RestPosition);
	if (Along > Length) Centre += Rotation.RotateVector(Axis) * (Along - Length);
	return Centre + Rotation.RotateVector(RestPosition - Base - Axis * Along);
}

FVector FCatRodBendCurve::TargetBend(const FVector& Axis, const FVector& ForceNewtons,
	const double ReferenceForceNewtons, const double MaximumAngleRadians)
{
	if (Axis.ContainsNaN() || ForceNewtons.ContainsNaN() || !FMath::IsFinite(ReferenceForceNewtons)
		|| ReferenceForceNewtons <= 0.0 || !FMath::IsFinite(MaximumAngleRadians)) return FVector::ZeroVector;
	const FVector UnitAxis = Axis.GetSafeNormal();
	if (UnitAxis.IsNearlyZero()) return FVector::ZeroVector;
	const FVector Transverse = ForceNewtons - UnitAxis * FVector::DotProduct(ForceNewtons, UnitAxis);
	const double Force = Transverse.Size();
	return Transverse.GetSafeNormal() * FMath::Clamp(MaximumAngleRadians, 0.0, UE_PI * 0.5)
		* (Force / (Force + ReferenceForceNewtons));
}

FVector FCatRodBendCurve::SmoothBend(const FVector& Current, const FVector& Target,
	const double DeltaSeconds, const double ResponseSeconds)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0) return Current;
	const double Alpha = 1.0 - FMath::Exp(-DeltaSeconds / FMath::Max(ResponseSeconds, 0.001));
	return FMath::Lerp(Current, Target, Alpha);
}
