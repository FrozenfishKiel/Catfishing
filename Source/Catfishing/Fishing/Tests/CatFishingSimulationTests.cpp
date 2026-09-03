#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingFightWorkModel.h"
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
		Config.AccelerationPerStrength = 5.0;
		Config.DriveResponseSeconds = 1.0;
		Config.RodStrength = 100.0;
		Config.CatStaminaMaximum = 100.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishCalmSpeedCentimetersPerSecond = 25.0;
		Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingShoreBeachingSolveTest,
	"Catfishing.Unit.Fishing.Simulation.LandwardHaulCanBeachFishWhenOldPointIsOutsideMovedConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingShoreBeachingSolveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishShoreContactInput Input;
	Input.CurrentFishWorldPosition = FVector(0.0, 300.0, 0.0);
	Input.CandidateFishWorldPosition = FVector(0.0, 200.0, 0.0);
	Input.ResolvedWaterWorldPosition = FVector(0.0, 250.0, 0.0);
	Input.WaterwardDirection = FVector(0.0, 1.0, 0.0);
	Input.RodTipWorldPosition = FVector::ZeroVector;
	Input.PreviousLineLengthCentimeters = 200.0;
	Input.ProposedLineLengthCentimeters = 200.0;
	Input.MaximumConstraintDistanceCentimeters = 200.0;

	const FCatFishShoreContactResult Blocked =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestFalse(TEXT("legacy water-only solve cannot reuse an old point outside the moved line sphere"),
		Blocked.bSucceeded);

	Input.bAllowBeaching = true;
	const FCatFishShoreContactResult Beached =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Input);
	TestTrue(TEXT("landward haul keeps the reachable shore-crossing candidate"), Beached.bSucceeded);
	TestTrue(TEXT("shore-crossing result explicitly requests the grounded lifecycle"), Beached.bBeached);
	TestTrue(TEXT("beaching preserves the candidate XY for the later ground trace"),
		Beached.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));
	TestEqual(TEXT("beaching does not manufacture paid-out line"),
		Beached.LineLengthCentimeters, Input.ProposedLineLengthCentimeters, 1e-9);
	return !HasAnyErrors();
}

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
	TestFalse(TEXT("fish does not beach without cat-side hauling"), Result.bBeached);
	TestTrue(TEXT("water-only shore response does not use the land candidate"),
		!Result.FishWorldPosition.Equals(Input.CandidateFishWorldPosition, 1e-6));
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
	Input.FishStrength = 25.0;
	Input.NormalizedTension = 1.0;
	Input.NormalizedFishLineLoad = 1.0;
	Input.RodLineAlignment = 0.0;
	Input.RodPhysicsLengthCentimeters = 100.0;
	const FCatFishingRodResistanceResult OneMeter = FCatFishingRodResistanceModel::Evaluate(Input);
	TestTrue(TEXT("one-meter configured rod solves"), OneMeter.bSucceeded);
	TestEqual(TEXT("one-meter rod consumes half the rotation capacity"),
		OneMeter.RotationSpeedMultiplier, 0.5, 1e-9);

	Input.RodPhysicsLengthCentimeters = 200.0;
	const FCatFishingRodResistanceResult TwoMeters = FCatFishingRodResistanceModel::Evaluate(Input);
	TestTrue(TEXT("two-meter configured rod solves"), TwoMeters.bSucceeded);
	TestTrue(TEXT("two-meter configured rod reaches the torque stall"), TwoMeters.bRotationStalled);
	TestEqual(TEXT("stalled rod has no available angular speed"),
		TwoMeters.RotationSpeedMultiplier, 0.0, 1e-9);

	Input.NormalizedTension = 0.0;
	const FCatFishingRodResistanceResult Slack = FCatFishingRodResistanceModel::Evaluate(Input);
	TestEqual(TEXT("slack line leaves rod rotation unrestricted"),
		Slack.RotationSpeedMultiplier, 1.0, 1e-9);
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
	TestEqual(TEXT("reeling shortens only the constraint length"), Reeling.LineLengthCentimeters, 492.0, 1e-6);
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
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Slack);
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("weak fish free-spool step solves"), Step.bSucceeded);
	TestEqual(TEXT("free fish keeps its behavior-defined struggle speed"),
		Step.IntendedSwimSpeedCentimetersPerSecond,
		Config.FishStruggleSpeedCentimetersPerSecond, 1e-9);
	TestTrue(TEXT("free-spool weak fish visibly changes world position"),
		Step.ProposedFishWorldPosition.X > State.FishWorldPosition.X + 1.0);
	TestTrue(TEXT("free spool pays line out for the actual swim"),
		Step.LineLengthCentimeters > State.LineLengthCentimeters);
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
	"Catfishing.Unit.Fishing.Simulation.BackingAwayMovesCarrierEndpointAndDoesNotDoubleReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingEndpointIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightRodConstraintInput Rod = MakeHeldConstraint();
	Rod.CarrierDesiredVelocityCentimetersPerSecond = -FVector::ForwardVector * 300.0;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), Rod, FVector::ForwardVector);
	TestTrue(TEXT("endpoint intent creates one coupled constraint"), Step.ConstraintErrorCentimeters > 0.0);
	TestEqual(TEXT("backing away does not change paid-out length"), Step.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("backing away does not pretend to reel"), Step.RequestedReelDistanceCentimeters, 0.0, 1e-6);
	TestTrue(TEXT("backing-away intent contributes to cat work"), Step.CatIntendedLineDistanceCentimeters > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMassSplitTest,
	"Catfishing.Unit.Fishing.Simulation.ConstraintCorrectionUsesMassNotStrengthOutcomeBranches",
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
	TestTrue(TEXT("heavier cat allocates more correction to fish endpoint"),
		Heavy.FishConstraintCorrectionCentimeters > Light.FishConstraintCorrectionCentimeters);
	TestTrue(TEXT("stronger fish produces a bounded carrier target"),
		Light.CarrierTargetPullSpeedCentimetersPerSecond > 0.0);
	TestEqual(TEXT("strong fish does not directly create a terminal cat-water outcome"),
		Heavy.Outcome, ECatFightStepOutcome::None);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingIsometricWorkTest,
	"Catfishing.Unit.Fishing.Simulation.BlockedIntentConsumesStaminaWithoutActualMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingIsometricWorkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightWorkInput Input;
	Input.Strength = 50.0;
	Input.IntendedLineDistanceCentimeters = 10.0;
	Input.ActualLineDistanceCentimeters = 0.0;
	Input.IsometricEffortMultiplier = 1.0;
	Input.CostPerStrengthCentimeter = 0.002;
	double Drain = 0.0;
	double Effort = 0.0;
	TestTrue(TEXT("work calculation succeeds"), FCatFishingFightWorkModel::ComputeDrain(Input, Drain, Effort));
	TestEqual(TEXT("blocked intent remains effective effort"), Effort, 10.0, 1e-9);
	TestEqual(TEXT("blocked intent drains stamina"), Drain, 1.0, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStrengthNormalizedStaminaTest,
	"Catfishing.Unit.Fishing.Simulation.FishStrengthChangesMotionNotStaminaCostForSameLineEffort",
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
	TestEqual(TEXT("相同沿线努力不因绝对力量差产生体力倍率"),
		WeakFish.FishStaminaDrain, StrongFish.FishStaminaDrain, 1e-9);
	TestTrue(TEXT("标准努力仍会消耗鱼体力"), WeakFish.FishStaminaDrain > 0.0);
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
	TestEqual(TEXT("exhausted fish contributes no carrier correction"),
		Reeling.CarrierConstraintCorrectionCentimeters, 0.0, 1e-9);
	TestEqual(TEXT("exhausted fish contributes no carrier target speed"),
		Reeling.CarrierTargetPullSpeedCentimetersPerSecond, 0.0, 1e-9);
	TestEqual(TEXT("exhausted fish does not restrict backing-away speed"),
		Reeling.CarrierAwaySpeedMultiplier, 1.0, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingHelperOnlyWorkTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedPrimaryDoesNotSuppressHelperWork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingHelperOnlyWorkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 35.0;
	Config.HelperMassKilograms = 8.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.CatStamina = 0.0;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("helper-only coupled step succeeds"), Step.bSucceeded);
	TestTrue(TEXT("active helper can still request reel motion"), Step.RequestedReelDistanceCentimeters > 0.0);
	TestTrue(TEXT("group work is not capped by exhausted primary stamina"), Step.CatStaminaDrain > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingEqualStrengthConstraintTest,
	"Catfishing.Unit.Fishing.Simulation.EqualStrengthNaturallyStalematesWithoutCarrierPull",
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
	TestEqual(TEXT("equal strength does not move the cat endpoint"),
		Step.CarrierConstraintCorrectionCentimeters, 0.0, 1e-6);
	TestEqual(TEXT("equal strength does not create a carrier target"),
		Step.CarrierTargetPullSpeedCentimetersPerSecond, 0.0, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStrengthAccelerationTest,
	"Catfishing.Unit.Fishing.Simulation.SharedStrengthAccelerationDrivesBothSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStrengthAccelerationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FishStrength = 7.65;
	const FCatFightStepResult WeakFish = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::None), MakeHeldConstraint(), FVector::ForwardVector);
	TestTrue(TEXT("strength-driven step succeeds"), WeakFish.bSucceeded);
	TestEqual(TEXT("cat acceleration is strength times shared coefficient"),
		WeakFish.CatDriveAccelerationCentimetersPerSecondSquared, 250.0, 1e-9);
	TestEqual(TEXT("fish acceleration is strength times shared coefficient"),
		WeakFish.FishDriveAccelerationCentimetersPerSecondSquared, 38.25, 1e-9);
	TestEqual(TEXT("weak fish keeps behavior speed before the line constrains it"),
		WeakFish.IntendedSwimSpeedCentimetersPerSecond,
		Config.FishStruggleSpeedCentimetersPerSecond, 1e-9);
	TestEqual(TEXT("7.65 strength fish cannot pull a 50 strength cat"),
		WeakFish.CarrierTargetPullSpeedCentimetersPerSecond, 0.0, 1e-9);
	TestEqual(TEXT("weak fish does not restrict cat movement"),
		WeakFish.CarrierAwaySpeedMultiplier, 1.0, 1e-9);

	Config.PrimaryOperatorCatStrength = 5.0;
	const FCatFightStepResult WeakCatReel = FCatFishingFightSimulator::Step(
		Config, MakeState(ECatFightCatAction::Pull), MakeHeldConstraint(), -FVector::ForwardVector);
	TestEqual(TEXT("cat reel intent uses the same acceleration conversion"),
		WeakCatReel.RequestedReelDistanceCentimeters, 2.5, 1e-9);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingHoldAndRecoveryTest,
	"Catfishing.Unit.Fishing.Simulation.LockedTensionCostsStaminaAndOnlyReleasedFreeSpoolRecovers",
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
	TestEqual(TEXT("free spool blocked by maximum line length cannot restore stamina"),
		Maxed.CatStaminaDrain, 0.0, 1e-9);
	return !HasAnyErrors();
}

#endif
