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
		&& IsFiniteNonNegative(GetCombinedCatStrength())
		&& IsFiniteNonNegative(PrimaryOperatorMassKilograms)
		&& IsFiniteNonNegative(HelperMassKilograms)
		&& FMath::IsFinite(GetCombinedCatMass()) && GetCombinedCatMass() > 0.0
		&& FMath::IsFinite(FishMassKilograms) && FishMassKilograms > 0.0
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0
		&& FMath::IsFinite(StrengthPerKilogram) && StrengthPerKilogram > 0.0
		&& FMath::IsFinite(ForcePerStrengthNewtons) && ForcePerStrengthNewtons > 0.0
		&& FMath::IsFinite(CatBodyMassKilograms) && CatBodyMassKilograms > 0.0
		&& FMath::IsFinite(ExhaustedReelForceNewtons) && ExhaustedReelForceNewtons > 0.0
		&& IsFiniteNonNegative(ExhaustedCatTowAccelerationCentimetersPerSecondSquared)
		&& FMath::IsFinite(DisplayTensionNewtons) && DisplayTensionNewtons > 0.0
		&& FMath::IsFinite(RodPhysicsLengthCentimeters) && RodPhysicsLengthCentimeters > 0.0
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0
		&& IsFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
		&& IsFiniteNonNegative(CatRodStaminaCostPerStrengthRadian)
		&& IsFiniteNonNegative(CatUnloadedWorkMultiplier)
		&& IsFiniteNonNegative(CatSupportStaminaPerSecond)
		&& IsFiniteNonNegative(FishStaminaCostPerStrengthCentimeter)
		&& IsFiniteNonNegative(IsometricEffortMultiplier)
		&& IsFiniteNonNegative(CatMovementStaminaMultiplier)
		&& IsFiniteNonNegative(CatReelStaminaMultiplier)
		&& IsFiniteNonNegative(CatRodStaminaMultiplier)
		&& IsFiniteNonNegative(CatHoldStaminaMultiplier)
		&& IsFiniteNonNegative(CatLoadStaminaMultiplier)
		&& IsFiniteNonNegative(FishLoadStaminaMultiplier)
		&& FMath::IsFinite(BaseDrainMultiplier) && BaseDrainMultiplier > 0.0
		&& FMath::IsFinite(StruggleDrainMultiplier) && StruggleDrainMultiplier >= BaseDrainMultiplier
		&& IsFiniteNonNegative(SlackStaminaRegenPerSecond)
		&& IsFiniteNonNegative(StalemateRodWearPerFishStrength)
		&& IsFiniteNonNegative(StruggleHoldRodWearPerSecond)
		&& FMath::IsFinite(TautRodWearMultiplier) && TautRodWearMultiplier >= 1.0
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& IsFiniteNonNegative(FishCalmSpeedCentimetersPerSecond)
		&& FMath::IsFinite(FishStruggleSpeedCentimetersPerSecond) && FishStruggleSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(ExhaustedCatEscapeSpeedMultiplier) && ExhaustedCatEscapeSpeedMultiplier >= 1.0
		&& FMath::IsFinite(FishExhaustionThreshold) && FishExhaustionThreshold >= 0.0
		&& FishExhaustionThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationAlignmentThreshold)
		&& StrongConfrontationAlignmentThreshold > 0.0 && StrongConfrontationAlignmentThreshold <= 1.0
		&& FMath::IsFinite(StrongConfrontationConfirmationSeconds)
		&& StrongConfrontationConfirmationSeconds >= 0.0
		&& FMath::IsFinite(AngleStrengthExponent) && AngleStrengthExponent >= 0.1
		&& FMath::IsFinite(MinimumRodLeverageMultiplier)
		&& MinimumRodLeverageMultiplier > 0.0 && MinimumRodLeverageMultiplier <= 1.0
		&& FMath::IsFinite(MaximumFishConstraintCorrectionSpeedCentimetersPerSecond)
		&& MaximumFishConstraintCorrectionSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0
		&& FMath::IsFinite(RodDurability) && RodDurability > 0.0
		&& IsFiniteNonNegative(EscapeSlackCentimeters);
}

bool FCatFishingFightSimulator::ShouldEscapeExhaustedCat(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const bool bRodHeld)
{
	return bRodHeld && State.bOperatorPresent && !State.bFishExhausted && State.FishStamina > 0.0
		&& State.CatStamina == 0.0 && Config.GetCombinedCatStrength() <= 0.0;
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
		|| !IsFiniteVector(State.FishVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.RodTipWorldPosition)
		|| !IsFiniteVector(RodConstraint.RodForwardWorld)
		|| !IsFiniteVector(RodConstraint.RodTipVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierDesiredVelocityCentimetersPerSecond)
		|| !IsFiniteNonNegative(RodConstraint.CatRodExertionSquaredSeconds)
		|| RodConstraint.CatRodExertionSquaredSeconds > Config.FixedStepSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER
		|| !IsFiniteNonNegative(RodConstraint.CatRodPositiveWorkRadians)
		|| (RodConstraint.bRodHeld && RodConstraint.RodForwardWorld.IsNearlyZero())
		|| !IsFiniteVector(DesiredFishDirection)
		|| (!State.bFishExhausted && DesiredFishDirection.IsNearlyZero()))
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
	// 力竭后没有游动意图；到达竿尖正下方时，水平游向为零是合法的收尾状态。
	// 不能为死鱼伪造方向，更不能用活鱼的游向校验终止其会话。
	FVector FishDirection = FVector::ZeroVector;
	if (!State.bFishExhausted)
	{
		FishDirection = FVector(DesiredFishDirection.X, DesiredFishDirection.Y, 0.0);
		if (!FishDirection.Normalize()) return Result;
	}

	const bool bOperatorPresent = State.bOperatorPresent;
	const bool bExhaustedCatEscape = ShouldEscapeExhaustedCat(Config, State, RodConstraint.bRodHeld);
	Result.bExhaustedCatEscape = bExhaustedCatEscape;
	// 力竭拖拽维持锁线；不能靠残留右键在零体力时反复放线回体、恢复全力。
	const bool bReeling = bOperatorPresent && !bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Pull;
	const bool bSlackRecovery = bOperatorPresent && !bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Slack;
	Result.bSlackRecoveryActive = bSlackRecovery;
	const bool bFreeSpool = !bOperatorPresent || bSlackRecovery;
	const bool bStruggling = !State.bFishExhausted
		&& (bExhaustedCatEscape || State.MotionIntent == ECatFishMotionIntent::StrugglingOutward);

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
	const double ActiveFishStrength = State.bFishExhausted ? 0.0 : Config.FishStrength;
	const double CatForce = EffectiveCatStrength * Config.ForcePerStrengthNewtons;
	const double CatDriveAcceleration = 100.0 * CatForce / Config.GetCombinedCatMass();
	const double FishSpeedCap = bStruggling
		? Config.FishStruggleSpeedCentimetersPerSecond : Config.FishCalmSpeedCentimetersPerSecond;
	const double SwimSpeed = State.bFishExhausted ? 0.0 : bExhaustedCatEscape
		? FMath::Max(Config.FishCalmSpeedCentimetersPerSecond, Config.FishStruggleSpeedCentimetersPerSecond)
			* Config.ExhaustedCatEscapeSpeedMultiplier : FishSpeedCap;
	const double FishThrust = SwimSpeed > UE_DOUBLE_SMALL_NUMBER
		? ActiveFishStrength * Config.ForcePerStrengthNewtons + (bExhaustedCatEscape
			? Config.GetCombinedCatMass() * Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared / 100.0 : 0.0) : 0.0;
	const double FishDriveAcceleration = 100.0 * FishThrust / Config.FishMassKilograms;
	// 隐式线性水阻使自由游动渐近性格目标速度，换向保留惯性；不会用力量永久限制小鱼游速。
	const double Drag = FMath::Max(FishThrust, Config.FishStrength * Config.ForcePerStrengthNewtons)
		/ FMath::Max(1.0, State.bFishExhausted ? Config.FishStruggleSpeedCentimetersPerSecond : SwimSpeed) * 100.0;
	const double EffectiveFishMass = Config.FishMassKilograms + Dt * Drag;
	// 力竭收尾保持既定的无自主漂游规则；辅助卷线仍有独立的有限出力。
	const FVector PreviousVelocity = State.bFishExhausted ? FVector::ZeroVector
		: FVector(State.FishVelocityCentimetersPerSecond.X, State.FishVelocityCentimetersPerSecond.Y, 0.0);
	const FVector FreeVelocity = (PreviousVelocity * Config.FishMassKilograms
		+ FishDirection * (100.0 * FishThrust * Dt)) / EffectiveFishMass;
	// 计费意图是鱼主动努力的目标距离，不能因被锁住后速度归零就免除支撑努力。
	const FVector FishIntentDisplacement = FishDirection * SwimSpeed * Dt;
	const FVector FreeFishPosition = State.FishWorldPosition + FreeVelocity * Dt;
	const double FreeDistance = FVector::Distance(RodTip, FreeFishPosition);
	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters, 0.0, Config.MaximumLineLengthCentimeters);
	const double RequestedReelDistance = bReeling && (State.bFishExhausted || CatForce > UE_DOUBLE_SMALL_NUMBER)
		? FMath::Min(Config.ReelSpeedCentimetersPerSecond * Dt, FMath::Max(0.0, PaidOutLine0 - VerticalDistance)) : 0.0;
	const double ReelForceLimit = State.bFishExhausted ? Config.ExhaustedReelForceNewtons : CatForce;
	FVector TowardRod = RodTip - FreeFishPosition;
	TowardRod.Z = 0.0;
	const double HorizontalDistance = TowardRod.Size2D();
	const double MobilityCmPerNewton = 100.0 * Dt * Dt / EffectiveFishMass;
	const auto HorizontalCorrectionForLength = [&](const double Length)
	{
		const double HeightRatio = Length > VerticalDistance ? VerticalDistance / Length : 1.0;
		const double Radius = Length * FMath::Sqrt(FMath::Max(0.0, 1.0 - HeightRatio * HeightRatio));
		return FMath::Max(0.0, HorizontalDistance - Radius);
	};
	const auto RequiredTensionForLength = [&](const double Length)
	{
		const double Correction = HorizontalCorrectionForLength(Length);
		// 约束修正方向上的有限步长平均力臂；竿尖正下方不除以零。
		const double MidRadius = FMath::Max(0.0, HorizontalDistance - Correction * 0.5);
		const double HorizontalFraction = MidRadius / FMath::Max(UE_DOUBLE_SMALL_NUMBER,
			FMath::Sqrt(MidRadius * MidRadius + VerticalDistance * VerticalDistance));
		return Correction / (MobilityCmPerNewton * FMath::Max(0.001, HorizontalFraction));
	};
	// 卷线器和锁线使用同一个张力求解；先检验接收端是否有余力，再提交真正完成的线长。
	double ActualReelDistance = 0.0;
	if (RequestedReelDistance > 0.0 && RequiredTensionForLength(PaidOutLine0) < ReelForceLimit)
	{
		double Low = 0.0, High = RequestedReelDistance;
		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			const double Candidate = (Low + High) * 0.5;
			if (RequiredTensionForLength(PaidOutLine0 - Candidate) <= ReelForceLimit) Low = Candidate;
			else High = Candidate;
		}
		ActualReelDistance = Low;
	}
	double LineLength = PaidOutLine0 - ActualReelDistance;
	if (bFreeSpool) LineLength = FMath::Min(Config.MaximumLineLengthCentimeters, FMath::Max(LineLength, FreeDistance));
	const bool bFreeSpoolReleased = bFreeSpool && FreeDistance <= Config.MaximumLineLengthCentimeters + UE_DOUBLE_KINDA_SMALL_NUMBER;
	// 只使用已经落位的竿尖。意图留给做功识别，绝不在撞墙时预支猫的位移。
	const FVector CarrierIntentDisplacement = RodConstraint.bRodHeld
		? RodConstraint.CarrierDesiredVelocityCentimetersPerSecond * Dt : FVector::ZeroVector;
	const double IntendedDistance = FreeDistance;
	const double ConstraintError = FMath::Max(0.0, FreeDistance - LineLength);
	const bool bLineRestraining = ConstraintError > UE_DOUBLE_KINDA_SMALL_NUMBER;
	const double FullCorrection = HorizontalCorrectionForLength(LineLength);
	const double FishCorrection = bFreeSpoolReleased ? 0.0 : FMath::Min(FullCorrection,
		FMath::Max(Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond, SwimSpeed) * Dt);
	const double LineTension = FullCorrection > UE_DOUBLE_SMALL_NUMBER
		? RequiredTensionForLength(LineLength) * FishCorrection / FullCorrection : 0.0;
	FVector ProposedFishPosition = FreeFishPosition + TowardRod.GetSafeNormal() * FishCorrection;
	const double NormalizedTension = FMath::Clamp(LineTension / Config.DisplayTensionNewtons, 0.0, 1.0);

	const double Distance1 = FVector::Distance(RodTip, ProposedFishPosition);
	const bool bLineTaut = bLineRestraining
		|| LineLength - Distance1 <= UE_DOUBLE_KINDA_SMALL_NUMBER;



	Result.IntendedSwimSpeedCentimetersPerSecond = SwimSpeed;
	Result.RequestedReelDistanceCentimeters = RequestedReelDistance;
	Result.ActualReelDistanceCentimeters = ActualReelDistance;
	Result.LineLengthCentimeters = LineLength;
	Result.TensionCentimeters = ConstraintError;
	Result.StraightLineDistanceCentimeters = Distance1;
	Result.SlackLineLengthCentimeters = FMath::Max(0.0, LineLength - Distance1);
	Result.NormalizedTension = NormalizedTension;
	Result.bLineTaut = bLineTaut;
	Result.ProposedFishWorldPosition = ProposedFishPosition;
	Result.FishEffortDirection = FishDirection;
	Result.FishLineAlignment = Alignment;
	Result.NormalizedLineLoad = OutwardLoad;
	Result.RodLineAlignment = RodLineAlignment;
	Result.RodLeverageMultiplier = RodLeverage;
	Result.EffectiveCatStrength = EffectiveCatStrength;
	Result.CombinedCatStrength = CombinedCatStrength;
	Result.CatDriveAccelerationCentimetersPerSecondSquared = CatDriveAcceleration;
	Result.FishDriveAccelerationCentimetersPerSecondSquared = FishDriveAcceleration;
	Result.LineTensionNewtons = LineTension;
	Result.ConstraintErrorCentimeters = ConstraintError;
	Result.RelativeConstraintSpeedCentimetersPerSecond = Dt > 0.0
		? (IntendedDistance - Distance0 + RequestedReelDistance) / Dt : 0.0;
	Result.FishConstraintCorrectionCentimeters = FishCorrection;

	if (!bExhaustedCatEscape && FreeDistance > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}
	Result.bSucceeded = true;
	return FinalizeResolvedStep(Config, State, RodConstraint, Result) ? Result : FCatFightStepResult{};
}

bool FCatFishingFightSimulator::FinalizeResolvedStep(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
	FCatFightStepResult& Result)
{
	if (!Result.bSucceeded || !Config.IsValid() || Result.ProposedFishWorldPosition.ContainsNaN() || Result.FishEffortDirection.ContainsNaN()
		|| !IsFiniteNonNegative(Result.LineLengthCentimeters) || !IsFiniteNonNegative(Result.LineTensionNewtons)) return false;
	// 该阶段可在地形解析后重新计算，但从未写入 ASC/装备；从输入状态重算，不能叠加临时费用。
	Result.CatStaminaDrain = Result.FishStaminaDrain = Result.FishUncappedStaminaDrain = 0.0;
	Result.CatMovementStaminaDrain = Result.CatReelStaminaDrain = Result.CatRodStaminaDrain = 0.0;
	Result.CatRodWorkStaminaDrain = Result.CatRodSupportStaminaDrain = Result.CatHoldStaminaDrain = 0.0;
	const double Dt = Config.FixedStepSeconds;
	const bool bOperatorPresent = State.bOperatorPresent;
	const bool bExhaustedCatEscape = Result.bExhaustedCatEscape;
	const bool bSlackRecovery = Result.bSlackRecoveryActive;
	const bool bFreeSpool = !bOperatorPresent || bSlackRecovery;
	const bool bReeling = bOperatorPresent && !bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Pull;
	const bool bStruggling = !State.bFishExhausted
		&& (bExhaustedCatEscape || State.MotionIntent == ECatFishMotionIntent::StrugglingOutward);
	const double LineTension = Result.LineTensionNewtons;
	const bool bLineRestraining = LineTension > UE_DOUBLE_SMALL_NUMBER;
	const bool bFreeSpoolReleased = !bLineRestraining;
	const double NormalizedTension = Result.NormalizedTension;
	const double EffectiveCatStrength = Result.EffectiveCatStrength;
	const double CombinedCatStrength = Result.CombinedCatStrength;
	const double ActiveFishStrength = State.bFishExhausted ? 0.0 : Config.FishStrength;
	const double FishThrust = ActiveFishStrength * Config.ForcePerStrengthNewtons;
	const double CatForce = EffectiveCatStrength * Config.ForcePerStrengthNewtons;
	const FVector LineDirection = (Result.ProposedFishWorldPosition - RodConstraint.RodTipWorldPosition)
		.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	const double RodLineAlignment = FVector::DotProduct(RodConstraint.RodForwardWorld.GetSafeNormal(), LineDirection);
	const double Alignment = Result.FishLineAlignment;
	const double OutwardLoad = Result.NormalizedLineLoad;
	const double SwimSpeed = Result.IntendedSwimSpeedCentimetersPerSecond;
	const FVector FishIntentDisplacement = Result.FishEffortDirection * SwimSpeed * Dt;
	const FVector CarrierIntentDisplacement = RodConstraint.bRodHeld
		? RodConstraint.CarrierDesiredVelocityCentimetersPerSecond * Dt : FVector::ZeroVector;
	const double RequestedReelDistance = Result.RequestedReelDistanceCentimeters;
	const double ActualReelDistance = FMath::Clamp(State.LineLengthCentimeters - Result.LineLengthCentimeters, 0.0, RequestedReelDistance);
	const FVector ProposedFishPosition = Result.ProposedFishWorldPosition;
	const bool bLoadedConstraint = bLineRestraining && !bFreeSpoolReleased;
	const bool bPrimaryCanPayEffort = Config.PrimaryOperatorCatStrength > UE_DOUBLE_SMALL_NUMBER;
	const double CatCarrierIntent = bOperatorPresent && RodConstraint.bRodHeld && bLoadedConstraint && bPrimaryCanPayEffort
		? FMath::Max(0.0, -FVector::DotProduct(CarrierIntentDisplacement, LineDirection)) : 0.0;
	const double CatCarrierActual = bOperatorPresent && RodConstraint.bRodHeld
		? FMath::Max(0.0, -FVector::DotProduct(
			RodConstraint.CarrierVelocityCentimetersPerSecond * Dt, LineDirection)) : 0.0;
	// 转杆仅消费转矩积分的支撑时间与真实正功，身体位移只使用 CarrierVelocity。
	const double CatRodExertion = bOperatorPresent && RodConstraint.bRodHeld && bPrimaryCanPayEffort
		? RodConstraint.CatRodExertionSquaredSeconds : 0.0;
	const double CatActiveIntentDistance = RequestedReelDistance + CatCarrierIntent;
	const double FishOutwardIntentDistance = FMath::Max(0.0,
		FVector::DotProduct(FishIntentDisplacement, LineDirection));
	// 保留沿线意图距离供既有约束诊断；支撑费用另按负载与持续时间结算。
	const double CatHoldIntentDistance = bLoadedConstraint && !bFreeSpool
		? FishOutwardIntentDistance * NormalizedTension : 0.0;
	Result.CatMovementIntentCentimeters = CatCarrierIntent;
	Result.CatMovementActualCentimeters = FMath::Min(CatCarrierActual, CatCarrierIntent);
	Result.CatRodExertionSquaredSeconds = CatRodExertion;
	Result.CatRodPositiveWorkRadians = bOperatorPresent && RodConstraint.bRodHeld && bPrimaryCanPayEffort
		? RodConstraint.CatRodPositiveWorkRadians : 0.0;
	Result.CatHoldIntentCentimeters = CatHoldIntentDistance;
	Result.RequestedReelDistanceCentimeters = RequestedReelDistance;
	Result.ActualReelDistanceCentimeters = ActualReelDistance;
	Result.CatIntendedLineDistanceCentimeters = CatActiveIntentDistance + CatHoldIntentDistance;
	Result.CatActualLineDistanceCentimeters = ActualReelDistance + Result.CatMovementActualCentimeters;
	const double FishSignedIntentLineDistance = FVector::DotProduct(
		FishIntentDisplacement, LineDirection);
	const double FishSignedActualLineDistance = FVector::DotProduct(
		ProposedFishPosition - State.FishWorldPosition, LineDirection);
	Result.FishIntendedLineDistanceCentimeters = FMath::Abs(FishSignedIntentLineDistance);
	// 被收线或甩杆强迫拖向意图反方向的位移仍参与位置约束，但不能冒充鱼主动做功。
	Result.FishActualLineDistanceCentimeters = FishSignedIntentLineDistance >= 0.0
		? FMath::Max(0.0, FishSignedActualLineDistance)
		: FMath::Max(0.0, -FishSignedActualLineDistance);
	// 对抗负载按各自可用力量归一化，松线解除约束后为零；相同意图在不同负载下不再等价。
	Result.CatNormalizedEffortLoad = FMath::Clamp(LineTension / FMath::Max(CatForce, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	const double PerpendicularRodLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - RodLineAlignment * RodLineAlignment));
	Result.CatRodNormalizedEffortLoad = FMath::Clamp(LineTension * Config.RodPhysicsLengthCentimeters / 100.0
		* PerpendicularRodLever / FMath::Max(Config.PrimaryOperatorCatStrength * Config.ForcePerStrengthNewtons, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	Result.FishNormalizedEffortLoad = CombinedCatStrength > UE_DOUBLE_SMALL_NUMBER
		? FMath::Max(0.0, Alignment) * FMath::Clamp(LineTension / FMath::Max(FishThrust, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0) : 0.0;

	double IgnoredEffortDistance = 0.0;
	// 鱼力竭后进入纯收尾：仍求解收线和双端位移，但不再向任何猫结算做功消耗。
	if (!bSlackRecovery && !State.bFishExhausted && bOperatorPresent && EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER)
	{
		FCatFightCatWorkInput CatWork;
		// 猫费用不再额外叠加鱼行为阶段倍率；负载变化已经体现在受力观察量里。
		CatWork.UnloadedWorkMultiplier = Config.CatUnloadedWorkMultiplier;
		CatWork.LoadStaminaMultiplier = Config.CatLoadStaminaMultiplier;
		const auto ComputeCatChannel = [&](const double ActualAmount, const double UnitCost, const double Multiplier,
			const double Load, double& OutDrain)
		{
			CatWork.PositiveWorkUnits = Config.StrengthPerKilogram * ActualAmount;
			CatWork.CostPerWorkUnit = UnitCost;
			CatWork.ActionMultiplier = Multiplier;
			CatWork.NormalizedLoad = Load;
			return FCatFishingFightWorkModel::ComputeCatWorkDrain(CatWork, OutDrain);
		};
		if (!ComputeCatChannel(Result.CatMovementActualCentimeters, Config.CatStaminaCostPerStrengthCentimeter,
			Config.CatMovementStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatMovementStaminaDrain)
			|| !ComputeCatChannel(ActualReelDistance, Config.CatStaminaCostPerStrengthCentimeter,
				Config.CatReelStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatReelStaminaDrain)
			|| !ComputeCatChannel(Result.CatRodPositiveWorkRadians, Config.CatRodStaminaCostPerStrengthRadian,
				Config.CatRodStaminaMultiplier, Result.CatRodNormalizedEffortLoad, Result.CatRodWorkStaminaDrain))
		{
			return false;
		}
		// 共享沿线支撑先按负载平方和真实步长结算；主位的转杆仅承担超出共享支撑的部分。
		// 实际做功费用不能抵扣支撑，从而不会因微动或无力主位的不可支付动作而免掉助手费用。
		Result.CatHoldStaminaDrain = bLoadedConstraint && !bFreeSpool
			? Config.CatSupportStaminaPerSecond * Dt * FMath::Square(Result.CatNormalizedEffortLoad)
				* Config.CatHoldStaminaMultiplier : 0.0;
		const double RodSupport = Config.CatSupportStaminaPerSecond
			* Result.CatRodExertionSquaredSeconds * Config.CatRodStaminaMultiplier;
		if (!IsFiniteNonNegative(RodSupport) || !IsFiniteNonNegative(Result.CatHoldStaminaDrain))
		{
			return false;
		}
		Result.CatRodSupportStaminaDrain = FMath::Max(0.0, RodSupport - Result.CatHoldStaminaDrain);
		Result.CatRodStaminaDrain = Result.CatRodWorkStaminaDrain + Result.CatRodSupportStaminaDrain;
		Result.CatStaminaDrain = Result.CatMovementStaminaDrain + Result.CatReelStaminaDrain
			+ Result.CatRodStaminaDrain + Result.CatHoldStaminaDrain;
		if (!IsFiniteNonNegative(Result.CatStaminaDrain))
		{
			return false;
		}
	}
	// 正常右键期间独立回体，移动、转杆和最大线长处的张力均不产生双方费用。
	// 无人值守放线不恢复旧操作手；零体力强制拖拽也不通过右键退出。
	if (bSlackRecovery)
	{
		Result.CatStaminaDrain = -FMath::Min(FMath::Max(0.0, Config.CatStaminaMaximum - State.CatStamina),
			Config.SlackStaminaRegenPerSecond * Dt);
	}

	if (!bSlackRecovery && !State.bFishExhausted && State.FishStamina > 0.0
		&& Result.FishIntendedLineDistanceCentimeters > UE_DOUBLE_SMALL_NUMBER)
	{
		FCatFightWorkInput FishWork;
		// 鱼只为对抗负载付费；自由游动、放线和没有可用猫合力的游动不产生基础耗体。
		FishWork.BaseEffortMultiplier = 0.0;
		FishWork.Strength = Config.StrengthPerKilogram;
		FishWork.IntendedLineDistanceCentimeters = Result.FishIntendedLineDistanceCentimeters;
		FishWork.ActualLineDistanceCentimeters = Result.FishActualLineDistanceCentimeters;
		FishWork.IsometricEffortMultiplier = Config.IsometricEffortMultiplier;
		FishWork.CostPerStrengthCentimeter = Config.FishStaminaCostPerStrengthCentimeter;
		FishWork.PhaseMultiplier = bStruggling ? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
		FishWork.NormalizedLoad = Result.FishNormalizedEffortLoad;
		FishWork.LoadStaminaMultiplier = Config.FishLoadStaminaMultiplier;
		if (!FCatFishingFightWorkModel::ComputeDrain(FishWork, Result.FishStaminaDrain, IgnoredEffortDistance))
		{
			return false;
		}
		Result.FishUncappedStaminaDrain = Result.FishStaminaDrain;
		Result.FishStaminaDrain = FMath::Min(Result.FishStaminaDrain, State.FishStamina);
		// 无负载或费用关闭时不能仅因剩余体力低于阈值而把鱼判为力竭。
		if (Result.FishStaminaDrain > 0.0
			&& State.FishStamina - Result.FishStaminaDrain <= Config.FishExhaustionThreshold)
		{
			Result.FishStaminaDrain = State.FishStamina;
		}
	}

	const bool bConfrontationCandidate = !State.bFishExhausted && !bExhaustedCatEscape && bLineRestraining
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

	const double FishLineForce = ActiveFishStrength * OutwardLoad;
	const double CatLineForce = bReeling ? EffectiveCatStrength : 0.0;
	// LineLoad 是鱼主动沿线向外施力的投影，也是鱼竿磨损的唯一方向负载。
	// Tension 只说明几何约束已经介入，不能在鱼回头或横游时替代 LineLoad，
	// 否则猫端收线制造的张力会让低负载帧继续按满负载磨线。
	const double WearLoad = OutwardLoad;
	// 鱼力竭后的收尾只保留线长约束和拖拽位移；死鱼不再施力，
	// 猫的收线力也不能独自制造鱼竿磨损，否则拉鱼干仍会耗尽耐久。
	// 拖落水期间不新增磨损，避免尚未落水就被断竿替代。
	const double RodWearDelta = !State.bFishExhausted && !bExhaustedCatEscape && bLineRestraining
		? (FMath::Max(FishLineForce, CatLineForce) * Config.StalemateRodWearPerFishStrength
			+ (bStruggling ? Config.StruggleHoldRodWearPerSecond : 0.0))
			* WearLoad * Dt * Config.TautRodWearMultiplier : 0.0;
	Result.RodWearDelta = RodWearDelta;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta;

	const bool bEscaped = Result.Outcome == ECatFightStepOutcome::Escaped;
	Result.Outcome = ECatFightStepOutcome::None;
	if (!State.bFishExhausted && Result.AbsoluteRodWear >= Config.RodDurability)
		Result.Outcome = ECatFightStepOutcome::RodBroken;
	else if (!State.bFishExhausted && (Result.bFishBeached || State.FishStamina - Result.FishStaminaDrain <= 0.0))
	{
		Result.FishStaminaDrain = State.FishStamina;
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (bEscaped) Result.Outcome = ECatFightStepOutcome::Escaped;
	// 岸线/坡面可能改变最终力臂；猫与杆都必须消费最终线方向上的同一张力。
	Result.NetFishPullAccelerationCentimetersPerSecondSquared = 100.0
		* FMath::Max(0.0, LineTension * LineDirection.Size2D() - CatForce) / Config.GetCombinedCatMass();
	Result.CarrierPullAccelerationCentimetersPerSecondSquared = RodConstraint.bRodHeld && bOperatorPresent
		&& !State.bFishExhausted && Result.Outcome == ECatFightStepOutcome::None
		? Result.NetFishPullAccelerationCentimetersPerSecondSquared : 0.0;
	Result.CarrierTargetPullSpeedCentimetersPerSecond = Result.CarrierPullAccelerationCentimetersPerSecondSquared > UE_DOUBLE_KINDA_SMALL_NUMBER
		? (bExhaustedCatEscape ? SwimSpeed : Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond) : 0.0;
	return IsFiniteNonNegative(Result.AbsoluteRodWear) && IsFiniteNonNegative(Result.FishStaminaDrain)
		&& FMath::IsFinite(Result.CatStaminaDrain)
		&& IsFiniteNonNegative(Result.CarrierPullAccelerationCentimetersPerSecondSquared);
}
