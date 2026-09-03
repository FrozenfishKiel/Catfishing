#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingFightWorkModel.h"

namespace CatFishingCoupledSimulationTest
{
	FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.1;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 10.0;
		Config.RodEffectiveMassKilograms = 2.0;
		Config.FishMassKilograms = 3.0;
		Config.FishStrength = 40.0;
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

#endif
