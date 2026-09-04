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
		&& FMath::IsFinite(AccelerationPerStrength) && AccelerationPerStrength > 0.0
		&& FMath::IsFinite(DriveResponseSeconds) && DriveResponseSeconds > 0.0
		&& FMath::IsFinite(RodStrength) && RodStrength > 0.0
		&& FMath::IsFinite(RodPhysicsLengthCentimeters) && RodPhysicsLengthCentimeters > 0.0
		&& FMath::IsFinite(CatStaminaMaximum) && CatStaminaMaximum > 0.0
		&& IsFiniteNonNegative(CatStaminaCostPerStrengthCentimeter)
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
		|| !IsFiniteVector(RodConstraint.RodTipWorldPosition)
		|| !IsFiniteVector(RodConstraint.RodForwardWorld)
		|| !IsFiniteVector(RodConstraint.RodTipVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierVelocityCentimetersPerSecond)
		|| !IsFiniteVector(RodConstraint.CarrierDesiredVelocityCentimetersPerSecond)
		|| !IsFiniteNonNegative(RodConstraint.CatRodIntentArcCentimeters)
		|| !IsFiniteNonNegative(RodConstraint.CatRodActualArcCentimeters)
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
	const bool bFreeSpool = !bOperatorPresent || (!bExhaustedCatEscape && State.CatAction == ECatFightCatAction::Slack);
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
	const double CatDriveAcceleration = EffectiveCatStrength * Config.AccelerationPerStrength;
	const double FishDriveAcceleration = ActiveFishStrength * Config.AccelerationPerStrength;
	const double FishOutwardAcceleration = FishDriveAcceleration * OutwardLoad;
	const double NetFishPullAcceleration = FMath::Max(0.0,
		FishOutwardAcceleration - CatDriveAcceleration);
	const double ForceScale = FMath::Max(CatDriveAcceleration, FishOutwardAcceleration);
	const double FishForceDominance = ForceScale > UE_DOUBLE_SMALL_NUMBER
		? NetFishPullAcceleration / ForceScale : 0.0;
	const double FishSpeedCap = bStruggling
		? Config.FishStruggleSpeedCentimetersPerSecond
		: Config.FishCalmSpeedCentimetersPerSecond;
	// 性格资产给的是鱼在无约束水体中的意图游速。力量换算出的加速度只参与绷线后的双方对抗，
	// 不能反过来成为自由游动的永久速度上限；否则小鱼即使松开线杯也会近似停在原地。
	const double SwimSpeed = State.bFishExhausted ? 0.0 : bExhaustedCatEscape
		? FMath::Max(Config.FishCalmSpeedCentimetersPerSecond, Config.FishStruggleSpeedCentimetersPerSecond)
			* Config.ExhaustedCatEscapeSpeedMultiplier : FishSpeedCap;
	if (!FMath::IsFinite(SwimSpeed)) return Result;
	const FVector FishIntentDisplacement = FishDirection * SwimSpeed * Dt;
	FVector FreeFishPosition = State.FishWorldPosition + FishIntentDisplacement;
	FreeFishPosition.Z = State.FishWorldPosition.Z;

	// L_paid 是线杯账面长度，竿尖旋转不能凭空放线。若抬竿后垂直距离暂时大于线长，
	// 由约束误差表达不可满足的几何状态，不能在按住收线时把线长反写得更长。
	const double PaidOutLine0 = FMath::Clamp(State.LineLengthCentimeters,
		0.0, Config.MaximumLineLengthCentimeters);
	// 力竭鱼的免耗体收尾使用配置收线速度，不能继续被猫的剩余搏斗力量卡住。
	// 活鱼仍按当前力量限制速度；两种阶段共用按键、线长下限和后续端点约束。
	const double ReelIntentSpeed = State.bFishExhausted
		? Config.ReelSpeedCentimetersPerSecond
		: FMath::Min(Config.ReelSpeedCentimetersPerSecond, CatDriveAcceleration * Config.DriveResponseSeconds);
	const double RequestedReelDistance = bReeling && (State.bFishExhausted || EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER)
		? FMath::Min(ReelIntentSpeed * Dt, FMath::Max(0.0, PaidOutLine0 - VerticalDistance)) : 0.0;
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
	const double NormalizedTension = FMath::Clamp(
		ConstraintError / Config.TensionResponseRangeCentimeters, 0.0, 1.0);

	FVector ProposedFishPosition = FreeFishPosition;
	double FishCorrection = 0.0;
	double CarrierCorrection = 0.0;
	double CarrierPullAcceleration = 0.0;
	double CarrierTargetPullSpeed = 0.0;
	double CarrierAwaySpeedMultiplier = 1.0;
	if (bLineRestraining && !bFreeSpoolReleased)
	{
		const double CatMass = Config.GetCombinedCatMass();
		const double BaseCarrierCorrectionShare = RodConstraint.bRodHeld
			? Config.FishMassKilograms / (CatMass + Config.FishMassKilograms) : 0.0;
		// 质量决定强鱼取得优势后两端如何分担运动；力量差决定是否取得优势。
		// 力量相等是自然僵持：鱼的向外意图由鱼端约束抵消，猫不会被“等质量各一半”无条件拖走。
		// 力竭后进入拖落水的玩法收尾：猫承担沿线位移，不再由小鱼质量把拖动压到近乎静止。
		// 仍通过同一鱼线约束和 CharacterMovement 的碰撞移动，不瞬移角色。
		const double CarrierCorrectionShare = bExhaustedCatEscape ? 1.0 : BaseCarrierCorrectionShare * FishForceDominance;
		CarrierPullAcceleration = NetFishPullAcceleration
			* BaseCarrierCorrectionShare * NormalizedTension;
		const double MaximumCarrierResponseSpeed = bExhaustedCatEscape ? SwimSpeed : FMath::Min(
			Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond,
			CarrierPullAcceleration * Config.DriveResponseSeconds);
		CarrierCorrection = FMath::Min(ConstraintError * CarrierCorrectionShare,
			MaximumCarrierResponseSpeed * Dt);
		const double MaximumFishCorrection = (bExhaustedCatEscape
			? FMath::Max(SwimSpeed, Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond)
			: Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond) * Dt;
		FVector TowardRod = IntendedRodTip - ProposedFishPosition;
		TowardRod.Z = 0.0;
		const double HorizontalDistance = TowardRod.Size2D();
		FishCorrection = FMath::Min3(FMath::Max(0.0, ConstraintError - CarrierCorrection),
			MaximumFishCorrection, HorizontalDistance);
		if (FishCorrection > UE_DOUBLE_SMALL_NUMBER && HorizontalDistance > UE_DOUBLE_SMALL_NUMBER)
		{
			ProposedFishPosition += TowardRod / HorizontalDistance * FishCorrection;
			ProposedFishPosition.Z = State.FishWorldPosition.Z;
		}
		CarrierTargetPullSpeed = CarrierCorrection / Dt;
		if (bExhaustedCatEscape) CarrierPullAcceleration = CarrierTargetPullSpeed / Dt;
		CarrierAwaySpeedMultiplier = bExhaustedCatEscape ? 0.0 : FMath::Lerp(1.0,
			Config.MinimumCarrierAwaySpeedMultiplier,
			NormalizedTension * CarrierCorrectionShare);
	}

	const double Distance1 = FVector::Distance(RodTip, ProposedFishPosition);
	const bool bLineTaut = bLineRestraining
		|| LineLength - Distance1 <= UE_DOUBLE_KINDA_SMALL_NUMBER;

	const bool bLoadedConstraint = bLineRestraining && !bFreeSpoolReleased;
	const bool bPrimaryCanPayEffort = Config.PrimaryOperatorCatStrength > UE_DOUBLE_SMALL_NUMBER;
	const double CatCarrierIntent = bOperatorPresent && RodConstraint.bRodHeld && bLoadedConstraint && bPrimaryCanPayEffort
		? FMath::Max(0.0, -FVector::DotProduct(CarrierIntentDisplacement, LineDirection)) : 0.0;
	const double CatCarrierActual = bOperatorPresent && RodConstraint.bRodHeld
		? FMath::Max(0.0, -FVector::DotProduct(
			RodConstraint.CarrierVelocityCentimetersPerSecond * Dt, LineDirection)) : 0.0;
	// 转杆使用转矩积分的独立努力，身体实际位移只使用 CarrierVelocity，避免竿尖扫动重复扣费。
	const double CatRodIntent = bOperatorPresent && RodConstraint.bRodHeld && bPrimaryCanPayEffort
		? RodConstraint.CatRodIntentArcCentimeters : 0.0;
	const double CatActiveIntentDistance = RequestedReelDistance + CatCarrierIntent;
	const double FishOutwardIntentDistance = FMath::Max(0.0,
		FVector::DotProduct(FishIntentDisplacement, LineDirection));
	// 未放线且约束已介入时，保持鱼线始终有一份基础努力。
	// 先保留完整基线，计费时只补主动操作尚未覆盖的费用，避免微小操作免掉整段保持。
	const double CatHoldIntentDistance = bLoadedConstraint && !bFreeSpool
		? FishOutwardIntentDistance * NormalizedTension : 0.0;
	Result.CatMovementIntentCentimeters = CatCarrierIntent;
	Result.CatMovementActualCentimeters = FMath::Min(CatCarrierActual, CatCarrierIntent);
	Result.CatRodIntentArcCentimeters = CatRodIntent;
	Result.CatRodActualArcCentimeters = FMath::Min(RodConstraint.CatRodActualArcCentimeters, CatRodIntent);
	Result.CatHoldIntentCentimeters = CatHoldIntentDistance;
	Result.RequestedReelDistanceCentimeters = RequestedReelDistance;
	Result.ActualReelDistanceCentimeters = RequestedReelDistance;
	Result.CatIntendedLineDistanceCentimeters = CatActiveIntentDistance + CatHoldIntentDistance;
	Result.CatActualLineDistanceCentimeters = RequestedReelDistance + Result.CatMovementActualCentimeters;
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
	const double EffortTension = bLoadedConstraint ? NormalizedTension : 0.0;
	const double FishOutwardStrength = ActiveFishStrength * OutwardLoad;
	Result.CatNormalizedEffortLoad = EffortTension * FMath::Clamp(
		FishOutwardStrength / FMath::Max(EffectiveCatStrength, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	const double PerpendicularRodLever = FMath::Sqrt(FMath::Max(0.0, 1.0 - RodLineAlignment * RodLineAlignment));
	Result.CatRodNormalizedEffortLoad = EffortTension * FMath::Clamp(
		FishOutwardStrength * Config.RodPhysicsLengthCentimeters / 100.0 * PerpendicularRodLever
		/ FMath::Max(Config.PrimaryOperatorCatStrength, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
	Result.FishNormalizedEffortLoad = EffortTension * FMath::Max(0.0, Alignment)
		* FMath::Clamp(EffectiveCatStrength / FMath::Max(ActiveFishStrength, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);

	double IgnoredEffortDistance = 0.0;
	// 鱼力竭后进入纯收尾：仍求解收线和双端位移，但不再向任何猫结算做功消耗。
	if (!State.bFishExhausted && bOperatorPresent && EffectiveCatStrength > UE_DOUBLE_SMALL_NUMBER)
	{
		FCatFightWorkInput CatWork;
		// 标准努力强度保留单位标尺；动作倍率与自身负载决定消耗，不按绝对力量放大数十倍。
		CatWork.Strength = Config.StrengthPerKilogram;
		CatWork.IsometricEffortMultiplier = Config.IsometricEffortMultiplier;
		CatWork.CostPerStrengthCentimeter = Config.CatStaminaCostPerStrengthCentimeter;
		CatWork.LoadStaminaMultiplier = Config.CatLoadStaminaMultiplier;
		const double Phase = bStruggling ? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
		const auto ComputeCatChannel = [&](const double Intent, const double Actual, const double Multiplier,
			const double Load, double& OutDrain)
		{
			CatWork.IntendedLineDistanceCentimeters = Intent;
			CatWork.ActualLineDistanceCentimeters = Actual;
			CatWork.PhaseMultiplier = Phase * Multiplier;
			CatWork.NormalizedLoad = Load;
			return FCatFishingFightWorkModel::ComputeDrain(CatWork, OutDrain, IgnoredEffortDistance);
		};
		if (!ComputeCatChannel(CatCarrierIntent, Result.CatMovementActualCentimeters,
			Config.CatMovementStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatMovementStaminaDrain)
			|| !ComputeCatChannel(RequestedReelDistance, RequestedReelDistance,
				Config.CatReelStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatReelStaminaDrain)
			|| !ComputeCatChannel(CatRodIntent, Result.CatRodActualArcCentimeters,
				Config.CatRodStaminaMultiplier, Result.CatRodNormalizedEffortLoad, Result.CatRodStaminaDrain)
			|| !ComputeCatChannel(CatHoldIntentDistance, 0.0,
				Config.CatHoldStaminaMultiplier, Result.CatNormalizedEffortLoad, Result.CatHoldStaminaDrain))
		{
			return FCatFightStepResult{};
		}
		const double ActiveDrain = Result.CatMovementStaminaDrain
			+ Result.CatReelStaminaDrain + Result.CatRodStaminaDrain;
		if (!IsFiniteNonNegative(ActiveDrain))
		{
			return FCatFightStepResult{};
		}
		// 个人费用只有主位当下能支付的部分才可覆盖保持；不能用透支请求免掉助手的保持费用。
		const double PaidHoldCoverage = Result.CatReelStaminaDrain
			+ FMath::Min(State.CatStamina, Result.GetPrimaryCatStaminaDrain());
		Result.CatHoldStaminaDrain = FMath::Max(0.0, Result.CatHoldStaminaDrain - PaidHoldCoverage);
		Result.CatStaminaDrain = ActiveDrain + Result.CatHoldStaminaDrain;
		if (!IsFiniteNonNegative(Result.CatStaminaDrain))
		{
			return FCatFightStepResult{};
		}
	}
	if (Result.CatStaminaDrain <= UE_DOUBLE_SMALL_NUMBER
		&& bOperatorPresent && bFreeSpool && bFreeSpoolReleased && !bLineRestraining)
	{
		const double Capped = FMath::Min(Config.CatStaminaMaximum,
			State.CatStamina + Config.SlackStaminaRegenPerSecond * Dt);
		Result.CatStaminaDrain = -(Capped - State.CatStamina);
	}

	if (!State.bFishExhausted && State.FishStamina > 0.0
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
			return FCatFightStepResult{};
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
	const double LineForceDemand = FMath::Max(FishLineForce, CatLineForce) * NormalizedTension;
	// LineLoad 是鱼主动沿线向外施力的投影，也是鱼竿磨损的唯一方向负载。
	// Tension 只说明几何约束已经介入，不能在鱼回头或横游时替代 LineLoad，
	// 否则猫端收线制造的张力会让低负载帧继续按满负载磨线。
	const double WearLoad = OutwardLoad;
	// 鱼力竭后的收尾只保留线长约束和拖拽位移；死鱼不再施力，
	// 猫的收线力也不能独自制造鱼竿磨损，否则拉鱼干仍会把会话判为断线。
	// 拖落水期间不新增磨损或积累过载终局，避免尚未落水就被断竿替代。
	const double RodWearDelta = !State.bFishExhausted && !bExhaustedCatEscape && bLineRestraining
		? (FMath::Max(FishLineForce, CatLineForce) * Config.StalemateRodWearPerFishStrength
			+ (bStruggling ? Config.StruggleHoldRodWearPerSecond : 0.0))
			* WearLoad * Dt * Config.TautRodWearMultiplier : 0.0;
	Result.RodWearDelta = RodWearDelta;
	Result.AbsoluteRodWear = State.AbsoluteRodWear + RodWearDelta;

	Result.CarrierConstraintCorrectionCentimeters = CarrierCorrection;
	Result.CarrierTargetPullSpeedCentimetersPerSecond = CarrierTargetPullSpeed;
	Result.CarrierPullAccelerationCentimetersPerSecondSquared = CarrierPullAcceleration;
	Result.CarrierAwaySpeedMultiplier = CarrierAwaySpeedMultiplier;

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
	Result.CatDriveAccelerationCentimetersPerSecondSquared = CatDriveAcceleration;
	Result.FishDriveAccelerationCentimetersPerSecondSquared = FishDriveAcceleration;
	Result.NetFishPullAccelerationCentimetersPerSecondSquared = NetFishPullAcceleration;
	Result.FishForceDominance = FishForceDominance;
	Result.ConstraintErrorCentimeters = ConstraintError;
	Result.RelativeConstraintSpeedCentimetersPerSecond = Dt > 0.0
		? (IntendedDistance - Distance0 + RequestedReelDistance) / Dt : 0.0;
	Result.FishConstraintCorrectionCentimeters = FishCorrection;

	// 同步归零时先确认真实坏竿，不能被鱼力竭或过载终局掩盖实例损坏。
	if (!State.bFishExhausted && Result.AbsoluteRodWear >= Config.RodDurability)
	{
		Result.Outcome = ECatFightStepOutcome::RodBroken;
	}
	else if (Result.bStrongConfrontation && LineForceDemand >= Config.RodStrength)
	{
		Result.LineBreakCause = ECatFightLineBreakCause::StrengthOverload;
		Result.Outcome = ECatFightStepOutcome::LineBroken;
	}
	else if (!State.bFishExhausted
		&& State.FishStamina - Result.FishStaminaDrain <= 0.0)
	{
		Result.Outcome = ECatFightStepOutcome::FishExhausted;
	}
	else if (!bExhaustedCatEscape && FreeDistance > Config.MaximumLineLengthCentimeters + Config.EscapeSlackCentimeters)
	{
		Result.Outcome = ECatFightStepOutcome::Escaped;
	}

	Result.bSucceeded = true;
	return Result;
}
