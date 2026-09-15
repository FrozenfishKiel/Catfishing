#include "Fishing/Simulation/CatFishBodyModel.h"

bool FCatFishBodyGeometry::IsValid() const
{
	return !MouthLocalPositionCentimeters.ContainsNaN() && !CenterOfMassLocalPositionCentimeters.ContainsNaN()
		&& !ScaleOriginLocalCentimeters.ContainsNaN() && FMath::IsFinite(YawRadiusOfGyrationCentimeters)
		&& YawRadiusOfGyrationCentimeters >= 0.0;
}

bool FCatFishBodyGeometry::HasMouthLever() const
{
	return IsValid() && YawRadiusOfGyrationCentimeters > UE_DOUBLE_SMALL_NUMBER
		&& FVector::Dist2D(MouthLocalPositionCentimeters, CenterOfMassLocalPositionCentimeters) > UE_DOUBLE_SMALL_NUMBER;
}

FCatFishBodyGeometry FCatFishBodyGeometry::Scaled(const double VisualScale) const
{
	auto Result = *this;
	Result.MouthLocalPositionCentimeters = ScaleOriginLocalCentimeters + (MouthLocalPositionCentimeters - ScaleOriginLocalCentimeters) * VisualScale;
	Result.CenterOfMassLocalPositionCentimeters = ScaleOriginLocalCentimeters + (CenterOfMassLocalPositionCentimeters - ScaleOriginLocalCentimeters) * VisualScale;
	Result.YawRadiusOfGyrationCentimeters *= VisualScale;
	return Result;
}

bool FCatFishBodyConfig::IsValid() const
{
	return Geometry.IsValid() && FMath::IsFinite(MaximumSwimTurnRateDegreesPerSecond) && MaximumSwimTurnRateDegreesPerSecond > 0
		&& FMath::IsFinite(TurnResponseSeconds) && TurnResponseSeconds > 0
		&& FMath::IsFinite(SwimTurnTorqueFraction) && SwimTurnTorqueFraction > 0 && SwimTurnTorqueFraction <= 1
		&& FMath::IsFinite(MaximumBodyTurnRateDegreesPerSecond) && MaximumBodyTurnRateDegreesPerSecond >= MaximumSwimTurnRateDegreesPerSecond;
}

FVector FCatFishBodyModel::RotateLocal(const FVector& Local, const FVector& Heading)
{
	const FVector Forward = Heading.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	return FVector(Forward.X * Local.X - Forward.Y * Local.Y, Forward.Y * Local.X + Forward.X * Local.Y, Local.Z);
}

FVector FCatFishBodyModel::MouthPosition(const FCatFishBodyGeometry& Geometry, const FVector& RootPosition, const FVector& Heading)
{
	return RootPosition + RotateLocal(Geometry.MouthLocalPositionCentimeters, Heading);
}

FVector FCatFishBodyModel::CenterPosition(const FCatFishBodyGeometry& Geometry, const FVector& RootPosition, const FVector& Heading)
{
	return RootPosition + RotateLocal(Geometry.CenterOfMassLocalPositionCentimeters, Heading);
}

FCatFishBodyTurn FCatFishBodyModel::PredictTurn(const FCatFishBodyConfig& Config, const FCatFishBodyState& State,
	const FVector& DesiredHeading, const double Effort, const double MassKilograms, const double FullThrustNewtons,
	const FVector& MouthForceNewtons, const double DeltaSeconds)
{
	FCatFishBodyTurn Result;
	Result.State = State;
	Result.State.Heading = State.Heading.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER,
		DesiredHeading.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector));
	const FVector LeverMeters = RotateLocal(Config.Geometry.MouthLocalPositionCentimeters
		- Config.Geometry.CenterOfMassLocalPositionCentimeters, Result.State.Heading) / 100.0;
	const double RadiusMeters = Config.Geometry.YawRadiusOfGyrationCentimeters / 100.0;
	// 零几何是质点的数学极限，供不含鱼身的约束测试使用；生产入战要求已标定的非零嘴部力臂。
	if (RadiusMeters <= UE_DOUBLE_SMALL_NUMBER)
	{
		Result.State.AngularVelocityRadiansPerSecond = 0;
		return Result;
	}
	const double MaximumSwimRate = FMath::DegreesToRadians(Config.MaximumSwimTurnRateDegreesPerSecond);
	const double FullSwimTorque = FullThrustNewtons * FMath::Max(LeverMeters.Size2D(), RadiusMeters) * Config.SwimTurnTorqueFraction;
	const double AngularDrag = FullSwimTorque / MaximumSwimRate;
	// 固定的附加水惯量使正常受力改变在响应时间内建立角速度，不因状态换挡重置。
	const double Inertia = FMath::Max(MassKilograms * RadiusMeters * RadiusMeters, AngularDrag * Config.TurnResponseSeconds);
	Result.EffectiveInertiaKilogramMetersSquared = Inertia;
	const double CurrentYaw = FMath::Atan2(Result.State.Heading.Y, Result.State.Heading.X);
	const FVector Target = DesiredHeading.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, Result.State.Heading);
	const double Error = FMath::FindDeltaAngleRadians(CurrentYaw, FMath::Atan2(Target.Y, Target.X));
	const double TargetRate = FMath::Clamp(Error / Config.TurnResponseSeconds, -MaximumSwimRate, MaximumSwimRate);
	Result.SwimTorqueNewtonMeters = FMath::Clamp(AngularDrag * TargetRate,
		-FullSwimTorque * Effort, FullSwimTorque * Effort);
	Result.LineTorqueNewtonMeters = FVector::CrossProduct(LeverMeters, MouthForceNewtons).Z;
	const double MaximumBodyRate = FMath::DegreesToRadians(Config.MaximumBodyTurnRateDegreesPerSecond);
	Result.State.AngularVelocityRadiansPerSecond = FMath::Clamp(
		(Inertia * State.AngularVelocityRadiansPerSecond + DeltaSeconds * (Result.SwimTorqueNewtonMeters + Result.LineTorqueNewtonMeters))
		/ (Inertia + DeltaSeconds * AngularDrag), -MaximumBodyRate, MaximumBodyRate);
	const double NewYaw = CurrentYaw + Result.State.AngularVelocityRadiansPerSecond * DeltaSeconds;
	Result.State.Heading = FVector(FMath::Cos(NewYaw), FMath::Sin(NewYaw), 0);
	return Result;
}
