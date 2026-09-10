#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

namespace CatFishingCoupledSimulationTest
{
	FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.1;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 10.0;
		Config.FishMassKilograms = 3.0;
		Config.FishStrength = 40.0;
		Config.StrengthPerKilogram = 10.0;
		Config.ForcePerStrengthNewtons = 1.0;
		Config.CatStaminaMaximum = 100.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		Config.MaximumLineLengthCentimeters = 1000.0;
		Config.RodDurability = 1000.0;
		return Config;
	}

	FCatFightSimulationState MakeState(ECatFightCatAction Action)
	{
		FCatFightSimulationState State;
		State.CatStamina = 100.0;
		State.FishStamina = 100.0;
		State.LineLengthCentimeters = 500.0;
		State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		State.CatAction = Action;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		return State;
	}

	FCatFightRodConstraintInput MakeHeldConstraint()
	{
		FCatFightRodConstraintInput Input;
		Input.RodForwardWorld = FVector::ForwardVector;
		Input.bRodHeld = true;
		return Input;
	}


}

using namespace CatFishingCoupledSimulationTest;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingUnforcedShoreContactTest,
	"Catfishing.Unit.Fishing.Simulation.UnforcedShoreContactStillSlidesInsideWater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingUnforcedShoreContactTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishShoreContactInput Input;
	Input.CurrentFishWorldPosition = FVector(0.0, 300.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(40.0, 240.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector(40.0, 260.0, 0.0);
	Input.WaterwardDirection = FVector(0.0, 1.0, 0.0);
	Input.RodTipWorldPosition = FVector::ZeroVector;
	Input.PreviousLineLengthCentimeters = 400.0;
	Input.ProposedLineLengthCentimeters = 400.0;
	Input.MaximumConstraintDistanceCentimeters = 400.0;

	const FCatFishShoreContactResult Result =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("ordinary shore contact still solves"), Result.bSucceeded);
	TestTrue(TEXT("ordinary shore contact is detected"), Result.bShoreContact);
	TestTrue(TEXT("water-only shore response does not use the land candidate"),
		!Result.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingShoreWaterwardRecoveryTest,
	"Catfishing.Unit.Fishing.Simulation.ShoreContactPreservesWaterwardRecoveryWithoutInsetSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingShoreWaterwardRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishShoreContactInput Input;
	Input.CurrentFishWorldPosition = FVector(-60.0, 0.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(-59.0, 0.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector(5.0, 0.0, 0.0);
	Input.WaterwardDirection = FVector::ForwardVector;
	Input.RodTipWorldPosition = FVector(-500.0, 0.0, 0.0);
	Input.PreviousLineLengthCentimeters = 600.0;
	Input.ProposedLineLengthCentimeters = 600.0;
	Input.MaximumConstraintDistanceCentimeters = 600.0;
	const auto GapRecovery = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("live fish in the collision-to-water-outline gap can recover"), GapRecovery.bSucceeded);
	TestTrue(TEXT("gap recovery retains exactly the proposed one-centimeter swim"),
		GapRecovery.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));
	TestTrue(TEXT("gap recovery does not jump across the gap to the water inset"),
		GapRecovery.FishWorldPosition.X < 0.0);

	// 0.25 cm 自游小步覆盖旧 1 cm 修正阈值，不能被归为无需处理而吸回最近岸点。
	Input.CurrentFishWorldPosition = FVector(0.1, 0.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(0.35, 0.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector::ZeroVector;
	const auto BoundaryRecovery = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("shore correction smaller than the former one-centimeter threshold remains valid"), BoundaryRecovery.bSucceeded);
	TestTrue(TEXT("boundary tolerance cannot erase a small swim into the lake"),
		BoundaryRecovery.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));

	Input.CurrentFishWorldPosition = FVector(-60.0, 0.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(-63.0, 4.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector(5.0, 4.0, 0.0);
	const auto Landward = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("unforced landward motion still resolves"), Landward.bSucceeded);
	TestEqual(TEXT("landward normal motion remains blocked"), Landward.FishWorldPosition.X, -60.0, 1e-6);
	TestEqual(TEXT("blocked landward motion retains its shoreline slide"), Landward.FishWorldPosition.Y, 4.0, 1e-6);

	Input.CandidateFishWorldPosition = FVector(-57.0, 4.0, 0.0);
	const auto DiagonalRecovery = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("diagonal waterward recovery resolves"), DiagonalRecovery.bSucceeded);
	TestTrue(TEXT("diagonal swim preserves both legitimate motion components"),
		DiagonalRecovery.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));
	Input.ResolvedWaterWorldPosition.Y = 40.0;
	const auto DistantProjection = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("distant shoreline projection resolves"), DistantProjection.bSucceeded);
	TestTrue(TEXT("distant shoreline projection retains positive waterward progress"),
		DistantProjection.FishWorldPosition.X > Input.CurrentFishWorldPosition.X);
	TestTrue(TEXT("combined normal and tangential correction stays within the original step budget"),
		FVector::Dist2D(DistantProjection.FishWorldPosition, Input.CurrentFishWorldPosition) <= 5.0 + 1e-6);

	Input.CandidateFishWorldPosition = FVector(-59.0, 0.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector(5.0, 0.0, 0.0);
	Input.PreviousLineLengthCentimeters = 440.0;
	Input.ProposedLineLengthCentimeters = 445.0;
	Input.bSlacking = true;
	const auto SlackRecovery = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("free-spool waterward recovery resolves"), SlackRecovery.bSucceeded);
	TestEqual(TEXT("free spool pays only for the final one-centimeter fish movement"),
		SlackRecovery.LineLengthCentimeters, 441.0, 1e-6);
	Input.PreviousLineLengthCentimeters = 600.0;
	Input.ProposedLineLengthCentimeters = 600.0;
	const auto ExistingSlack = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("existing slack permits waterward recovery"), ExistingSlack.bSucceeded);
	TestEqual(TEXT("shore recovery preserves already-paid slack"), ExistingSlack.LineLengthCentimeters, 600.0, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBeachingIntentTest,
	"Catfishing.Unit.Fishing.Simulation.BeachingRequiresReelOrCarrierTranslationNotRodTipSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBeachingIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishBeachingIntentInput Input;
	Input.CurrentFishWorldPosition = FVector(0.0, 10.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(0.0, -10.0, 0.0);
	Input.WaterwardDirection = FVector(0.0, 1.0, 0.0);
	Input.bLineTaut = true;
	TestFalse(TEXT("rod-tip rotation alone cannot beach the fish"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));

	Input.ActualReelDistanceCentimeters = 2.0;
	TestTrue(TEXT("actual reeling can beach a shore-crossing fish"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	Input.NonCarrierRodTipWorldDisplacement = FVector(0.0, -20.0, 0.0);
	TestFalse(TEXT("rod-tip swing cannot borrow a simultaneous reel input to beach the fish"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	Input.NonCarrierRodTipWorldDisplacement = FVector::ZeroVector;
	Input.ActualReelDistanceCentimeters = 0.0;
	Input.CarrierActualWorldDisplacement = FVector(0.0, -2.0, 0.0);
	TestTrue(TEXT("actual carrier translation toward land can beach a shore-crossing fish"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	Input.CarrierActualWorldDisplacement = FVector::ZeroVector;
	Input.ReelConstraintDistanceCentimeters = 2.0;
	TestTrue(TEXT("residual line correction still hauls when the reel has reached its vertical limit"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	Input.NonCarrierRodTipWorldDisplacement = FVector(20.0, 0.0, 0.0);
	TestTrue(TEXT("lateral aim adjustment does not cancel actual landward hauling"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	Input.ReelConstraintDistanceCentimeters = 0.0;
	TestFalse(TEXT("aim adjustment without real hauling still cannot exhaust a live fish"),
		FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Input));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingShoreRotationRecoveryTest,
	"Catfishing.Unit.Fishing.Simulation.ReelingSurvivesRodSwingLineGeometryAtShore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingShoreRotationRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishShoreContactInput Input;
	Input.CurrentFishWorldPosition = FVector(0.0, 200.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(0.0, 190.0, 0.0);
	Input.ResolvedWaterWorldPosition = Input.CandidateFishWorldPosition;
	Input.WaterwardDirection = FVector::ForwardVector;
	Input.RodTipWorldPosition = FVector(0.0, 0.0, 150.0);
	Input.PreviousLineLengthCentimeters = 190.0;
	Input.ProposedLineLengthCentimeters = 200.0;
	Input.MaximumConstraintDistanceCentimeters = 250.0;
	Input.bReeling = true;
	const FCatFishShoreContactResult Result =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("recoverable rod-swing geometry does not invalidate the shore solve"), Result.bSucceeded);
	TestEqual(TEXT("reeling never pays line out while recovering geometry"),
		Result.LineLengthCentimeters, Input.PreviousLineLengthCentimeters, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodResistanceLengthTest,
	"Catfishing.Unit.Fishing.Simulation.RodRotationResistanceUsesConfiguredPhysicsLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodResistanceLengthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingRodResistanceInput Input;
	Input.CatStrength = 50.0;
	Input.LineTensionNewtons = 25.0;
	Input.RodLineAlignment = 0.0;
	Input.RodPhysicsLengthCentimeters = 100.0;
	const FCatFishingRodResistanceResult OneMeter = FCatFishingRodResistanceModel::Evaluate(Input);
	TestTrue(TEXT("one-meter configured rod solves"), OneMeter.bSucceeded);
	TestEqual(TEXT("one-meter rod produces torque from configured length"),
		OneMeter.MaximumFishTorqueStrengthMeters, 25.0, 1e-9);

	Input.RodPhysicsLengthCentimeters = 200.0;
	const FCatFishingRodResistanceResult TwoMeters = FCatFishingRodResistanceModel::Evaluate(Input);
	TestTrue(TEXT("two-meter configured rod solves"), TwoMeters.bSucceeded);
	TestEqual(TEXT("two-meter configured rod doubles fish torque without a lock flag"),
		TwoMeters.MaximumFishTorqueStrengthMeters, 50.0, 1e-9);

	Input.LineTensionNewtons = 0.0;
	const FCatFishingRodResistanceResult Slack = FCatFishingRodResistanceModel::Evaluate(Input);
	TestEqual(TEXT("slack line leaves rod rotation unrestricted"),
		Slack.MaximumFishTorqueStrengthMeters, 0.0, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSpoolModesTest,
	"Catfishing.Unit.Fishing.Simulation.SpoolModesSeparateEndpointMovementFromLineLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSpoolModesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	const FCatFightStepResult Locked = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), Rod, FVector::ForwardVector);
	const FCatFightStepResult Reeling = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::Pull), Rod, FVector::ForwardVector);
	const FCatFightStepResult FreeSpool = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::Slack), Rod, FVector::ForwardVector);
	TestTrue(TEXT("all spool modes solve"), Locked.bSucceeded && Reeling.bSucceeded && FreeSpool.bSucceeded);
	TestEqual(TEXT("locked spool preserves paid-out length"), Locked.LineLengthCentimeters, 500.0, 1e-6);
	TestTrue(TEXT("finite reeling shortens the line only by completed distance"), Reeling.ActualReelDistanceCentimeters > 0.0
		&& Reeling.ActualReelDistanceCentimeters < Reeling.RequestedReelDistanceCentimeters);
	TestEqual(TEXT("line account equals the completed reel distance"), Reeling.LineLengthCentimeters, 500.0 - Reeling.ActualReelDistanceCentimeters, 1e-6);
	TestEqual(TEXT("requested reel distance is explicit"), Reeling.RequestedReelDistanceCentimeters, 8.0, 1e-6);
	TestTrue(TEXT("free spool pays out for outward fish intent"), FreeSpool.LineLengthCentimeters > 500.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFreeSpoolSwimTest,
	"Catfishing.Unit.Fishing.Simulation.FreeSpoolUsesBehaviorSwimSpeedEvenForWeakFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFreeSpoolSwimTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FishStrength = 1.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Slack);
	State.FishVelocityCentimetersPerSecond = FVector::ForwardVector * Config.FishFullEffortSpeedCentimetersPerSecond;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("weak fish free-spool step solves"), Step.bSucceeded);
	TestEqual(TEXT("free fish keeps its behavior-defined struggle speed"),
		Step.IntendedSwimSpeedCentimetersPerSecond,
		Config.FishFullEffortSpeedCentimetersPerSecond, 1e-9);
	TestTrue(TEXT("free-spool weak fish visibly changes world position"),
		Step.ProposedFishWorldPosition.X > State.FishWorldPosition.X + 1.0);
	TestTrue(TEXT("free spool pays line out for the actual swim"),
		Step.LineLengthCentimeters > State.LineLengthCentimeters);
	TestEqual(TEXT("free swimming with released spool costs no fish stamina"), Step.FishStaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingVerticalRodSwingDoesNotPayOutTest,
	"Catfishing.Unit.Fishing.Simulation.VerticalRodSwingCannotPayOutLineWhileReeling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingVerticalRodSwingDoesNotPayOutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.LineLengthCentimeters = 100.0;
	State.FishWorldPosition = FVector(100.0, 0.0, 0.0);
	const FCatFightStepResult HorizontalRodStep = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	Rod.RodTipWorldPosition = FVector(0.0, 0.0, 150.0);
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("vertical swing geometry remains solvable"), Step.bSucceeded);
	TestEqual(TEXT("vertical swing does not manufacture line"),
		Step.LineLengthCentimeters, State.LineLengthCentimeters, 1e-9);
	TestEqual(TEXT("impossible vertical geometry pauses reel progress"),
		Step.RequestedReelDistanceCentimeters, 0.0, 1e-9);
	TestEqual(TEXT("vertical rod geometry cannot move fish off its water plane"),
		Step.ProposedFishWorldPosition.Z, State.FishWorldPosition.Z, 1e-9);
	TestTrue(TEXT("vertical rod geometry cannot add fish stamina work"),
		Step.FishStaminaDrain <= HorizontalRodStep.FishStaminaDrain + 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingEndpointIntentTest,
	"Catfishing.Unit.Fishing.Simulation.ActualBackingEndpointDoesNotDoubleReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingEndpointIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	// 只消费身体实际完成后的竿尖样本，不把移动意图预支成位移。
	Rod.RodTipWorldPosition = FVector(-15, 0, 0);
	Rod.CarrierVelocityCentimetersPerSecond = -FVector::ForwardVector * 300.0;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), Rod, FVector::ForwardVector);
	TestTrue(TEXT("endpoint intent creates one coupled constraint"), Step.ConstraintErrorCentimeters > 0.0);
	TestEqual(TEXT("backing away does not change paid-out length"), Step.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("backing away does not pretend to reel"), Step.RequestedReelDistanceCentimeters, 0.0, 1e-6);
	TestTrue(TEXT("solver consumes the actual endpoint without moving it again"),
		Step.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMassSplitTest,
	"Catfishing.Unit.Fishing.Simulation.ObservedEndpointDoesNotIntegrateCatBodyMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingMassSplitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig LightCat = MakeConfig();
	LightCat.FishStrength = 100.0;
	LightCat.PrimaryOperatorMassKilograms = 3.0;
	FCatFightSimulationConfig HeavyCat = LightCat;
	HeavyCat.PrimaryOperatorMassKilograms = 30.0;
	const FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	const FCatFightStepResult Light = FCatFishingFightSimulator::Step(
		LightCat, MakeState(ECatFightCatAction::None), Rod, FVector::ForwardVector);
	const FCatFightStepResult Heavy = FCatFishingFightSimulator::Step(
		HeavyCat, MakeState(ECatFightCatAction::None), Rod, FVector::ForwardVector);
	TestEqual(TEXT("same actual rod endpoint produces the same line force"), Heavy.LineTensionNewtons, Light.LineTensionNewtons, 1e-6);
	TestTrue(TEXT("both pure solves leave the observed rod endpoint untouched"),
		Light.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6)
		&& Heavy.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
	TestEqual(TEXT("strong fish does not directly create a terminal cat-water outcome"),
		Heavy.Outcome, ECatFightStepOutcome::None);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStalledFishIntentCostTest,
	"Catfishing.Unit.Fishing.Simulation.StalledFishIntentCostUsesCurrentEffortOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStalledFishIntentCostTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = MakeConfig();
	const double FullIntentDistance = Config.FishFullEffortSpeedCentimetersPerSecond * Config.FixedStepSeconds;
	for (const double Effort : {0.0, 0.25, 0.5, 1.0})
	{
		auto State = MakeState(ECatFightCatAction::None);
		State.FishEffortRatio = Effort;
		const auto Step = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
		if (!TestTrue(TEXT("各出力的真实锁线求解有效"), Step.bSucceeded)) return false;
		TestTrue(TEXT("固定竿端锁线确实阻止鱼向外位移"), Step.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 1e-9));
		TestEqual(TEXT("期望意图距离只按当前出力缩放一次"),
			Step.FishIntendedDistanceCentimeters, FullIntentDistance * Effort, 1e-9);
		TestEqual(TEXT("未完成距离保留全部僵持意图"),
			Step.FishUnfulfilledDistanceCentimeters, FullIntentDistance * Effort, 1e-9);
		TestEqual(TEXT("僵持鱼费用按米价结算，不再额外平方出力"), Step.FishStaminaDrain,
			FullIntentDistance * Effort / 100.0 * Config.FishStaminaPerUnfulfilledMeter, 1e-9);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingInwardReelRodWearTest,
	"Catfishing.Unit.Fishing.Simulation.TensionWithoutOutwardFishLoadCannotWearRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingInwardReelRodWearTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.StalemateRodWearPerFishStrength = 0.1;
	Config.FishFullEffortRodWearPerSecond = 1.0;
	Config.TautRodWearMultiplier = 2.0;
	Config.RodDurability = 1.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.AbsoluteRodWear = 0.99;
	State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), -FVector::ForwardVector);
	TestTrue(TEXT("inward reel still creates a real geometric constraint"),
		Step.bSucceeded && Step.NormalizedTension > 0.0);
	TestEqual(TEXT("inward fish direction has no outward line load"),
		Step.NormalizedLineLoad, 0.0, 1e-9);
	TestEqual(TEXT("tension alone cannot add rod wear"), Step.RodWearDelta, 0.0, 1e-9);
	TestTrue(TEXT("inward reeling completes at least the fish's intended progress"),
		Step.FishActualIntentProgressCentimeters >= Step.FishIntendedDistanceCentimeters);
	TestEqual(TEXT("completed inward intent has no unfulfilled-distance cost"), Step.FishStaminaDrain, 0.0);
	TestEqual(TEXT("accumulated rod wear is unchanged without outward fish load"),
		Step.AbsoluteRodWear, State.AbsoluteRodWear, 1e-9);
	TestEqual(TEXT("inward reeling cannot break a nearly worn rod"),
		Step.Outcome, ECatFightStepOutcome::None);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingDirectionalRodWearTest,
	"Catfishing.Unit.Fishing.Simulation.LowOutwardLoadScalesWearAndFullLoadCanStillBreakRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingDirectionalRodWearTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.StalemateRodWearPerFishStrength = 0.1;
	Config.FishFullEffortRodWearPerSecond = 1.0;
	Config.TautRodWearMultiplier = 2.0;
	Config.RodDurability = 1.0;
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	const FVector MostlySideways(0.05, FMath::Sqrt(1.0 - 0.05 * 0.05), 0.0);
	const FCatFightStepResult LowLoad = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), MostlySideways);
	const FCatFightStepResult FullLoad = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("both line load cases solve"), LowLoad.bSucceeded && FullLoad.bSucceeded);
	TestTrue(TEXT("reeling tension is higher than the low outward fish load"),
		LowLoad.NormalizedTension > LowLoad.NormalizedLineLoad);
	TestTrue(TEXT("low outward load still causes small positive wear"), LowLoad.RodWearDelta > 0.0);
	TestEqual(TEXT("wear scales continuously by the outward projection"),
		LowLoad.RodWearDelta, FullLoad.RodWearDelta * LowLoad.NormalizedLineLoad, 1e-9);
	TestEqual(TEXT("low load preserves the session despite reeling tension"),
		LowLoad.Outcome, ECatFightStepOutcome::None);
	TestEqual(TEXT("real durability depletion breaks the rod"),
		FullLoad.Outcome, ECatFightStepOutcome::RodBroken);
	FCatFightSimulationState ExhaustingFish = State;
	ExhaustingFish.FishStamina = 0.001;
	const FCatFightStepResult Simultaneous = FCatFishingFightSimulator::Step(
		Config, ExhaustingFish, MakeHeldConstraint(), FVector::ForwardVector);
	TestEqual(TEXT("fish exhaustion cannot hide simultaneous rod depletion"),
		Simultaneous.Outcome, ECatFightStepOutcome::RodBroken);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStrengthNormalizedStaminaTest,
	"Catfishing.Unit.Fishing.Simulation.RightButtonRecoveryWaivesCostForWeakOrStrongFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStrengthNormalizedStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig WeakFishConfig = MakeConfig();
	WeakFishConfig.FishStrength = 0.4;
	FCatFightSimulationConfig StrongFishConfig = WeakFishConfig;
	StrongFishConfig.FishStrength = 40.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Slack);
	const FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	const FCatFightStepResult WeakFish = FCatFishingFightSimulator::Step(
		WeakFishConfig, State, Rod, FVector::ForwardVector);
	const FCatFightStepResult StrongFish = FCatFishingFightSimulator::Step(
		StrongFishConfig, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("弱鱼与强鱼单步都成功"), WeakFish.bSucceeded && StrongFish.bSucceeded);
	TestTrue(TEXT("松线时相同游速产生相同沿线努力距离"),
		FMath::IsNearlyEqual(WeakFish.FishIntendedLineDistanceCentimeters,
			StrongFish.FishIntendedLineDistanceCentimeters, 1e-9));
	TestTrue(TEXT("强弱鱼都由未满线右键恢复规则豁免费用"), WeakFish.bSlackRecoveryActive && StrongFish.bSlackRecoveryActive);
	TestEqual(TEXT("右键恢复不因绝对力量差产生体力费用"),
		WeakFish.FishStaminaDrain, StrongFish.FishStaminaDrain, 1e-9);
	TestEqual(TEXT("右键恢复期间弱鱼不消耗体力"), WeakFish.FishStaminaDrain, 0.0);
	TestEqual(TEXT("右键恢复期间强鱼不消耗体力"), StrongFish.FishStaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedVerticalEndpointTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedFishAtVerticalRodEndpointNeedsNoSwimDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedVerticalEndpointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.FishWorldPosition = FVector(0.0, 0.0, -100.0);
	State.LineLengthCentimeters = 95.0;
	for (const FVector& Direction : {FVector::ZeroVector, FVector::UpVector, FVector::ForwardVector})
	{
		const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
			Config, State, MakeHeldConstraint(), Direction);
		TestTrue(TEXT("dead fish remains valid directly below the tip"), Step.bSucceeded);
		TestEqual(TEXT("vertical endpoint does not terminate the session"), Step.Outcome, ECatFightStepOutcome::None);
		TestTrue(TEXT("dead fish does not invent horizontal swimming"),
			Step.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 1e-6));
		TestEqual(TEXT("dead fish has no outward load"), Step.NormalizedLineLoad, 0.0);
		TestEqual(TEXT("dead fish causes no stamina drain"), Step.CatStaminaDrain, 0.0);
		TestEqual(TEXT("dead fish causes no rod wear"), Step.RodWearDelta, 0.0);
	}
	State.bFishExhausted = false;
	State.FishStamina = 100.0;
	TestFalse(TEXT("live fish still rejects a missing swim direction"),
		FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ZeroVector).bSucceeded);
	TestFalse(TEXT("live fish still rejects purely vertical swimming"),
		FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::UpVector).bSucceeded);
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedContinuationTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedFishKeepsSameSolverAndMovesOnlyWhenReeled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedContinuationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState LockedState = MakeState(ECatFightCatAction::None);
	LockedState.bFishExhausted = true;
	LockedState.FishStamina = 0.0;
	LockedState.AbsoluteRodWear = Config.RodDurability;
	LockedState.MotionIntent = ECatFishMotionIntent::AutoHauling;
	FCatFightSimulationState ReelingState = LockedState;
	ReelingState.CatAction = ECatFightCatAction::Pull;
	const FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	const FCatFightStepResult Locked = FCatFishingFightSimulator::Step(
		Config, LockedState, Rod, -FVector::ForwardVector);
	const FCatFightStepResult Reeling = FCatFishingFightSimulator::Step(
		Config, ReelingState, Rod, -FVector::ForwardVector);
	TestTrue(TEXT("exhausted steps still use the coupled solver"), Locked.bSucceeded && Reeling.bSucceeded);
	TestTrue(TEXT("locked exhausted fish stays in place"),
		Locked.ProposedFishWorldPosition.Equals(LockedState.FishWorldPosition, 1e-6));
	TestTrue(TEXT("reeling exhausted fish draws it toward the rod"),
		Reeling.ProposedFishWorldPosition.X < ReelingState.FishWorldPosition.X);
	TestEqual(TEXT("exhausted fish cannot spend fish stamina"), Reeling.FishStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("locked exhausted fish cannot drain cat stamina"), Locked.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("reeling exhausted fish cannot drain cat stamina"), Reeling.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("exhausted fish contributes no active propulsion"),
		Reeling.FishDriveAccelerationCentimetersPerSecondSquared, 0.0, 1e-9);
	TestEqual(TEXT("reeling an exhausted fish cannot add rod wear"),
		Reeling.AbsoluteRodWear, LockedState.AbsoluteRodWear, 1e-9);
	TestEqual(TEXT("exhausted simulation does not generate another break from retained wear"),
		Reeling.Outcome, ECatFightStepOutcome::None);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedReelWithoutCatStaminaTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedFishReelsAtConfiguredSpeedWithoutCatStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedReelWithoutCatStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.CatStamina = 0.0;
	State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	const FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	for (const double RemainingStrength : {0.0, 0.001, 50.0})
	{
		Config.PrimaryOperatorCatStrength = RemainingStrength;
		const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
		TestTrue(TEXT("鱼力竭后各种剩余猫力量都能继续收线"), Step.bSucceeded);
		TestEqual(TEXT("收尾使用配置速度而非剩余搏斗力量"), Step.RequestedReelDistanceCentimeters,
			Config.ReelSpeedCentimetersPerSecond * Config.FixedStepSeconds, 1e-6);
		TestTrue(TEXT("收尾确实缩短鱼线并把鱼拉近"), Step.LineLengthCentimeters < State.LineLengthCentimeters
			&& Step.ProposedFishWorldPosition.X < State.FishWorldPosition.X);
		TestEqual(TEXT("收尾不伪造猫当前力量"), Step.OperatorCatStrength, RemainingStrength);
		TestEqual(TEXT("收尾不扣猫体力"), Step.CatStaminaDrain, 0.0);
		TestEqual(TEXT("收尾不扣鱼体力"), Step.FishStaminaDrain, 0.0);
		TestEqual(TEXT("收尾不磨损鱼竿"), Step.RodWearDelta, 0.0);
	}
	Config.PrimaryOperatorCatStrength = 0.0;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
		TestTrue(TEXT("双方零体力时持续按住仍每步拉近"), Step.bSucceeded
			&& Step.ProposedFishWorldPosition.X < State.FishWorldPosition.X);
		State.LineLengthCentimeters = Step.LineLengthCentimeters;
		State.FishVelocityCentimetersPerSecond = Step.ResolvedFishVelocityCentimetersPerSecond;
		State.FishWorldPosition = Step.ProposedFishWorldPosition;
	}
	const double ReelingLineLength = State.LineLengthCentimeters;
	for (const auto Action : {ECatFightCatAction::None, ECatFightCatAction::Slack})
	{
		State.CatAction = Action;
		const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
		TestTrue(TEXT("松开左键或放线仍是合法步骤"), Step.bSucceeded);
		TestEqual(TEXT("松开左键或放线不会自动收线"), Step.RequestedReelDistanceCentimeters, 0.0);
		TestTrue(TEXT("松开左键或放线不会缩短线长"), Step.LineLengthCentimeters >= ReelingLineLength);
	}
	State.CatAction = ECatFightCatAction::Pull;
	State.bOperatorPresent = false;
	const auto Unattended = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
	TestTrue(TEXT("没有操作手时步骤有效"), Unattended.bSucceeded);
	TestEqual(TEXT("没有操作手不能凭残留左键状态自动收线"), Unattended.RequestedReelDistanceCentimeters, 0.0);
	State.bOperatorPresent = true;
	State.bFishExhausted = false;
	State.FishStamina = 100.0;
	const auto LiveFish = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("活鱼阶段零力量仍是合法步骤"), LiveFish.bSucceeded);
	TestEqual(TEXT("活鱼阶段仍保留零力量不能收线的限制"), LiveFish.RequestedReelDistanceCentimeters, 0.0);
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.FishWorldPosition = FVector(0.0, 0.0, -100.0);
	State.LineLengthCentimeters = 102.0;
	const auto Endpoint = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
	TestTrue(TEXT("零体力收尾在竿尖正下方仍有效"), Endpoint.bSucceeded);
	TestEqual(TEXT("收尾不能收过垂直距离下限"), Endpoint.LineLengthCentimeters, 100.0);
	TestTrue(TEXT("收尾不会伪造鱼的水平游动"), Endpoint.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPhysicalAssistEndpointTest,
	"Catfishing.Unit.Fishing.Simulation.PhysicalEndpointMotionDoesNotGrantReelingStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPhysicalAssistEndpointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.CatStamina = 0.0;
	auto Constraint = MakeHeldConstraint();
	Constraint.RodTipWorldPosition = FVector(-15.0, 0.0, 0.0);
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("actual endpoint motion is accepted while the operator is exhausted"), Step.bSucceeded);
	TestEqual(TEXT("physical assistance cannot grant the operator virtual reel strength"), Step.RequestedReelDistanceCentimeters, 0.0);
	TestEqual(TEXT("exhausted operator cannot charge another player's stamina"), Step.CatStaminaDrain, 0.0);
	TestTrue(TEXT("line constraint observes the actual moved rod endpoint"),
		Step.Trace.ConstraintRodEndWorldPosition.Equals(Constraint.RodTipWorldPosition, 1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingEqualStrengthConstraintTest,
	"Catfishing.Unit.Fishing.Simulation.ObservedEndpointKeepsTautLineConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingEqualStrengthConstraintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorMassKilograms = Config.FishMassKilograms;
	Config.FishStrength = Config.PrimaryOperatorCatStrength;
	Config.StrongConfrontationConfirmationSeconds = Config.FixedStepSeconds;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("equal-strength step succeeds"), Step.bSucceeded);
	TestTrue(TEXT("equal opposing drives become a natural stalemate"), Step.bStalemate);
	TestTrue(TEXT("fish outward intent is canceled at its endpoint"),
		Step.ProposedFishWorldPosition.Equals(FVector(500.0, 0.0, 0.0), 1e-6));
	TestTrue(TEXT("constraint keeps the observed endpoint unchanged"),
		Step.Trace.ConstraintRodEndWorldPosition.IsNearlyZero(1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStrongFishContinuousFightTest,
	"Catfishing.Unit.Fishing.Simulation.StrongFishDoesNotTerminateAtFormerBreakThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStrongFishContinuousFightTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 最近联机的 72.406 力量黑鱼曾在 0.41 秒被旧承载阈值结束；覆盖松左键和持续收线。
	for (const double FishStrength : {53.38, 72.406, 150.0})
	{
		for (const ECatFightCatAction Action : {ECatFightCatAction::None, ECatFightCatAction::Pull})
		{
			auto Config = MakeConfig();
			Config.FixedStepSeconds = 0.05;
			Config.PrimaryOperatorMassKilograms = 5.0;
			Config.FishStrength = FishStrength;
			Config.FishMassKilograms = FishStrength / Config.StrengthPerKilogram;
			Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
			Config.MaximumLineLengthCentimeters = 1500.0;
			auto State = MakeState(Action);
			auto Rod = MakeHeldConstraint();
			bool bSawStrongConfrontation = false;
			for (int32 Index = 0; Index < 60; ++Index)
			{
				const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
				if (!TestTrue(TEXT("强鱼的连续受力步骤有效"), Step.bSucceeded)) return false;
				if (!TestEqual(TEXT("力量高于猫和旧承载值仍继续搏斗"), Step.Outcome, ECatFightStepOutcome::None)) return false;
				TestTrue(TEXT("发布的鱼线张力始终有限且非负"), FMath::IsFinite(Step.LineTensionNewtons) && Step.LineTensionNewtons >= 0.0);
				bSawStrongConfrontation |= Step.bStrongConfrontation;
				State.FishVelocityCentimetersPerSecond = Step.ResolvedFishVelocityCentimetersPerSecond;
				State.FishWorldPosition = Step.ProposedFishWorldPosition;
				State.LineLengthCentimeters = Step.LineLengthCentimeters;
				State.CatStamina -= Step.CatStaminaDrain;
				State.FishStamina -= Step.FishStaminaDrain;
				State.AbsoluteRodWear = Step.AbsoluteRodWear;
				State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
			}
			TestTrue(TEXT("已经跨过强对抗确认窗口"), bSawStrongConfrontation);
			TestTrue(TEXT("取消阈值不会取消体力与耐久结算"),
				State.CatStamina < 100.0 && State.FishStamina < 100.0 && State.AbsoluteRodWear > 0.0);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingConfrontationPresentationOnlyTest,
	"Catfishing.Unit.Fishing.Simulation.ConfrontationClassificationCannotChangePhysicsOrOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingConfrontationPresentationOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Config = MakeConfig();
	Config.FishStrength = 150.0;
	auto State = MakeState(ECatFightCatAction::Pull);
	Config.StrongConfrontationConfirmationSeconds = 0.0;
	const auto Confirmed = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	Config.StrongConfrontationConfirmationSeconds = 2.0;
	const auto Pending = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("只有表现确认状态不同"), Confirmed.bSucceeded && Pending.bSucceeded
		&& Confirmed.bStrongConfrontation && !Pending.bStrongConfrontation);
	TestEqual(TEXT("强对抗不结束仍有耐久的搏斗"), Confirmed.Outcome, ECatFightStepOutcome::None);
	TestEqual(TEXT("确认时间不改变终局"), Confirmed.Outcome, Pending.Outcome);
	TestTrue(TEXT("确认时间不改变鱼端位置"), Confirmed.ProposedFishWorldPosition.Equals(Pending.ProposedFishWorldPosition, 1e-9));
	TestEqual(TEXT("确认时间不改变真实竿接收的张力"), Confirmed.LineTensionNewtons, Pending.LineTensionNewtons);
	TestEqual(TEXT("确认时间不改变实际收线"), Confirmed.ActualReelDistanceCentimeters, Pending.ActualReelDistanceCentimeters);
	TestEqual(TEXT("确认时间不改变猫耗体"), Confirmed.CatStaminaDrain, Pending.CatStaminaDrain);
	TestEqual(TEXT("确认时间不改变鱼耗体"), Confirmed.FishStaminaDrain, Pending.FishStaminaDrain);
	TestEqual(TEXT("确认时间不改变耐久磨损"), Confirmed.RodWearDelta, Pending.RodWearDelta);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStrengthAccelerationTest,
	"Catfishing.Unit.Fishing.Simulation.ForceConversionAndMassDriveAccelerationWhileReelUsesCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStrengthAccelerationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FishStrength = 7.65;
	const FCatFightStepResult WeakFish = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("strength-driven step succeeds"), WeakFish.bSucceeded);
	TestEqual(TEXT("fish acceleration uses its independent mass"),
		WeakFish.FishDriveAccelerationCentimetersPerSecondSquared, 255.0, 1e-9);
	TestEqual(TEXT("weak fish keeps behavior speed before the line constrains it"),
		WeakFish.IntendedSwimSpeedCentimetersPerSecond,
		Config.FishFullEffortSpeedCentimetersPerSecond, 1e-9);

	Config.PrimaryOperatorCatStrength = 5.0;
	const FCatFightStepResult WeakCatReel = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::Pull), MakeHeldConstraint(), -FVector::ForwardVector);
	TestEqual(TEXT("reel intent remains configured speed and actual progress is force limited"),
		WeakCatReel.RequestedReelDistanceCentimeters, 8.0, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingHoldAndRecoveryTest,
	"Catfishing.Unit.Fishing.Simulation.RightButtonAtLineLimitUsesOrdinaryLockedConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingHoldAndRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorMassKilograms = Config.FishMassKilograms;
	FCatFightSimulationState LockedState = MakeState(ECatFightCatAction::None);
	LockedState.CatStamina = 50.0;
	const FCatFightStepResult Locked = FCatFishingFightSimulator::Step(
		Config, LockedState, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("locked taut line creates isometric cat cost"), Locked.CatStaminaDrain > 0.0);

	FCatFightSimulationState ReleasedState = LockedState;
	ReleasedState.CatAction = ECatFightCatAction::Slack;
	const FCatFightStepResult Released = FCatFishingFightSimulator::Step(
		Config, ReleasedState, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("free spool inside the line limit restores cat stamina"), Released.CatStaminaDrain < 0.0);

	FCatFightSimulationState MaxedState = ReleasedState;
	MaxedState.LineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	MaxedState.FishWorldPosition = FVector(Config.MaximumLineLengthCentimeters, 0.0, 0.0);
	const FCatFightStepResult Maxed = FCatFishingFightSimulator::Step(
		Config, MaxedState, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("maximum line length still creates physical tension"), Maxed.NormalizedTension > 0.0);
	TestFalse(TEXT("right button at maximum line length cannot restore stamina"), Maxed.bSlackRecoveryActive);
	TestTrue(TEXT("outward opposition at maximum line length costs both participants and rod durability"),
		Maxed.CatStaminaDrain > 0.0 && Maxed.FishStaminaDrain > 0.0 && Maxed.RodWearDelta > 0.0);

	// 保留同一物理状态，只改变右键输入；整个受力与资源结果应与普通锁线一致。
	FCatFightRodConstraintInput MovingConstraint = MakeHeldConstraint();
	MovingConstraint.CarrierVelocityCentimetersPerSecond = FVector(10.0, 0.0, 0.0);
	MovingConstraint.CatRodExertionSquaredSeconds = 0.04;
	MovingConstraint.CatRodPositiveWorkRadians = 0.02;
	Config.FishStrength = 80.0;
	for (const auto& Constraint : {MakeHeldConstraint(), MovingConstraint})
	{
		for (const auto& FishDirection : {FVector::ForwardVector, -FVector::ForwardVector})
		{
			MaxedState.CatAction = ECatFightCatAction::Slack;
			const auto RightHeld = FCatFishingFightSimulator::Step(Config, MaxedState, Constraint, FishDirection);
			MaxedState.CatAction = ECatFightCatAction::None;
			const auto NoRightButton = FCatFishingFightSimulator::Step(Config, MaxedState, Constraint, FishDirection);
			if (!TestTrue(TEXT("满线右键与无右键的固定端和可移动端均可求解"), RightHeld.bSucceeded && NoRightButton.bSucceeded)) return false;
			TestEqual(TEXT("满线右键不切换自由线杯"), RightHeld.Trace.bFreeSpool, NoRightButton.Trace.bFreeSpool);
			TestEqual(TEXT("满线右键不改变约束张力"), RightHeld.LineTensionNewtons, NoRightButton.LineTensionNewtons, 1e-9);
			TestEqual(TEXT("满线右键不改变约束修正"), RightHeld.FishConstraintCorrectionCentimeters, NoRightButton.FishConstraintCorrectionCentimeters, 1e-9);
			TestTrue(TEXT("满线右键不改变鱼落点"), RightHeld.ProposedFishWorldPosition.Equals(NoRightButton.ProposedFishWorldPosition, 1e-9));
			TestTrue(TEXT("满线右键不改变鱼速度"), RightHeld.ResolvedFishVelocityCentimetersPerSecond.Equals(NoRightButton.ResolvedFishVelocityCentimetersPerSecond, 1e-9));
			TestTrue(TEXT("满线右键不改变竿端约束落点"), RightHeld.Trace.ConstraintRodEndWorldPosition.Equals(NoRightButton.Trace.ConstraintRodEndWorldPosition, 1e-9));
			TestEqual(TEXT("满线右键不改变已放线长"), RightHeld.LineLengthCentimeters, NoRightButton.LineLengthCentimeters, 1e-9);
			TestEqual(TEXT("满线右键不改变猫总耗体"), RightHeld.CatStaminaDrain, NoRightButton.CatStaminaDrain, 1e-9);
			TestEqual(TEXT("满线右键不改变主控支撑耗体"), RightHeld.GetRodActionStaminaDrain(), NoRightButton.GetRodActionStaminaDrain(), 1e-9);
			TestEqual(TEXT("满线右键不改变鱼耗体"), RightHeld.FishStaminaDrain, NoRightButton.FishStaminaDrain, 1e-9);
			TestEqual(TEXT("满线右键不增加第二份磨损"), RightHeld.RodWearDelta, NoRightButton.RodWearDelta, 1e-9);
			TestEqual(TEXT("满线右键不改变终局"), RightHeld.Outcome, NoRightButton.Outcome);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingContinuousFishPropulsionTest,
	"Catfishing.Unit.Fishing.Simulation.ContinuousFishEffortChangesPropulsionWithoutChangingWaterDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingContinuousFishPropulsionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const double Dt : {0.025, 0.05, 0.1})
	{
		for (const double Effort : {0.2, 0.5, 1.0})
		{
			auto Config = MakeConfig();
			Config.FixedStepSeconds = Dt;
			Config.MaximumLineLengthCentimeters = 10000.0;
			auto State = MakeState(ECatFightCatAction::Slack);
			State.FishEffortRatio = Effort;
			for (int32 Index = 0; Index < FMath::RoundToInt(4.0 / Dt); ++Index)
			{
				const auto Step = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
				if (!TestTrue(TEXT("各出力与步长均可推进"), Step.bSucceeded)) return false;
				TestEqual(TEXT("实际推力独立乘出力比例"), Step.Trace.FishThrustNewtons,
					Config.FishStrength * Config.ForcePerStrengthNewtons * Effort, 1e-9);
				TestEqual(TEXT("水阻保持满出力参考校准"), Step.Trace.FishLinearDragKilogramsPerSecond,
					100.0 * Config.FishStrength * Config.ForcePerStrengthNewtons / Config.FishFullEffortSpeedCentimetersPerSecond, 1e-9);
				TestEqual(TEXT("未满线右键恢复仍免鱼耗体"), Step.FishStaminaDrain, 0.0);
				State.FishWorldPosition = Step.ProposedFishWorldPosition;
				State.FishVelocityCentimetersPerSecond = Step.ResolvedFishVelocityCentimetersPerSecond;
				State.LineLengthCentimeters = Step.LineLengthCentimeters;
			}
			TestEqual(TEXT("降低出力确实降低稳态自由游速"), State.FishVelocityCentimetersPerSecond.X,
				Config.FishFullEffortSpeedCentimetersPerSecond * Effort, 0.001);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingZeroEffortInertiaTest,
	"Catfishing.Unit.Fishing.Simulation.ZeroFishEffortPreservesInertiaButExhaustionClearsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingZeroEffortInertiaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Config = MakeConfig();
	auto State = MakeState(ECatFightCatAction::Slack);
	State.FishEffortRatio = 0.0;
	State.FishVelocityCentimetersPerSecond = FVector(40.0, 0.0, 0.0);
	const auto Coasting = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("活鱼零出力仍由惯性前进并被水阻减速"), Coasting.bSucceeded
		&& Coasting.ResolvedFishVelocityCentimetersPerSecond.X > 0.0
		&& Coasting.ResolvedFishVelocityCentimetersPerSecond.X < State.FishVelocityCentimetersPerSecond.X);
	TestEqual(TEXT("零出力关闭主动推力"), Coasting.Trace.FishThrustNewtons, 0.0);
	TestEqual(TEXT("零出力没有鱼耗体"), Coasting.FishStaminaDrain, 0.0);
	TestEqual(TEXT("零出力不等于鱼力竭"), Coasting.Outcome, ECatFightStepOutcome::None);
	State.CatAction = ECatFightCatAction::None;
	Config.FishFullEffortRodWearPerSecond = 20.0;
	const auto PassiveLoad = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("旧惯性仍可形成真实张力"), PassiveLoad.bSucceeded && PassiveLoad.LineTensionNewtons > 0.0);
	TestEqual(TEXT("旧挣扎标签不能在零出力时制造鱼磨损"), PassiveLoad.RodWearDelta, 0.0);
	TestEqual(TEXT("被动负载不能制造鱼耗体"), PassiveLoad.FishStaminaDrain, 0.0);
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.LineLengthCentimeters = 800.0;
	const auto Exhausted = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ZeroVector);
	TestTrue(TEXT("真正力竭继续使用无自主漂游收尾"), Exhausted.bSucceeded
		&& Exhausted.ResolvedFishVelocityCentimetersPerSecond.IsNearlyZero());
	State.FishEffortRatio = -0.01;
	TestFalse(TEXT("负出力输入拒绝进入物理解算"), FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ZeroVector).bSucceeded);
	State.FishEffortRatio = 1.01;
	TestFalse(TEXT("超过正常满力的输入拒绝进入物理解算"), FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ZeroVector).bSucceeded);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentProgressGeometryTest,
	"Catfishing.Unit.Fishing.Simulation.FishIntentCostUsesFinalProgressWithoutLineAngleOrTensionGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentProgressGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = MakeConfig();
	const auto Rod = MakeHeldConstraint();
	for (const double Effort : {0.0, 0.25, 0.5, 1.0})
	{
		auto State = MakeState(ECatFightCatAction::None);
		State.FishEffortRatio = Effort;
		auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
		Step.ProposedFishWorldPosition = State.FishWorldPosition;
		Step.LineTensionNewtons = Config.FishStrength * Config.ForcePerStrengthNewtons * 0.5;
		TestTrue(TEXT("受控最终落点快照可重新结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		const double IntendedDistance = Config.FishFullEffortSpeedCentimetersPerSecond * Effort * Config.FixedStepSeconds;
		TestEqual(TEXT("最终没有移动时保留全部未完成意图"), Step.FishUnfulfilledDistanceCentimeters, IntendedDistance, 1e-9);
		TestEqual(TEXT("最终停住按未完成意图米数计费"), Step.FishStaminaDrain,
			IntendedDistance / 100.0 * Config.FishStaminaPerUnfulfilledMeter, 1e-9);
		const double OutwardCost = Step.FishStaminaDrain;
		State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
		TestTrue(TEXT("旧表现意图不参与鱼费用"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		TestEqual(TEXT("同一意图缺失不因平静标签降价"), Step.FishStaminaDrain, OutwardCost, 1e-9);
		Step.FishEffortDirection = FVector::RightVector;
		TestTrue(TEXT("纯横向主动方向接受同一张力快照"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		TestEqual(TEXT("横向主动意图被挡住仍按同一缺失距离收费"), Step.FishStaminaDrain, OutwardCost, 1e-9);
		Step.FishEffortDirection = -FVector::ForwardVector;
		TestTrue(TEXT("向竿尖主动游动接受同一张力快照"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		TestEqual(TEXT("向内主动意图被挡住仍按同一缺失距离收费"), Step.FishStaminaDrain, OutwardCost, 1e-9);
		Step.LineTensionNewtons = 0.0;
		TestTrue(TEXT("无张力最终快照也可重算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		TestEqual(TEXT("张力归零不会豁免实际未完成的主动意图"), Step.FishStaminaDrain, OutwardCost, 1e-9);
		Step.ProposedFishWorldPosition = State.FishWorldPosition + Step.FishEffortDirection * IntendedDistance * 0.4;
		TestTrue(TEXT("最终落点增加主动进展后重算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step));
		TestEqual(TEXT("最终实际完成四成意图后只支付六成缺失"), Step.FishStaminaDrain, OutwardCost * 0.6, 1e-9);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMotionPresentationIndependenceTest,
	"Catfishing.Unit.Fishing.Simulation.MotionPresentationCannotChangePhysicsStaminaOrWear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingMotionPresentationIndependenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Config = MakeConfig();
	Config.FishFullEffortRodWearPerSecond = 8.0;
	// 0.45处于表现滞回的0.4/0.55之间，强弱档都可能合法地观察到同一真实出力。
	auto State = MakeState(ECatFightCatAction::None);
	State.FishEffortRatio = 0.45;
	const auto StrongDisplay = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	const auto WeakDisplay = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("同出力的两种显示标签均可推进"), StrongDisplay.bSucceeded && WeakDisplay.bSucceeded);
	TestTrue(TEXT("夹具确实包含鱼端持续出力、双边费用和基础磨损"), WeakDisplay.Trace.FishThrustNewtons > 0.0
		&& WeakDisplay.FishStaminaDrain > 0.0 && WeakDisplay.CatStaminaDrain > 0.0 && WeakDisplay.RodWearDelta > 0.0);
	TestTrue(TEXT("标签差异仍可用于诊断观察"), StrongDisplay.Trace.bStruggling && !WeakDisplay.Trace.bStruggling);
	TestEqual(TEXT("显示标签不改变主动推力"), StrongDisplay.Trace.FishThrustNewtons, WeakDisplay.Trace.FishThrustNewtons, 1e-9);
	TestEqual(TEXT("显示标签不改变共同张力"), StrongDisplay.LineTensionNewtons, WeakDisplay.LineTensionNewtons, 1e-9);
	TestTrue(TEXT("显示标签不改变鱼位置或惯性"), StrongDisplay.ProposedFishWorldPosition.Equals(WeakDisplay.ProposedFishWorldPosition, 1e-9)
		&& StrongDisplay.ResolvedFishVelocityCentimetersPerSecond.Equals(WeakDisplay.ResolvedFishVelocityCentimetersPerSecond, 1e-9));
	TestEqual(TEXT("显示标签不改变鱼耗体"), StrongDisplay.FishStaminaDrain, WeakDisplay.FishStaminaDrain, 1e-9);
	TestEqual(TEXT("显示标签不改变猫耗体"), StrongDisplay.CatStaminaDrain, WeakDisplay.CatStaminaDrain, 1e-9);
	TestEqual(TEXT("显示标签不改变鱼竿磨损"), StrongDisplay.RodWearDelta, WeakDisplay.RodWearDelta, 1e-9);
	Config.FishFullEffortRodWearPerSecond = 0.0;
	const auto WithoutBaseWear = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestEqual(TEXT("低显示档仍按实际出力平方计入基础磨损"), WeakDisplay.RodWearDelta - WithoutBaseWear.RodWearDelta,
		8.0 * FMath::Square(State.FishEffortRatio) * Config.FixedStepSeconds * Config.TautRodWearMultiplier, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSimulationTraceTest,
	"Catfishing.Unit.Fishing.Simulation.TraceContainsUnitCheckedForceAndConstraintInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSimulationTraceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("trace sample solves"), Step.bSucceeded);
	TestEqual(TEXT("successful trace has no rejection reason"),
		Step.RejectReason, ECatFightSimulationRejectReason::None);
	TestTrue(TEXT("trace records accepted input and finalization"),
		Step.Trace.bInputAccepted && Step.Trace.bFinalizeInputAccepted);
	TestEqual(TEXT("trace records the configured fixed step"),
		Step.Trace.FixedStepSeconds, Config.FixedStepSeconds, 1e-9);
	TestEqual(TEXT("trace force conversion matches strength times newtons per strength"),
		Step.Trace.CatForceNewtons,
		Step.Trace.EffectiveCatStrength * Config.ForcePerStrengthNewtons, 1e-9);
	TestEqual(TEXT("trace fish force conversion matches active strength"),
		Step.Trace.FishThrustNewtons,
		Step.Trace.ActiveFishStrength * Config.ForcePerStrengthNewtons, 1e-9);
	TestEqual(TEXT("trace exposes the operator body mass"),
		Step.Trace.OperatorBodyMassKilograms, Config.PrimaryOperatorMassKilograms, 1e-9);
	TestTrue(TEXT("trace records finite geometry and tension intermediates"),
		FMath::IsFinite(Step.Trace.DistanceBeforeCentimeters)
		&& FMath::IsFinite(Step.Trace.ExistingPositionErrorCentimeters)
		&& FMath::IsFinite(Step.Trace.RequiredTensionAtCurrentLengthNewtons)
		&& FMath::IsFinite(Step.Trace.LineTensionNewtons));
	TestEqual(TEXT("trace line load uses the result line load"),
		Step.Trace.NormalizedLineLoad, Step.NormalizedLineLoad, 1e-9);
	TestEqual(TEXT("trace wear load uses the directional line load"),
		Step.Trace.WearLoad, Step.NormalizedLineLoad, 1e-9);
	TestEqual(TEXT("trace stamina after-step is derived from the input state"),
		Step.Trace.FishStaminaAfterStep, FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain), 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSimulationRejectReasonTest,
	"Catfishing.Unit.Fishing.Simulation.InvalidStepExposesFailClosedReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSimulationRejectReasonTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig InvalidConfig = MakeConfig();
	InvalidConfig.FixedStepSeconds = 0.0;
	const FCatFightStepResult ConfigRejected = FCatFishingFightSimulator::Step(
		InvalidConfig, MakeState(ECatFightCatAction::None), MakeHeldConstraint(), FVector::ForwardVector);
	TestFalse(TEXT("invalid config is rejected"), ConfigRejected.bSucceeded);
	TestEqual(TEXT("invalid config exposes its reason"), ConfigRejected.RejectReason,
		ECatFightSimulationRejectReason::InvalidConfig);

	const FCatFightStepResult DirectionRejected = FCatFishingFightSimulator::Step(
		MakeConfig(), MakeState(ECatFightCatAction::None), MakeHeldConstraint(), FVector::UpVector);
	TestFalse(TEXT("vertical-only live fish direction is rejected"), DirectionRejected.bSucceeded);
	TestEqual(TEXT("invalid fish direction exposes its reason"), DirectionRejected.RejectReason,
		ECatFightSimulationRejectReason::InvalidFishDirection);
	TestTrue(TEXT("rejected trace also carries the same reason"),
		DirectionRejected.Trace.RejectReason == DirectionRejected.RejectReason);
	return !HasAnyErrors();
}

#endif
