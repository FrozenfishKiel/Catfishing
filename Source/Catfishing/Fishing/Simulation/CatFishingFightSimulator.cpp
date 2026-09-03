#include "Fishing/Simulation/CatFishingFightSimulator.h"

#include "Fishing/Simulation/CatFishingFightWorkModel.h"

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}
}

bool FCatFightSimulationConfig::IsValid() const
{
	return FMath::IsFinite(FixedStepSeconds) && FixedStepSeconds > 0.0
		&& IsFiniteNonNegative(PrimaryOperatorCatStrength)
		&& IsFiniteNonNegative(SecondCatStrength)
		&& IsFiniteNonNegative(PrimaryOperatorMassKilograms)
		&& IsFiniteNonNegative(HelperMassKilograms)
		&& FMath::IsFinite(GetCombinedCatMass()) && GetCombinedCatMass() > 0.0
		&& FMath::IsFinite(FishMassKilograms) && FishMassKilograms > 0.0
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0
		&& FMath::IsFinite(RodStrength) && RodStrength > 0.0
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0
		&& IsFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
		&& IsFiniteNonNegative(FishStaminaCostPerStrengthCentimeter)
		&& IsFiniteNonNegative(IsometricEffortMultiplier)
		&& FMath::IsFinite(BaseDrainMultiplier) && BaseDrainMultiplier > 0.0
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier >= BaseDrainMultiplier
		&& IsFiniteNonNegative(SlackStaminaRegenPerSecond)
		&& IsFiniteNonNegative(StalemateRodWearPerFishStrength)
		&& IsFiniteNonNegative(StruggleHoldRodWearPerSecond)
		&& FMath::IsFinite(TautRodWearMultiplier) && TautRodWearMultiplier >= 1.0
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& IsFiniteNonNegative(FishCalmSpeedCentimetersPerSecond)
		&& FMath::IsFinite(FishStruggleSpeedCentimetersPerSecond) && FishStruggleSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(FishExhaustionThreshold) && FishExhaustionThreshold >= 0.0
		&& FishExhaustionThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationAlignmentThreshold)
		&& StrongConfrontationAlignmentThreshold > 0.0 && StrongConfrontationAlignmentThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationConfirmationSeconds)
		&& StrongConfrontationConfirmationSeconds >= 0.0
		&& FMath::IsFinite(AngleStrengthExponent) && AngleStrengthExponent >= 0.1
		&& FMath::IsFinite(TensionResponseRangeCentimeters) && TensionResponseRangeCentimeters > 0.0
		&& FMath::IsFinite(MinimumRodLeverageMultiplier)
		&& MinimumRodLeverageMultiplier > 0.0 && MinimumRodLeverageMultiplier <= 1.0
		&& FMath::IsFinite(MaximumFishConstraintCorrectionSpeedCentimetersPerSecond)
		&& MaximumFishConstraintCorrectionSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(MinimumCarrierAwaySpeedMultiplier)
		&& MinimumCarrierAwaySpeedMultiplier >= 0.0 && MinimumCarrierAwaySpeedMultiplier <= 1.0
		&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0
		&& FMath::IsFinite(RodDurability) && RodDurability > 0.0
		&& IsFiniteNonNegative(EscapeSlackCentimeters);
}

FCatFightStepResult FCatFishingFightSimulator::Step(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FVector& RodTipWorldPosition,
	const FVector& DesiredFishDirection)
{
	FCatFightRodConstraintInput Constraint;
	Constraint.RodTipWorldPosition = RodTipWorldPosition;
	return Step(Config, State, Constraint, DesiredFishDirection);
}

FCatFightStepResult FCatFishingFightSimulator::Step(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
	const FVector& DesiredFishDirection)
{
	FCatFightStepResult Result;
	if (!Config.IsValid() || !FMath::IsFinite(State.CatStamina) || State.CatStamina < 0.0
		|| !FMath::IsFinite(State.FishStamina) || State.FishStamina < 0.0
		|| !FMath::IsFinite(State.LineLengthCentimeters) || State.LineLengthCentimeters < 0.0
		|| !FMath::IsFinite(State.AbsoluteRodWear) || State.AbsoluteRodWear < 0.0
		|| !FMath::IsFinite(State.StrongConfrontationBuildUpSeconds)
		|| State.StrongConfrontationBuildUpSeconds < 0.0
		|| !IsFiniteVector(State.FishWorldPosition)
		|| !IsFiniteVector(RodConstraint.RodTipWorldPosition)
		|| !IsFiniteVector(RodConstraint.RodForwardWorld)
		|| !IsFiniteVector(RodConstraint.RodTipVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierDesiredVelocityCentimetersPerSecond)
		|| (RodConstraint.bRodHeld && RodConstraint.RodForwardWorld.IsNearlyZero())
		|| !IsFiniteVector(DesiredFishDirection) || DesiredFishDirection.IsNearlyZero())
	{
		return Result;
	}

	const double Dt = Config.FixedStepSeconds;
	const FVector RodTip = RodConstraint.RodTipWorldPosition;
	const FVector FromRod = State.FishWorldPosition - RodTip;
	const double Distance0 = FromRod.Size();
	const double VerticalDistance = FMath::Abs(FromRod.Z);
	FVector HorizontalOutward(FromRod.X, FromRod.Y, 0.0);
	const bool bHasRadialBasis = HorizontalOutward.Normalize();
	if (!bHasRadialBasis)
	{
		HorizontalOutward = FVector::ForwardVector;
	}
	FVector FishDirection(DesiredFishDirection.X, DesiredFishDirection.Y, 0.0);
	if (!FishDirection.Normalize())
	{
		return Result;
	}

	const bool bOperatorPresent = State.bOperatorPresent;
	const bool bReeling = bOperatorPresent && State.CatAction == ECatFightCatAction::Pull;
	const bool bFreeSpool = !bOperatorPresent || State.CatAction == ECatFightCatAction::Slack;
	const bool bStruggling = !State.bFishExhausted
		&& State.MotionIntent == ECatFishMotionIntent::StrugglingOutward;
	const double SwimSpeed = State.bFishExhausted ? 0.0
		: bStruggling ? Config.FishStruggleSpeedCentimetersPerSecond
		: Config.FishCalmSpeedCentimetersPerSecond;
	const FVector FishIntentDisplacement = FishDirection * SwimSpeed * Dt;
	FVector FreeFishPosition = State.FishWorldPosition + FishIntentDisplacement;
	FreeFishPosition.Z = State.FishWorldPosition.Z;

	const double Alignment = bHasRadialBasis
		? FMath::Clamp(FVector::DotProduct(FishDirection, HorizontalOutward), -1.0, 1.0) : 0.0;
	const double OutwardLoad = FMath::Pow(FMath::Max(0.0, Alignment), Config.AngleStrengthExponent);
	const FVector LineDirection = FromRod.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, HorizontalOutward);
	const FVector RodForward = RodConstraint.RodForwardWorld.GetSafeNormal();
	const double RodLineAlignment = RodConstraint.bRodHeld
		? FMath::Clamp(FVector::DotProduct(RodForward, LineDirection), 0.0, 1.0) : 1.0;
	const double RodLeverage = RodConstraint.bRodHeld
		? FMath::Lerp(Config.MinimumRodLeverageMultiplier, 1.0, RodLineAlignment) : 1.0;
	const double CombinedCatStrength = bOperatorPresent ? Config.GetCombinedCatStrength() : 0.0;
	const double EffectiveCatStrength = CombinedCatStrength * RodLeverage;

	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters,
		VerticalDistance, Config.MaximumLineLengthCentimeters);
	const double RequestedReelDistance = bReeling && EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER
		? FMath::Min(Config.ReelSpeedCentimetersPerSecond * Dt, PaidOutLine0 - VerticalDistance) : 0.0;
	double LineLength = PaidOutLine0 - RequestedReelDistance;
	const double FreeDistance = FVector::Distance(RodTip, FreeFishPosition);
	if (bFreeSpool)
	{
		LineLength = FMath::Clamp(FMath::Max(LineLength, FreeDistance),
			VerticalDistance, Config.MaximumLineLengthCentimeters);
	}
	const bool bFreeSpoolReleased = bFreeSpool
		&& FreeDistance <= Config.MaximumLineLengthCentimeters + UE_DOUBLE_KINDA_SMALL_NUMBER;

	// 移动意图只改变猫端候选位置；卷线请求只改变静止线长。两者在这里第一次汇合。
	const FVector CarrierIntentDisplacement = RodConstraint.bRodHeld
		? RodConstraint.CarrierDesiredVelocityCentimetersPerSecond * Dt : FVector::ZeroVector;
	const FVector IntendedRodTip = RodTip + CarrierIntentDisplacement;
	const double IntendedDistance = FVector::Distance(IntendedRodTip, FreeFishPosition);
	const double ConstraintError = FMath::Max(0.0, IntendedDistance - LineLength);
	const bool bLineRestraining = ConstraintError > UE_DOUBLE_KINDA_SMALL_NUMBER;

	FVector ProposedFishPosition = FreeFishPosition;
	double FishCorrection = 0.0;
	double CarrierCorrection = 0.0;
	if (bLineRestraining && !bFreeSpoolReleased)
	{
		const double CatMass = Config.GetCombinedCatMass();
		const double FishCorrectionShare = RodConstraint.bRodHeld
			? CatMass / (CatMass + Config.FishMassKilograms) : 1.0;
		const double CarrierCorrectionShare = RodConstraint.bRodHeld
			? Config.FishMassKilograms / (CatMass + Config.FishMassKilograms) : 0.0;
		const double MaximumFishCorrection = Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond * Dt;
		FVector TowardRod = IntendedRodTip - ProposedFishPosition;
		TowardRod.Z = 0.0;
		const double HorizontalDistance = TowardRod.Size2D();
		FishCorrection = FMath::Min3(ConstraintError * FishCorrectionShare,
			MaximumFishCorrection, HorizontalDistance);
		if (FishCorrection > UE_DOUBLE_SMALL_NUMBER && HorizontalDistance > UE_DOUBLE_SMALL_NUMBER)
		{
			ProposedFishPosition += TowardRod / HorizontalDistance * FishCorrection;
			ProposedFishPosition.Z = State.FishWorldPosition.Z;
		}
		CarrierCorrection = FMath::Min(ConstraintError * CarrierCorrectionShare,
			MaximumFishCorrection);
	}

	const double Distance1 = FVector::Distance(RodTip, ProposedFishPosition);
	const double NormalizedTension = FMath::Clamp(
		ConstraintError / Config.TensionResponseRangeCentimeters, 0.0, 1.0);
	const bool bLineTaut = bLineRestraining
		|| LineLength - Distance1 <= UE_DOUBLE_KINDA_SMALL_NUMBER;

	const double CatCarrierIntent = bOperatorPresent && RodConstraint.bRodHeld
		? FMath::Max(0.0, -FVector::DotProduct(CarrierIntentDisplacement, LineDirection)) : 0.0;
	const double CatCarrierActual = bOperatorPresent && RodConstraint.bRodHeld
		? FMath::Max(0.0, -FVector::DotProduct(
			RodConstraint.RodTipVelocityCentimetersPerSecond * Dt, LineDirection)) : 0.0;
	const double CatActiveIntentDistance = RequestedReelDistance + CatCarrierIntent;
	const double FishOutwardIntentDistance = FMath::Max(0.0,
		FVector::DotProduct(FishIntentDisplacement, LineDirection));
	// 锁线本身也是意图：当鱼试图沿线向外且约束已介入时，猫在做等长保持功。
	// 收线或主动后退已经给出了猫的显式意图，此时不再重复叠加保持距离。
	const double CatHoldIntentDistance = State.CatAction == ECatFightCatAction::None
		&& CatActiveIntentDistance <= UE_DOUBLE_SMALL_NUMBER && bLineRestraining
		? FishOutwardIntentDistance * NormalizedTension : 0.0;
	Result.RequestedReelDistanceCentimeters = RequestedReelDistance;
	Result.ActualReelDistanceCentimeters = RequestedReelDistance;
	Result.CatIntendedLineDistanceCentimeters = CatActiveIntentDistance + CatHoldIntentDistance;
	Result.CatActualLineDistanceCentimeters = RequestedReelDistance + CatCarrierActual;
	Result.FishIntendedLineDistanceCentimeters = FMath::Abs(
		FVector::DotProduct(FishIntentDisplacement, LineDirection));
	Result.FishActualLineDistanceCentimeters = FMath::Abs(FVector::DotProduct(
		ProposedFishPosition - State.FishWorldPosition, LineDirection));

	double IgnoredEffortDistance = 0.0;
	// 鱼力竭后进入纯收尾：仍求解收线和双端位移，但不再向任何猫结算做功消耗。
	if (!State.bFishExhausted && bOperatorPresent && EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER
		&& Result.CatIntendedLineDistanceCentimeters > UE_DOUBLE_SMALL_NUMBER)
	{
		FCatFightWorkInput CatWork;
		// 杠杆决定传到鱼线上的有效力，但猫为维持姿态付出的肌肉做功仍按自身主动强度计费。
		CatWork.Strength = CombinedCatStrength;
		CatWork.IntendedLineDistanceCentimeters = Result.CatIntendedLineDistanceCentimeters;
		CatWork.ActualLineDistanceCentimeters = Result.CatActualLineDistanceCentimeters;
		CatWork.IsometricEffortMultiplier = Config.IsometricEffortMultiplier;
		CatWork.CostPerStrengthCentimeter = Config.CatStaminaCostPerStrengthCentimeter;
		CatWork.PhaseMultiplier = bStruggling ? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
		if (!FCatFishingFightWorkModel::ComputeDrain(CatWork, Result.CatStaminaDrain, IgnoredEffortDistance))
		{
			return FCatFightStepResult{};
		}
	}
	else if (bOperatorPresent && bFreeSpool && bFreeSpoolReleased && !bLineRestraining)
	{
		const double Capped = FMath::Min(Config.CatStaminaMaximum,
			State.CatStamina + Config.SlackStaminaRegenPerSecond * Dt);
		Result.CatStaminaDrain = -(Capped - State.CatStamina);
	}

	if (!State.bFishExhausted && State.FishStamina > UE_DOUBLE_SMALL_NUMBER
		&& Result.FishIntendedLineDistanceCentimeters > UE_DOUBLE_SMALL_NUMBER)
	{
		FCatFightWorkInput FishWork;
		FishWork.Strength = Config.FishStrength;
		FishWork.IntendedLineDistanceCentimeters = Result.FishIntendedLineDistanceCentimeters;
		FishWork.ActualLineDistanceCentimeters = Result.FishActualLineDistanceCentimeters;
		FishWork.IsometricEffortMultiplier = Config.IsometricEffortMultiplier;
		FishWork.CostPerStrengthCentimeter = Config.FishStaminaCostPerStrengthCentimeter;
		FishWork.PhaseMultiplier = bStruggling ? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
		if (!FCatFishingFightWorkModel::ComputeDrain(FishWork, Result.FishStaminaDrain, IgnoredEffortDistance))
		{
			return FCatFightStepResult{};
		}
		Result.FishStaminaDrain = FMath::Min(Result.FishStaminaDrain, State.FishStamina);
		if (State.FishStamina - Result.FishStaminaDrain <= Config.FishExhaustionThreshold)
		{
			Result.FishStaminaDrain = State.FishStamina;
		}
	}

	const bool bConfrontationCandidate = !State.bFishExhausted && bLineRestraining
		&& OutwardLoad >= Config.StrongConfrontationAlignmentThreshold;
	Result.StrongConfrontationBuildUpSeconds = bConfrontationCandidate
		? State.StrongConfrontationBuildUpSeconds + Dt : 0.0;
	Result.bStrongConfrontation = bConfrontationCandidate
		&& Result.StrongConfrontationBuildUpSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER
			>= Config.StrongConfrontationConfirmationSeconds;
	const double ActualRadialFishDelta = FVector::DotProduct(
		ProposedFishPosition - State.FishWorldPosition, LineDirection);
	Result.bStalemate = Result.bStrongConfrontation
		&& FMath::Abs(ActualRadialFishDelta) <= FMath::Max(1.0, SwimSpeed * Dt * 0.1);

	const double WearLoad = FMath::Max(OutwardLoad, NormalizedTension);
	const double RodWearDelta = bLineRestraining
		? (Config.FishStrength * Config.StalemateRodWearPerFishStrength
			+ (bStruggling ? Config.StruggleHoldRodWearPerSecond : 0.0))
			* WearLoad * Dt * Config.TautRodWearMultiplier : 0.0;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta;

	if (RodConstraint.bRodHeld && bLineRestraining && !bFreeSpoolReleased)
	{
		const double CatMass = Config.GetCombinedCatMass();
		const double CarrierShare = Config.FishMassKilograms / (CatMass + Config.FishMassKilograms);
		Result.CarrierConstraintCorrectionCentimeters = CarrierCorrection;
		Result.CarrierTargetPullSpeedCentimetersPerSecond = CarrierCorrection / Dt;
		Result.CarrierPullAccelerationCentimetersPerSecondSquared =
			Result.CarrierTargetPullSpeedCentimetersPerSecond / Dt;
		Result.CarrierAwaySpeedMultiplier = FMath::Lerp(1.0,
			Config.MinimumCarrierAwaySpeedMultiplier, NormalizedTension * CarrierShare);
	}

	Result.IntendedSwimSpeedCentimetersPerSecond = SwimSpeed;
	Result.LineLengthCentimeters = LineLength;
	Result.TensionCentimeters = ConstraintError;
	Result.StraightLineDistanceCentimeters = Distance1;
	Result.SlackLineLengthCentimeters = FMath::Max(0.0, LineLength - Distance1);
	Result.NormalizedTension = NormalizedTension;
	Result.bLineTaut = bLineTaut;
	Result.ProposedFishWorldPosition = ProposedFishPosition;
	Result.FishLineAlignment = Alignment;
	Result.NormalizedLineLoad = OutwardLoad;
	Result.RodLineAlignment = RodLineAlignment;
	Result.RodLeverageMultiplier = RodLeverage;
	Result.EffectiveCatStrength = EffectiveCatStrength;
	Result.CombinedCatStrength = CombinedCatStrength;
	Result.ConstraintErrorCentimeters = ConstraintError;
	Result.RelativeConstraintSpeedCentimetersPerSecond = Dt > 0.0
		? (IntendedDistance - Distance0 + RequestedReelDistance) / Dt : 0.0;
	Result.FishConstraintCorrectionCentimeters = FishCorrection;

	const double LineForceDemand = FMath::Max(
		Config.FishStrength * OutwardLoad,
		bReeling ? EffectiveCatStrength : 0.0) * NormalizedTension;
	if (Result.bStrongConfrontation && LineForceDemand >= Config.RodStrength)
	{
		Result.LineBreakCause = ECatFightLineBreakCause::StrengthOverload;
		Result.Outcome = ECatFightStepOutcome::LineBroken;
	}
	else if (!State.bFishExhausted
		&& State.FishStamina - Result.FishStaminaDrain <= UE_DOUBLE_SMALL_NUMBER)
	{
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (Result.AbsoluteRodWear >= Config.RodDurability)
	{
		Result.LineBreakCause = ECatFightLineBreakCause::DurabilityDepleted;
		Result.Outcome = ECatFightStepOutcome::LineBroken;
	}
	else if (FreeDistance > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}

	Result.bSucceeded = true;
	return Result;
}
