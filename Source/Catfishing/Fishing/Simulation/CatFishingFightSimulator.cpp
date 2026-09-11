#include "Fishing/Simulation/CatFishingFightSimulator.h"

#include "Physics/Simulation/CatIntentMotionModel.h"
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
		&& FMath::IsFinite(PrimaryOperatorMassKilograms) && PrimaryOperatorMassKilograms > 0.0
		&& FMath::IsFinite(FishMassKilograms) && FishMassKilograms > 0.0
		&& FMath::IsFinite(FishStrength) && FishStrength > 0.0
		&& FMath::IsFinite(StrengthPerKilogram) && StrengthPerKilogram > 0.0
		&& FMath::IsFinite(ForcePerStrengthNewtons) && ForcePerStrengthNewtons > 0.0
		&& FMath::IsFinite(ExhaustedReelForceNewtons) && ExhaustedReelForceNewtons > 0.0
		&& IsFiniteNonNegative(ExhaustedCatTowAccelerationCentimetersPerSecondSquared)
		&& FMath::IsFinite(DisplayTensionNewtons) && DisplayTensionNewtons > 0.0
		&& FMath::IsFinite(RodPhysicsLengthCentimeters) && RodPhysicsLengthCentimeters > 0.0
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0
		&& IsFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
		&& IsFiniteNonNegative(CatRodStaminaCostPerStrengthRadian)
		&& IsFiniteNonNegative(CatUnloadedWorkMultiplier)
		&& IsFiniteNonNegative(CatSupportStaminaPerSecond)
		&& IsFiniteNonNegative(FishStaminaPerUnfulfilledMeter)
		&& IsFiniteNonNegative(CatMovementStaminaMultiplier)
		&& IsFiniteNonNegative(CatReelStaminaMultiplier)
		&& IsFiniteNonNegative(CatRodStaminaMultiplier)
		&& IsFiniteNonNegative(CatHoldStaminaMultiplier)
		&& IsFiniteNonNegative(CatLoadStaminaMultiplier)
		&& IsFiniteNonNegative(SlackStaminaRegenPerSecond)
		&& IsFiniteNonNegative(StalemateRodWearPerFishStrength)
		&& IsFiniteNonNegative(FishFullEffortRodWearPerSecond)
		&& FMath::IsFinite(TautRodWearMultiplier) && TautRodWearMultiplier >= 1.0
		&& FMath::IsFinite(ReelSpeedCentimetersPerSecond) && ReelSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(FishFullEffortSpeedCentimetersPerSecond) && FishFullEffortSpeedCentimetersPerSecond > 0.0
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

bool FCatFishingFightSimulator::IsLineAtMaximum(const FCatFightSimulationConfig& Config,
	const double LineLengthCentimeters)
{
	return LineLengthCentimeters >= Config.MaximumLineLengthCentimeters - UE_DOUBLE_KINDA_SMALL_NUMBER;
}

bool FCatFishingFightSimulator::ShouldEscapeExhaustedCat(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const bool bRodHeld)
{
	return bRodHeld && State.bOperatorPresent && !State.bFishExhausted && State.FishStamina > 0.0
		&& State.CatStamina == 0.0;
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
	if (!Config.IsValid())
	{
		Result.RejectReason = ECatFightSimulationRejectReason::InvalidConfig;
		Result.Trace.RejectReason = Result.RejectReason;
		return Result;
	}
	if (!FMath::IsFinite(State.CatStamina) || State.CatStamina < 0.0
		|| !FMath::IsFinite(State.FishStamina) || State.FishStamina < 0.0
		|| !FMath::IsFinite(State.FishEffortRatio) || State.FishEffortRatio < 0.0 || State.FishEffortRatio > 1.0
		|| !FMath::IsFinite(State.LineLengthCentimeters) || State.LineLengthCentimeters < 0.0
		|| !FMath::IsFinite(State.AbsoluteRodWear) || State.AbsoluteRodWear < 0.0
		|| !FMath::IsFinite(State.StrongConfrontationBuildUpSeconds)
		|| State.StrongConfrontationBuildUpSeconds < 0.0
		|| !IsFiniteVector(State.FishWorldPosition)
		|| !IsFiniteVector(State.FishVelocityCentimetersPerSecond))
	{
		Result.RejectReason = ECatFightSimulationRejectReason::InvalidState;
		Result.Trace.RejectReason = Result.RejectReason;
		return Result;
	}
	if (!IsFiniteVector(RodConstraint.RodTipWorldPosition)
		|| !IsFiniteVector(RodConstraint.RodForwardWorld)
		|| !IsFiniteVector(RodConstraint.RodTipVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.RodTipAccelerationCentimetersPerSecondSquared)
		|| !IsFiniteVector(RodConstraint.PreviousLineForceNewtons)
		|| !IsFiniteNonNegative(RodConstraint.PendingLineResponseSeconds)
		|| !IsFiniteVector(RodConstraint.PendingLineImpulseNewtonSeconds)
		|| !IsFiniteVector(RodConstraint.PendingLinePositionMomentNewtonSecondsSquared)
		|| !IsFiniteVector(RodConstraint.RodPointInverseMassX) || !IsFiniteVector(RodConstraint.RodPointInverseMassY)
		|| !IsFiniteVector(RodConstraint.RodPointInverseMassZ) || !IsFiniteNonNegative(RodConstraint.PhysicsStepSeconds)
		|| !FMath::IsFinite(RodConstraint.CatSupportAlignment)
		|| RodConstraint.CatSupportAlignment < -1.0 || RodConstraint.CatSupportAlignment > 1.0
		|| !IsFiniteNonNegative(RodConstraint.CatRodExertionSquaredSeconds)
		|| RodConstraint.CatRodExertionSquaredSeconds > Config.FixedStepSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER
		|| !IsFiniteNonNegative(RodConstraint.CatRodPositiveWorkRadians)
		|| (RodConstraint.bRodHeld && RodConstraint.RodForwardWorld.IsNearlyZero()))
	{
		Result.RejectReason = ECatFightSimulationRejectReason::InvalidRodConstraint;
		Result.Trace.RejectReason = Result.RejectReason;
		return Result;
	}
	if (!IsFiniteVector(DesiredFishDirection)
		|| (!State.bFishExhausted && DesiredFishDirection.IsNearlyZero()))
	{
		Result.RejectReason = ECatFightSimulationRejectReason::InvalidFishDirection;
		Result.Trace.RejectReason = Result.RejectReason;
		return Result;
	}

	const double Dt = Config.FixedStepSeconds;
	Result.Trace.bInputAccepted = true;
	Result.Trace.FixedStepSeconds = Dt;
	Result.Trace.OperatorBodyMassKilograms = Config.PrimaryOperatorMassKilograms;
	const FVector RodTip = RodConstraint.RodTipWorldPosition;
	const FVector FromRod = State.FishWorldPosition - RodTip;
	const double Distance0 = FromRod.Size();
	const double VerticalDistance = FMath::Abs(FromRod.Z);
	Result.Trace.DistanceBeforeCentimeters = Distance0;
	Result.Trace.VerticalDistanceCentimeters = VerticalDistance;
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
		if (!FishDirection.Normalize())
		{
			Result.RejectReason = ECatFightSimulationRejectReason::InvalidFishDirection;
			Result.Trace.bInputAccepted = false;
			Result.Trace.RejectReason = Result.RejectReason;
			return Result;
		}
	}

	const bool bOperatorPresent = State.bOperatorPresent;
	const bool bExhaustedCatEscape = ShouldEscapeExhaustedCat(Config, State, RodConstraint.bRodHeld);
	Result.bExhaustedCatEscape = bExhaustedCatEscape;
	// 力竭拖拽维持锁线；不能靠残留右键在零体力时反复放线回体、恢复全力。
	const bool bReeling = bOperatorPresent && !bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Pull;
	const bool bSlackRecovery = bOperatorPresent && !bExhaustedCatEscape
		&& State.CatAction == ECatFightCatAction::Slack && !IsLineAtMaximum(Config, State.LineLengthCentimeters);
	Result.bSlackRecoveryActive = bSlackRecovery;
	const bool bFreeSpool = !bOperatorPresent || bSlackRecovery;
	const bool bStruggling = !State.bFishExhausted
		&& (bExhaustedCatEscape || State.MotionIntent == ECatFishMotionIntent::StrugglingOutward);
	Result.Trace.bFreeSpool = bFreeSpool;
	Result.Trace.bReeling = bReeling;
	Result.Trace.bStruggling = bStruggling;

	const double Alignment = bHasRadialBasis
		? FMath::Clamp(FVector::DotProduct(FishDirection, HorizontalOutward), -1.0, 1.0) : 0.0;
	const double OutwardLoad = FMath::Pow(FMath::Max(0.0, Alignment), Config.AngleStrengthExponent);
	Result.Trace.FishAlignment = Alignment;
	Result.Trace.NormalizedLineLoad = OutwardLoad;
	const FVector LineDirection = FromRod.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, HorizontalOutward);
	const FVector RodForward = RodConstraint.RodForwardWorld.GetSafeNormal();
	const double RodLineAlignment = RodConstraint.bRodHeld
		? FMath::Clamp(FVector::DotProduct(RodForward, LineDirection), 0.0, 1.0) : 1.0;
	const double RodLeverage = RodConstraint.bRodHeld
		? FMath::Lerp(Config.MinimumRodLeverageMultiplier, 1.0, RodLineAlignment) : 1.0;
	const double OperatorCatStrength = bOperatorPresent ? Config.PrimaryOperatorCatStrength : 0.0;
	const double SignedCatSupportStrength = OperatorCatStrength * RodLeverage * RodConstraint.CatSupportAlignment;
	const double EffectiveCatStrength = FMath::Max(0.0, SignedCatSupportStrength);
	const double FishEffortRatio = State.bFishExhausted ? 0.0 : bExhaustedCatEscape ? 1.0 : State.FishEffortRatio;
	const double ActiveFishStrength = Config.FishStrength * FishEffortRatio;
	const double FullEffortThrust = Config.FishStrength * Config.ForcePerStrengthNewtons;
	const double CatForce = SignedCatSupportStrength * Config.ForcePerStrengthNewtons;
	const double CatDriveAcceleration = 100.0 * CatForce / Config.PrimaryOperatorMassKilograms;
	const double SwimSpeed = State.bFishExhausted ? 0.0 : bExhaustedCatEscape
		? Config.FishFullEffortSpeedCentimetersPerSecond * Config.ExhaustedCatEscapeSpeedMultiplier
		: Config.FishFullEffortSpeedCentimetersPerSecond * FishEffortRatio;
	const double FishThrust = ActiveFishStrength * Config.ForcePerStrengthNewtons + (bExhaustedCatEscape
		? Config.PrimaryOperatorMassKilograms * Config.ExhaustedCatTowAccelerationCentimetersPerSecondSquared / 100.0 : 0.0);
	const double FishDriveAcceleration = 100.0 * FishThrust / Config.FishMassKilograms;
	Result.Trace.RodLineAlignment = RodLineAlignment;
	Result.Trace.RodLeverageMultiplier = RodLeverage;
	Result.Trace.OperatorCatStrength = OperatorCatStrength;
	Result.Trace.EffectiveCatStrength = EffectiveCatStrength;
	Result.Trace.ActiveFishStrength = ActiveFishStrength;
	Result.Trace.CatForceNewtons = CatForce;
	Result.Trace.FishThrustNewtons = FishThrust;
	Result.Trace.CatDriveAccelerationCentimetersPerSecondSquared = CatDriveAcceleration;
	Result.Trace.FishDriveAccelerationCentimetersPerSecondSquared = FishDriveAcceleration;
	Result.Trace.FishFullEffortSpeedCentimetersPerSecond = Config.FishFullEffortSpeedCentimetersPerSecond;
	Result.Trace.FishEffortRatio = FishEffortRatio;
	Result.Trace.FishFullEffortThrustNewtons = FullEffortThrust;
	Result.Trace.FishStaminaPerUnfulfilledMeter = Config.FishStaminaPerUnfulfilledMeter;
	Result.Trace.SwimSpeedCentimetersPerSecond = SwimSpeed;
	// 正常水阻固定按满出力校准，不能随u缩小抵消降力。零体力拖水保留独立的辅助推力/速度政策。
	const double Drag = bExhaustedCatEscape
		? FishThrust / FMath::Max(1.0, SwimSpeed) * 100.0
		: FullEffortThrust / FMath::Max(1.0, Config.FishFullEffortSpeedCentimetersPerSecond) * 100.0;
	Result.Trace.FishLinearDragKilogramsPerSecond = Drag;
	const double EffectiveFishMass = Config.FishMassKilograms + Dt * Drag;
	// 力竭收尾保持既定的无自主漂游规则；辅助卷线仍有独立的有限出力。
	const FVector PreviousVelocity = State.bFishExhausted ? FVector::ZeroVector
		: FVector(State.FishVelocityCentimetersPerSecond.X, State.FishVelocityCentimetersPerSecond.Y, 0.0);
	const FVector FreeVelocity = (PreviousVelocity * Config.FishMassKilograms
		+ FishDirection * (100.0 * FishThrust * Dt)) / EffectiveFishMass;
	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters, 0.0, Config.MaximumLineLengthCentimeters);
	const auto RadiusAtHeight = [](const double Length, const double Height)
	{
		return FMath::Sqrt(FMath::Max(0.0, Length * Length - Height * Height));
	};
	// 历史几何误差单独回收，不能把位置纠偏伪装成新冲量，再写回鱼的惯性。
	const double ExistingHorizontalError = !bFreeSpool
		? FMath::Max(0.0, FromRod.Size2D() - RadiusAtHeight(PaidOutLine0, VerticalDistance)) : 0.0;
	const bool bCMC = bool(RodConstraint.PredictCMCEndpoint);
	const double PositionCorrection = RodConstraint.bPhysicalRodEndpoint && !bCMC ? 0.0 : FMath::Min(ExistingHorizontalError,
		Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond * Dt);
	const FVector ForceStart = RodConstraint.bPhysicalRodEndpoint && !bCMC ? State.FishWorldPosition : State.FishWorldPosition - HorizontalOutward * ExistingHorizontalError;
	const FVector ResidualPositionError = RodConstraint.bPhysicalRodEndpoint && !bCMC ? FVector::ZeroVector : HorizontalOutward * (ExistingHorizontalError - PositionCorrection);
	const FVector FreeFishPosition = ForceStart + FreeVelocity * Dt;
	const double FreeDistance = FVector::Distance(RodTip, FreeFishPosition + ResidualPositionError);
	const double RequestedReelDistance = bReeling && (State.bFishExhausted || CatForce > UE_DOUBLE_SMALL_NUMBER)
		? FMath::Min(Config.ReelSpeedCentimetersPerSecond * Dt, FMath::Max(0.0, PaidOutLine0 - VerticalDistance)) : 0.0;
	const double ReelForceLimit = State.bFishExhausted ? Config.ExhaustedReelForceNewtons : FMath::Max(0.0, CatForce);
	const double HorizontalDistance = FVector::Dist2D(RodTip, FreeFishPosition);
	const double MobilityCmPerNewton = 100.0 * Dt * Dt / EffectiveFishMass;
	Result.Trace.HorizontalDistanceCentimeters = HorizontalDistance;
	Result.Trace.MobilityCentimetersPerNewton = MobilityCmPerNewton;
	Result.Trace.FishPositionCorrectionCentimeters = PositionCorrection;
	struct FLineSolve
	{
		double Tension = 0.0;
		FVector Force = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Position = FVector::ZeroVector;
		FVector RodEnd = FVector::ZeroVector;
	};
	const auto ApplyPointResponse = [&](const FVector& Force)
	{
		return RodConstraint.RodPointInverseMassX * Force.X + RodConstraint.RodPointInverseMassY * Force.Y
			+ RodConstraint.RodPointInverseMassZ * Force.Z;
	};
	const double EndpointPositionFactor = 0.5 + 0.5 * FMath::Clamp(RodConstraint.PhysicsStepSeconds / Dt, 0.0, 1.0);
	const FVector ExternalEndpointAcceleration = RodConstraint.RodTipAccelerationCentimetersPerSecondSquared
		- ApplyPointResponse(RodConstraint.PreviousLineForceNewtons) * 100.0;
	const double PendingSeconds = RodConstraint.PendingLineResponseSeconds;
	const FVector QueuedEndpoint = RodTip + RodConstraint.RodTipVelocityCentimetersPerSecond * PendingSeconds
		+ ExternalEndpointAcceleration * (0.5 * PendingSeconds * PendingSeconds)
		+ ApplyPointResponse(RodConstraint.PendingLinePositionMomentNewtonSecondsSquared) * 100.0;
	const FVector QueuedVelocity = RodConstraint.RodTipVelocityCentimetersPerSecond + ExternalEndpointAcceleration * PendingSeconds
		+ ApplyPointResponse(RodConstraint.PendingLineImpulseNewtonSeconds) * 100.0;
	const FVector FreePhysicalEndpoint = QueuedEndpoint + QueuedVelocity * Dt
		+ ExternalEndpointAcceleration * (EndpointPositionFactor * Dt * Dt);
    bool bCandidateSucceeded = true;
    const double CMCTravelLimit = bCMC && RodConstraint.GetCMCTravelLimit ? RodConstraint.GetCMCTravelLimit(HorizontalOutward,100000.0) : 0.0;
    Result.Trace.bCMCEndpointPredicted = bCMC;
	const auto SolveForLength = [&](const double Length)
	{
        FLineSolve Solved;
        if (bCMC)
        {
            // 7e606dd: one candidate tension drives fish, collision-bounded CMC motion and the same aim model.
            const FVector Offset = FreeFishPosition - RodTip;
            const FVector Axis = Offset.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, HorizontalOutward);
            const double Radius = RadiusAtHeight(Length, FMath::Abs(Offset.Z));
            const double MidRadius = .5 * (Offset.Size2D() + FMath::Min(Offset.Size2D(),Radius));
            const double HorizontalFraction = FMath::Max(.001, MidRadius / FMath::Max(UE_DOUBLE_SMALL_NUMBER,
                FMath::Sqrt(MidRadius*MidRadius + Offset.Z*Offset.Z)));
            const FVector ForceAxis = Axis * HorizontalFraction + FVector(0,0,FMath::Sign(Offset.Z)*FMath::Sqrt(1-HorizontalFraction*HorizontalFraction));
            const auto Evaluate = [&](double Tension)
            {
                Solved.Tension = Tension; Solved.Force = ForceAxis * Tension;
                Solved.Velocity = FreeVelocity - Axis * (100*Tension*HorizontalFraction*Dt/EffectiveFishMass);
                Solved.Position = ForceStart + Solved.Velocity * Dt;
                FCatFightCMCPredictionQuery Query;
                Query.ForceNewtons = Solved.Force; Query.Seconds = Dt;
                Query.TorqueStrengthMetersPerNewton = Config.RodPhysicsLengthCentimeters / (100*Config.ForcePerStrengthNewtons);
                Query.TravelAxis = HorizontalOutward; Query.TravelLimitCentimeters = CMCTravelLimit;
                const auto Predicted = RodConstraint.PredictCMCEndpoint(Query);
                bCandidateSucceeded &= Predicted.bSucceeded;
                Solved.RodEnd = Predicted.bSucceeded ? Predicted.RodTipWorldPosition : RodTip;
                const FVector Separation = Solved.Position - Solved.RodEnd;
                const double EndRadius = RadiusAtHeight(Length,FMath::Abs(Separation.Z));
                const double Along = FVector::DotProduct(Separation,Axis);
                const double AcrossSquared = FMath::Max(0.0,Separation.SizeSquared2D()-Along*Along);
                return Along-FMath::Sqrt(FMath::Max(0.0,EndRadius*EndRadius-AcrossSquared));
            };
            if (Evaluate(0)>UE_DOUBLE_SMALL_NUMBER)
            {
                double Low=0, High=(Offset.Size2D()+FMath::Max(0.0,-FVector::DotProduct(RodConstraint.CarrierVelocityCentimetersPerSecond,Axis)*Dt))
                    / (MobilityCmPerNewton*HorizontalFraction);
                for(int32 I=0;I<40;++I) { const double Mid=(Low+High)*.5; if(Evaluate(Mid)>0) Low=Mid; else High=Mid; }
                Evaluate(High);
            }
            Solved.Position += ResidualPositionError;
            return Solved;
        }
        if (RodConstraint.bPhysicalRodEndpoint)
		{
			const FVector FreeOffset = FreeFishPosition - FreePhysicalEndpoint;
			const FVector Axis = FreeOffset.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, LineDirection);
			const FVector FishResponse = FVector(Axis.X, Axis.Y, 0) * MobilityCmPerNewton;
			const FVector RodResponse = ApplyPointResponse(Axis) * (100.0 * EndpointPositionFactor * Dt * Dt);
			const FVector RelativeResponse = FishResponse + RodResponse;
			const double ErrorSquared = FreeOffset.SizeSquared() - Length * Length;
			if (ErrorSquared > 0 && RelativeResponse.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
			{
				const double Projection = FVector::DotProduct(FreeOffset, RelativeResponse);
				const double Discriminant = Projection * Projection - RelativeResponse.SizeSquared() * ErrorSquared;
				if (Projection > 0)
					Solved.Tension = Discriminant >= 0
						? ErrorSquared / (Projection + FMath::Sqrt(Discriminant))
						: Projection / RelativeResponse.SizeSquared();
			}
			Solved.Force = Axis * Solved.Tension;
			Solved.Velocity = FreeVelocity - FishResponse * (Solved.Tension / Dt);
			Solved.Position = State.FishWorldPosition + Solved.Velocity * Dt;
			Solved.RodEnd = RodTip; // Observation only. No predicted transform is committed to Chaos.
			return Solved;
		}
		const FVector FreeOffset = FreeFishPosition - RodTip;
		const FVector Axis = FreeOffset.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, HorizontalOutward);
		const double Radius = RadiusAtHeight(Length, FMath::Abs(FreeOffset.Z));
		const double MidRadius = 0.5 * (FreeOffset.Size2D() + FMath::Min(FreeOffset.Size2D(), Radius));
		const double HorizontalFraction = FMath::Max(0.001, MidRadius / FMath::Max(UE_DOUBLE_SMALL_NUMBER,
			FMath::Sqrt(MidRadius * MidRadius + FreeOffset.Z * FreeOffset.Z)));
		Solved.Tension = FMath::Max(0.0, FreeOffset.Size2D() - Radius) / (MobilityCmPerNewton * HorizontalFraction);
		Solved.Force = (FreeFishPosition - RodTip).GetSafeNormal() * Solved.Tension;
		Solved.Velocity = FreeVelocity - Axis * (100.0 * Solved.Tension * HorizontalFraction * Dt / EffectiveFishMass);
		Solved.Position = ForceStart + Solved.Velocity * Dt + ResidualPositionError;
		// Static-anchor callers retain the sampled endpoint. No hypothetical pose is committed.
		Solved.RodEnd = RodTip;
		return Solved;
	};
	// 卷线器和锁线使用同一个张力求解；先检验接收端是否有余力，再提交真正完成的线长。
	double ActualReelDistance = 0.0;
	if (RequestedReelDistance > 0.0 && SolveForLength(PaidOutLine0).Tension < ReelForceLimit)
	{
		double Low = 0.0, High = RequestedReelDistance;
		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			const double Candidate = (Low + High) * 0.5;
			if (SolveForLength(PaidOutLine0 - Candidate).Tension <= ReelForceLimit) Low = Candidate;
			else High = Candidate;
		}
		ActualReelDistance = Low;
	}
	double LineLength = PaidOutLine0 - ActualReelDistance;
	if (bFreeSpool) LineLength = FMath::Min(Config.MaximumLineLengthCentimeters, FMath::Max(LineLength, FreeDistance));
	const bool bFreeSpoolReleased = bFreeSpool && FreeDistance <= Config.MaximumLineLengthCentimeters + UE_DOUBLE_KINDA_SMALL_NUMBER;
	const double IntendedDistance = FreeDistance;
	const double ConstraintError = FMath::Max(0.0, FreeDistance - LineLength);
	FLineSolve Solved = SolveForLength(LineLength);
    if (!bCandidateSucceeded)
    {
        Result.RejectReason = Result.Trace.RejectReason = ECatFightSimulationRejectReason::InvalidRodConstraint;
        return Result;
    }
	if (bFreeSpoolReleased)
	{
		Solved.Tension = 0.0;
		Solved.Force = FVector::ZeroVector;
		Solved.Velocity = FreeVelocity;
		Solved.Position = State.FishWorldPosition + FreeVelocity * Dt;
		Solved.RodEnd = RodTip;
	}
	const double LineTension = Solved.Tension;
	const bool bLineRestraining = LineTension > UE_DOUBLE_SMALL_NUMBER;
	const double FishCorrection = PositionCorrection + (FreeVelocity - Solved.Velocity).Size() * Dt;
	Result.Trace.ExistingPositionErrorCentimeters = ExistingHorizontalError;
	Result.Trace.FishCorrectionCentimeters = FishCorrection;
	Result.Trace.LineTensionNewtons = LineTension;
	Result.Trace.RequiredTensionAtCurrentLengthNewtons = SolveForLength(LineLength).Tension;
	Result.Trace.RequiredTensionAtPaidOutLengthNewtons = SolveForLength(PaidOutLine0).Tension;
	Result.Trace.ReelForceLimitNewtons = ReelForceLimit;
	Result.Trace.bLineRestraining = bLineRestraining;
	const FVector ProposedFishPosition = Solved.Position;
	Result.ResolvedFishVelocityCentimetersPerSecond = Solved.Velocity;
	Result.FishPositionCorrectionWorldDisplacement = -HorizontalOutward * PositionCorrection;
	Result.Trace.ConstraintRodEndWorldPosition = Solved.RodEnd;
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
	Result.OperatorCatStrength = OperatorCatStrength;
	Result.CatDriveAccelerationCentimetersPerSecondSquared = CatDriveAcceleration;
	Result.FishDriveAccelerationCentimetersPerSecondSquared = FishDriveAcceleration;
	Result.LineTensionNewtons = LineTension;
	Result.RodLineForceNewtons = Solved.Force;
	Result.ConstraintErrorCentimeters = ConstraintError;
	Result.RelativeConstraintSpeedCentimetersPerSecond = Dt > 0.0
		? (IntendedDistance - Distance0 + RequestedReelDistance) / Dt : 0.0;
	Result.FishConstraintCorrectionCentimeters = FishCorrection;

	if (!bExhaustedCatEscape && FreeDistance > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}
	Result.bSucceeded = true;
	if (!FinalizeResolvedStep(Config, State, RodConstraint, Result))
	{
		Result.bSucceeded = false;
		return Result;
	}
	return Result;
}

bool FCatFishingFightSimulator::FinalizeResolvedStep(const FCatFightSimulationConfig& Config,
	const FCatFightSimulationState& State, const FCatFightRodConstraintInput& RodConstraint,
	FCatFightStepResult& Result)
{
	const auto RejectResolvedResult = [&]()
	{
		// 保留诊断快照，但恢复旧有的失败结果契约：任何数值输出都必须是有限的默认值。
		const FCatFightSimulationTrace Trace = Result.Trace;
		Result = FCatFightStepResult{};
		Result.RejectReason = ECatFightSimulationRejectReason::InvalidResolvedResult;
		Result.Trace = Trace;
		Result.Trace.RejectReason = Result.RejectReason;
		Result.Trace.bFinalizeInputAccepted = false;
		return false;
	};
	if (!Result.bSucceeded || !Config.IsValid() || Result.ProposedFishWorldPosition.ContainsNaN() || Result.FishEffortDirection.ContainsNaN()
		|| !FMath::IsFinite(State.FishEffortRatio) || State.FishEffortRatio < 0.0 || State.FishEffortRatio > 1.0
		|| !IsFiniteVector(Result.ResolvedFishVelocityCentimetersPerSecond)
		|| !IsFiniteVector(Result.RodLineForceNewtons)
		|| !IsFiniteVector(Result.FishPositionCorrectionWorldDisplacement)
		|| !FMath::IsFinite(RodConstraint.CatSupportAlignment)
		|| RodConstraint.CatSupportAlignment < -1.0 || RodConstraint.CatSupportAlignment > 1.0
		|| !IsFiniteNonNegative(Result.LineLengthCentimeters) || !IsFiniteNonNegative(Result.LineTensionNewtons))
	{
		return RejectResolvedResult();
	}
	Result.Trace.bFinalizeInputAccepted = true;
	// 该阶段可在地形解析后重新计算，但从未写入 ASC/装备；从输入状态重算，不能叠加临时费用。
	Result.CatStaminaDrain = Result.FishStaminaDrain = Result.FishUncappedStaminaDrain = 0.0;
	Result.Trace.FishStaminaDrainBeforeClamp = 0.0;
	Result.CatReelStaminaDrain = Result.CatRodStaminaDrain = 0.0;
	Result.CatRodWorkStaminaDrain = Result.CatRodSupportStaminaDrain = Result.CatHoldStaminaDrain = 0.0;
	const double Dt = Config.FixedStepSeconds;
	const bool bOperatorPresent = State.bOperatorPresent;
	const bool bExhaustedCatEscape = Result.bExhaustedCatEscape;
	// 使用最终已放线长，覆盖本步刚好放尽和岸线解析后的重算；满线完全复用普通锁线费用。
	const bool bSlackRecovery = bOperatorPresent && !bExhaustedCatEscape
		&& State.CatAction == ECatFightCatAction::Slack && !IsLineAtMaximum(Config, Result.LineLengthCentimeters);
	Result.bSlackRecoveryActive = bSlackRecovery;
	const bool bFreeSpool = !bOperatorPresent || bSlackRecovery;
	const bool bReeling = bOperatorPresent && !bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Pull;
	const bool bStruggling = !State.bFishExhausted
		&& (bExhaustedCatEscape || State.MotionIntent == ECatFishMotionIntent::StrugglingOutward);
	const double LineTension = Result.LineTensionNewtons;
	if (LineTension <= 0.0) Result.RodLineForceNewtons = FVector::ZeroVector;
	const bool bLineRestraining = LineTension > UE_DOUBLE_SMALL_NUMBER;
	const bool bFreeSpoolReleased = !bLineRestraining;
	const double NormalizedTension = Result.NormalizedTension;
	const double EffectiveCatStrength = Result.EffectiveCatStrength;
	const double OperatorCatStrength = Result.OperatorCatStrength;
	const double FishEffortRatio = State.bFishExhausted ? 0.0 : bExhaustedCatEscape ? 1.0 : State.FishEffortRatio;
	const double ActiveFishStrength = Config.FishStrength * FishEffortRatio;
	const double CatForce = OperatorCatStrength * Result.RodLeverageMultiplier
		* RodConstraint.CatSupportAlignment * Config.ForcePerStrengthNewtons;
	Result.Trace.bFreeSpool = bFreeSpool;
	Result.Trace.bReeling = bReeling;
	Result.Trace.bStruggling = bStruggling;
	Result.Trace.bLineRestraining = bLineRestraining;
	Result.Trace.CatForceNewtons = CatForce;
	const FVector LineDirection = (Result.ProposedFishWorldPosition - RodConstraint.RodTipWorldPosition)
		.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
	const double RodLineAlignment = FVector::DotProduct(RodConstraint.RodForwardWorld.GetSafeNormal(), LineDirection);
	const double OutwardLoad = Result.NormalizedLineLoad;
	const double SwimSpeed = Result.IntendedSwimSpeedCentimetersPerSecond;
	Result.Trace.RodLineAlignment = RodLineAlignment;
	Result.Trace.LineTensionNewtons = LineTension;
	Result.Trace.NormalizedLineLoad = OutwardLoad;
	const FVector FishIntentDisplacement = Result.FishEffortDirection * SwimSpeed * Dt;
	const double RequestedReelDistance = Result.RequestedReelDistanceCentimeters;
	const double ActualReelDistance = FMath::Clamp(State.LineLengthCentimeters - Result.LineLengthCentimeters, 0.0, RequestedReelDistance);
	const FVector ProposedFishPosition = Result.ProposedFishWorldPosition;
	const bool bLoadedConstraint = bLineRestraining && !bFreeSpoolReleased;
	const bool bOperatorCanPayEffort = EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER;
	// 转杆仅消费本人实际物理转矩积分；本人的身体移动费用由 Runner 单独结算。
	const double CatRodExertion = bOperatorPresent && RodConstraint.bRodHeld && bOperatorCanPayEffort
		? RodConstraint.CatRodExertionSquaredSeconds : 0.0;
	const double FishOutwardIntentDistance = FMath::Max(0.0,
		FVector::DotProduct(FishIntentDisplacement, LineDirection));
	// 保留沿线意图距离供既有约束诊断；支撑费用另按负载与持续时间结算。
	const double CatHoldIntentDistance = bLoadedConstraint && !bFreeSpool
		? FishOutwardIntentDistance * NormalizedTension : 0.0;
	Result.CatRodExertionSquaredSeconds = CatRodExertion;
	Result.CatRodPositiveWorkRadians = bOperatorPresent && RodConstraint.bRodHeld && bOperatorCanPayEffort
		? RodConstraint.CatRodPositiveWorkRadians : 0.0;
	Result.CatHoldIntentCentimeters = CatHoldIntentDistance;
	Result.RequestedReelDistanceCentimeters = RequestedReelDistance;
	Result.ActualReelDistanceCentimeters = ActualReelDistance;
	Result.CatIntendedLineDistanceCentimeters = RequestedReelDistance + CatHoldIntentDistance;
	Result.CatActualLineDistanceCentimeters = ActualReelDistance;
	const double FishSignedIntentLineDistance = FVector::DotProduct(
		FishIntentDisplacement, LineDirection);
	const FVector FishActualDisplacement = ProposedFishPosition - State.FishWorldPosition
		- Result.FishPositionCorrectionWorldDisplacement;
	const double FishSignedActualLineDistance = FVector::DotProduct(FishActualDisplacement, LineDirection);
	Result.FishIntendedLineDistanceCentimeters = FMath::Abs(FishSignedIntentLineDistance);
	// 被收线或甩杆强迫拖向意图反方向的位移仍参与位置约束，但不能冒充鱼主动做功。
	Result.FishActualLineDistanceCentimeters = FishSignedIntentLineDistance >= 0.0
		? FMath::Max(0.0, FishSignedActualLineDistance)
		: FMath::Max(0.0, -FishSignedActualLineDistance);
	// 猫负载按可用力量归一化；鱼费用独立使用沿本步主动方向的运动缺失。
	Result.CatNormalizedEffortLoad = FMath::Clamp(LineTension / FMath::Max(CatForce, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	const double PerpendicularRodLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - RodLineAlignment * RodLineAlignment));
	Result.CatRodNormalizedEffortLoad = FMath::Clamp(LineTension * Config.RodPhysicsLengthCentimeters / 100.0
		* PerpendicularRodLever / FMath::Max(FMath::Max(0.0, OperatorCatStrength * RodConstraint.CatSupportAlignment)
			* Config.ForcePerStrengthNewtons, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	const bool bChargeFishIntent = !bSlackRecovery && !bExhaustedCatEscape && bOperatorPresent
		&& OperatorCatStrength > UE_DOUBLE_SMALL_NUMBER && !State.bFishExhausted && State.FishStamina > 0.0;
	FCatIntentMotionInput FishIntent;
	FishIntent.IntendedDisplacementCentimeters = FishIntentDisplacement;
	FishIntent.ActualDisplacementCentimeters = FishActualDisplacement;
	// 豁免步骤仍记录运动缺失，但不能因一笔不会收取的高价格乘积溢出而拒绝物理结果。
	FishIntent.StaminaPerUnfulfilledMeter = bChargeFishIntent ? Config.FishStaminaPerUnfulfilledMeter : 0.0;
	FCatIntentMotionResult FishIntentResult;
	if (!FCatIntentMotionModel::ComputeDrain(FishIntent, FishIntentResult))
	{
		return RejectResolvedResult();
	}
	Result.FishIntendedDistanceCentimeters = Result.Trace.FishIntendedDistanceCentimeters = FishIntentResult.IntendedDistanceCentimeters;
	Result.FishActualIntentProgressCentimeters = Result.Trace.FishActualIntentProgressCentimeters = FishIntentResult.ActualProgressCentimeters;
	Result.FishUnfulfilledDistanceCentimeters = Result.Trace.FishUnfulfilledDistanceCentimeters = FishIntentResult.UnfulfilledDistanceCentimeters;
	Result.Trace.FishStaminaPerUnfulfilledMeter = Config.FishStaminaPerUnfulfilledMeter;
	Result.Trace.CatReelPositiveWorkUnits = Config.StrengthPerKilogram * ActualReelDistance;
	Result.Trace.CatRodPositiveWorkUnits = Result.CatRodPositiveWorkRadians;
	Result.Trace.CatHoldNormalizedLoad = Result.CatNormalizedEffortLoad;
	Result.Trace.CatRodNormalizedLoad = Result.CatRodNormalizedEffortLoad;

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
		if (!ComputeCatChannel(ActualReelDistance, Config.CatStaminaCostPerStrengthCentimeter,
				Config.CatReelStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatReelStaminaDrain)
			|| !ComputeCatChannel(Result.CatRodPositiveWorkRadians, Config.CatRodStaminaCostPerStrengthRadian,
				Config.CatRodStaminaMultiplier, Result.CatRodNormalizedEffortLoad, Result.CatRodWorkStaminaDrain))
		{
			return RejectResolvedResult();
		}
		// 本人沿线与转杆支撑只收较高者；本人移动由 Runner 独立结算后再次去重。
		// 实际做功不能抵扣支撑；其他身体的物理接触不产生此账单。
		Result.CatHoldStaminaDrain = bLoadedConstraint && !bFreeSpool
			? Config.CatSupportStaminaPerSecond * Dt * FMath::Square(Result.CatNormalizedEffortLoad)
				* Config.CatHoldStaminaMultiplier : 0.0;
		const double RodSupport = Config.CatSupportStaminaPerSecond
			* Result.CatRodExertionSquaredSeconds * Config.CatRodStaminaMultiplier;
		Result.Trace.CatRodSupportBeforeHoldDeduction = RodSupport;
		if (!IsFiniteNonNegative(RodSupport) || !IsFiniteNonNegative(Result.CatHoldStaminaDrain))
		{
			return RejectResolvedResult();
		}
		Result.CatRodSupportStaminaDrain = FMath::Max(0.0, RodSupport - Result.CatHoldStaminaDrain);
		Result.CatRodStaminaDrain = Result.CatRodWorkStaminaDrain + Result.CatRodSupportStaminaDrain;
		Result.CatStaminaDrain = Result.CatReelStaminaDrain
			+ Result.CatRodStaminaDrain + Result.CatHoldStaminaDrain;
		if (!IsFiniteNonNegative(Result.CatStaminaDrain))
		{
			return RejectResolvedResult();
		}
	}
	// 尚有线杯容量时右键独立回体；已放尽则恢复正常做功、支撑与鱼出力费用。
	// 无人值守放线不恢复旧操作手；零体力强制拖拽也不通过右键退出。
	if (bSlackRecovery)
	{
		Result.CatStaminaDrain = -FMath::Min(FMath::Max(0.0, Config.CatStaminaMaximum - State.CatStamina),
			Config.SlackStaminaRegenPerSecond * Dt);
	}

	if (bChargeFishIntent)
	{
		// u 已进入意图速度；不再乘 u²、张力或鱼线夹角。倒拖保留全部负进展。
		Result.FishStaminaDrain = FishIntentResult.StaminaDrain;
		Result.FishUncappedStaminaDrain = Result.FishStaminaDrain;
		Result.Trace.FishStaminaDrainBeforeClamp = Result.FishStaminaDrain;
		Result.FishStaminaDrain = FMath::Min(Result.FishStaminaDrain, State.FishStamina);
		// 没有运动缺失或费用关闭时，不能仅因残余体力低于阈值就判为力竭。
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
	Result.Trace.FishLineForceNewtons = FishLineForce * Config.ForcePerStrengthNewtons;
	Result.Trace.CatLineForceNewtons = CatLineForce * Config.ForcePerStrengthNewtons;
	// LineLoad 是鱼主动沿线向外施力的投影，也是鱼竿磨损的唯一方向负载。
	// Tension 只说明几何约束已经介入，不能在鱼回头或横游时替代 LineLoad，
	// 否则猫端收线制造的张力会让低负载帧继续按满负载磨线。
	const double WearLoad = OutwardLoad;
	Result.Trace.WearLoad = WearLoad;
	// 鱼力竭后的收尾只保留线长约束和拖拽位移；死鱼不再施力，
	// 猫的收线力也不能独自制造鱼竿磨损，否则拉鱼干仍会耗尽耐久。
	// 拖落水期间不新增磨损，避免尚未落水就被断竿替代。
	const double RodWearDelta = !State.bFishExhausted && !bExhaustedCatEscape && bLineRestraining
		? (FMath::Max(FishLineForce, CatLineForce) * Config.StalemateRodWearPerFishStrength
			+ Config.FishFullEffortRodWearPerSecond * FMath::Square(FishEffortRatio))
			* WearLoad * Dt * Config.TautRodWearMultiplier : 0.0;
	Result.RodWearDelta = RodWearDelta;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta;
	Result.Trace.RodWearDelta = RodWearDelta;

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
	// No net-cat acceleration is published: the complete line load has one physical rod receiver.
	Result.Trace.HorizontalLineFactor = LineDirection.Size2D();
	Result.Trace.CatStaminaAfterStep = FMath::Clamp(State.CatStamina - Result.CatStaminaDrain, 0.0, Config.CatStaminaMaximum);
	Result.Trace.FishStaminaAfterStep = FMath::Max(0.0, State.FishStamina - Result.FishStaminaDrain);
	return IsFiniteNonNegative(Result.AbsoluteRodWear) && IsFiniteNonNegative(Result.FishStaminaDrain)
		&& FMath::IsFinite(Result.CatStaminaDrain);
}
