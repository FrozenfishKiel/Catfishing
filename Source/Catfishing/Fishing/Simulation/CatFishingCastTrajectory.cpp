#include "Fishing/Simulation/CatFishingCastTrajectory.h"

bool FCatFishingCastTrajectory::Initialize(const FVector& InOrigin, const FVector& InLanding,
	const double InGravityZ, const double InStartedServerTime)
{
	*this = FCatFishingCastTrajectory{};
	if (InOrigin.ContainsNaN() || InLanding.ContainsNaN() || !FMath::IsFinite(InGravityZ)
		|| InGravityZ >= -UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(InStartedServerTime)) return false;
	Origin = InOrigin;
	Landing = InLanding;
	GravityZ = InGravityZ;
	StartedServerTime = InStartedServerTime;
	const FVector Delta = Landing - Origin;
	// 1500 cm/s 是水平表现速度；额外飞行时间保证先上扬、再下降到水面（含竿尖低于水面的情况）。
	DurationSeconds = FMath::Max3(0.35, Delta.Size2D() / 1500.0,
		FMath::Sqrt(2.0 * FMath::Abs(Delta.Z) / -GravityZ) + 0.2);
	InitialVelocity = Delta / DurationSeconds - FVector(0.0, 0.0, 0.5 * GravityZ * DurationSeconds);
	return !InitialVelocity.ContainsNaN() && FMath::IsFinite(DurationSeconds);
}

FVector FCatFishingCastTrajectory::Evaluate(const double ServerTime) const
{
	if (DurationSeconds <= 0.0 || ServerTime <= StartedServerTime) return Origin;
	const double Elapsed = ServerTime - StartedServerTime;
	if (Elapsed >= DurationSeconds) return Landing;
	return Origin + InitialVelocity * Elapsed + FVector(0.0, 0.0, 0.5 * GravityZ * Elapsed * Elapsed);
}
