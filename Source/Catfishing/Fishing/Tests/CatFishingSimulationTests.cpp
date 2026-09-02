#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorInwardTest,
	"Catfishing.Unit.Fishing.Simulation.InwardDirectionCreatesAFreeReelWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorOutwardJudgmentTest,
	"Catfishing.Unit.Fishing.Simulation.OutwardPullJudgesSnapDragOverpowerStalemateInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorSlackTest,
	"Catfishing.Unit.Fishing.Simulation.HoldingSlackAlwaysRegensAndTautLineStillForcesJudgment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightUnattendedSlackTest,
	"Catfishing.Unit.Fishing.Simulation.UnattendedFightPaysOutThenWearsLineWithoutPlayerResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightVerticalProjectionRecoveryTest,
	"Catfishing.Unit.Fishing.Simulation.VerticalProjectionSlackRestoresHorizontalMotionWithoutAxisBias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorPriorityTest,
	"Catfishing.Unit.Fishing.Simulation.ZeroingPriorityIsFishThenCatThenRod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightMotionSolverTest,
	"Catfishing.Unit.Fishing.Simulation.MotionSolverProjectsDeterministicallyIntoWaterAndLineReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightAngleProjectionTest,
	"Catfishing.Unit.Fishing.Simulation.AngleProjectionControlsLoadDrainAndRadialReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishSteeringDeterminismTest,
	"Catfishing.Unit.Fishing.Simulation.PersonalitySteeringIsSeededSmoothAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightExhaustionThresholdTest,
	"Catfishing.Unit.Fishing.Simulation.SubDisplayUnitStaminaExhaustsAndZeroes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishSteeringShoreRedirectTest,
	"Catfishing.Unit.Fishing.Simulation.LiveFishShoreCollisionRedirectsWaterward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishShoreContactContinuityTest,
	"Catfishing.Unit.Fishing.Simulation.LiveFishShoreContactDoesNotTeleportToInset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishStaminaDrivenInwardProbabilityTest,
	"Catfishing.Unit.Fishing.Simulation.LowerStaminaRaisesInwardConeSelectionProbability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightCombinedCatStrengthTest,
	"Catfishing.Unit.Fishing.Simulation.CombinedCatStrengthAddsTwoCatsWithoutGrantingSecondInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingHeldRodConstraintTest,
	"Catfishing.Unit.Fishing.Simulation.HeldRodDirectionAndCarrierMovementAffectConstraintSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingSimulationTest
{
	// 规格快照：猫 50 / 竿强 60 / 鱼 40 → 向外游+拖 落入 ④ 僵持。
	static FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 1.0; // 用 1 秒步长让每秒速率直接可读
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.FishStrength = 40.0;
		Config.RodStrength = 60.0;
		Config.CatStaminaMaximum = 100.0;
		Config.BaseDrainMultiplier = 1.0;
		Config.StruggleDrainMultiplier = 2.0;
		Config.ReelSpeedCentimetersPerSecond = 100.0;
		Config.FishCalmSpeedCentimetersPerSecond = 25.0;
		Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
		Config.MaximumLineLengthCentimeters = 1000.0;
		Config.RodDurability = 70.0;
		return Config;
	}

	static FCatFightSimulationState MakeState(const ECatFishMotionIntent Intent, const ECatFightCatAction Action)
	{
		FCatFightSimulationState State;
		State.CatStamina = 100.0;
		State.FishStamina = 50.0;
		State.LineLengthCentimeters = 500.0;
		State.FishWorldPosition = FVector(500, 0, 0);
		State.MotionIntent = Intent;
		State.CatAction = Action;
		return State;
	}

	static FCatFightStepResult Run(const FCatFightSimulationConfig& Config, const FCatFightSimulationState& State)
	{
		const FVector Direction = State.MotionIntent == ECatFishMotionIntent::CalmOrInward
			? -FVector::ForwardVector : FVector::ForwardVector;
		return FCatFishingFightSimulator::Step(Config, State, FVector::ZeroVector, Direction);
	}

	static FCatFightStepResult RunDirection(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FVector& Direction)
	{
		return FCatFishingFightSimulator::Step(Config, State, FVector::ZeroVector, Direction);
	}
}

using namespace CatFishingSimulationTest;

bool FCatFishingHeldRodConstraintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FixedStepSeconds = 0.1;
	Config.FishCalmSpeedCentimetersPerSecond = 20.0;
	Config.ReelSpeedCentimetersPerSecond = 20.0;
	FCatFightSimulationState State = MakeState(
		ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull);
	State.LineLengthCentimeters = 500.0;

	FCatFightRodConstraintInput Aligned;
	Aligned.RodTipWorldPosition = FVector::ZeroVector;
	Aligned.RodForwardWorld = FVector::ForwardVector;
	Aligned.bRodHeld = true;
	FCatFightRodConstraintInput Sideways = Aligned;
	Sideways.RodForwardWorld = FVector::RightVector;
	const FCatFightStepResult AlignedPull = FCatFishingFightSimulator::Step(
		Config, State, Aligned, FVector::ForwardVector);
	const FCatFightStepResult SidewaysPull = FCatFishingFightSimulator::Step(
		Config, State, Sideways, FVector::ForwardVector);
	TestTrue(TEXT("aligned held-rod solve succeeds"), AlignedPull.bSucceeded);
	TestTrue(TEXT("sideways held-rod solve succeeds"), SidewaysPull.bSucceeded);
	TestTrue(TEXT("rod direction produces higher aligned leverage"),
		AlignedPull.RodLeverageMultiplier > SidewaysPull.RodLeverageMultiplier);
	TestTrue(TEXT("aligned leverage produces more fish stamina drain under the existing strength formula"),
		AlignedPull.FishStaminaDrain > SidewaysPull.FishStaminaDrain);
	TestTrue(TEXT("poor leverage costs the cat more effort"),
		SidewaysPull.CatStaminaDrain > AlignedPull.CatStaminaDrain);

	State.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput MovingAway = Aligned;
	MovingAway.CarrierVelocityCentimetersPerSecond = FVector(-300.0, 0.0, 0.0);
	MovingAway.RodTipVelocityCentimetersPerSecond = FVector(-300.0, 0.0, 0.0);
	const FCatFightStepResult Stationary = FCatFishingFightSimulator::Step(
		Config, State, Aligned, FVector::ForwardVector);
	const FCatFightStepResult BackingAway = FCatFishingFightSimulator::Step(
		Config, State, MovingAway, FVector::ForwardVector);
	TestTrue(TEXT("carrier movement contribution is measured"), BackingAway.CarrierMovementAlpha > 0.99);
	TestTrue(TEXT("backing away increases effective cat strength"),
		BackingAway.EffectiveCatStrength > Stationary.EffectiveCatStrength);
	TestTrue(TEXT("taut moving constraint produces an opposing carrier acceleration"),
		BackingAway.CarrierPullAccelerationCentimetersPerSecondSquared > 0.0);

	// 真实端点位移必须和左键共用一个误差：走路不能把鱼全量硬投影，走路+左键也不能把两种位移完整相加。
	Config.FixedStepSeconds = 0.05;
	Config.FishCalmSpeedCentimetersPerSecond = 0.0;
	Config.ReelSpeedCentimetersPerSecond = 80.0;
	Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
	State.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput WalkedAway = Aligned;
	WalkedAway.RodTipWorldPosition = FVector(-5.0, 0.0, 0.0);
	WalkedAway.RodTipVelocityCentimetersPerSecond = FVector(-100.0, 0.0, 0.0);
	WalkedAway.CarrierVelocityCentimetersPerSecond = FVector(-100.0, 0.0, 0.0);
	const FCatFightStepResult WalkingOnly = FCatFishingFightSimulator::Step(
		Config, State, WalkedAway, FVector::ForwardVector);
	State.CatAction = ECatFightCatAction::Pull;
	const FCatFightStepResult WalkingAndReeling = FCatFishingFightSimulator::Step(
		Config, State, WalkedAway, FVector::ForwardVector);
	TestTrue(TEXT("walking a taut held line distributes only part of the endpoint error to the fish"),
		WalkingOnly.FishConstraintCorrectionCentimeters > 0.0
		&& WalkingOnly.FishConstraintCorrectionCentimeters < 5.0);
	TestTrue(TEXT("the unresolved walking share constrains the carrier speed"),
		WalkingOnly.CarrierAwaySpeedMultiplier < 1.0);
	TestTrue(TEXT("walking plus reel is capped by one fish-side constraint correction budget"),
		WalkingAndReeling.FishConstraintCorrectionCentimeters
			<= Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond
				* Config.FixedStepSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("walking plus reel no longer gives the fish the full 100 plus 80 centimeters per second"),
		WalkingAndReeling.FishConstraintCorrectionCentimeters / Config.FixedStepSeconds
			< 180.0 - UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("walking and reeling publish one merged constraint error"),
		WalkingAndReeling.ConstraintErrorCentimeters > WalkingOnly.ConstraintErrorCentimeters
		&& WalkingAndReeling.RelativeConstraintSpeedCentimetersPerSecond > 0.0);

	State.CatAction = ECatFightCatAction::None;
	FCatFightRodConstraintInput WalkedTowardFish = Aligned;
	WalkedTowardFish.RodTipWorldPosition = FVector(5.0, 0.0, 0.0);
	WalkedTowardFish.RodTipVelocityCentimetersPerSecond = FVector(100.0, 0.0, 0.0);
	WalkedTowardFish.CarrierVelocityCentimetersPerSecond = FVector(100.0, 0.0, 0.0);
	const FCatFightStepResult TowardFish = FCatFishingFightSimulator::Step(
		Config, State, WalkedTowardFish, FVector::ForwardVector);
	TestEqual(TEXT("walking toward the fish creates slack instead of a phantom pull"),
		TowardFish.ConstraintErrorCentimeters, 0.0, 1e-9);
	TestEqual(TEXT("slack leaves carrier movement unrestricted"),
		TowardFish.CarrierAwaySpeedMultiplier, 1.0, 1e-9);
	return !HasAnyErrors();
}

bool FCatFishingFightCombinedCatStrengthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FishStrength = 60.0;
	Config.RodStrength = 100.0;
	const FCatFightSimulationState Pull = MakeState(
		ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull);

	const FCatFightStepResult PrimaryOnly = Run(Config, Pull);
	TestEqual(TEXT("只有主操作猫 50 力时会被 60 力的鱼拖下水"),
		static_cast<int32>(PrimaryOnly.Outcome), static_cast<int32>(ECatFightStepOutcome::DraggedIntoWater));

	Config.SecondCatStrength = 25.0;
	TestEqual(TEXT("猫总体力量等于主操作猫 50 加第二只猫 25"), Config.GetCombinedCatStrength(), 75.0);
	const FCatFightStepResult TwoCats = Run(Config, Pull);
	TestTrue(TEXT("第二只猫只贡献力量时进入合计 75 力的僵持结算"), TwoCats.bStalemate);
	TestEqual(TEXT("合计力量参与鱼体力消耗"), TwoCats.FishStaminaDrain,
		75.0 * Config.StalemateFishDrainPerCatStrength * Config.StruggleDrainMultiplier, 1e-6);
	TestEqual(TEXT("第二只猫力量预留不产生第二套输入，模拟仍只有一个 CatAction"),
		static_cast<int32>(Pull.CatAction), static_cast<int32>(ECatFightCatAction::Pull));
	return !HasAnyErrors();
}

bool FCatFishingFightSimulatorInwardTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();

	const FCatFightStepResult Pull = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull));
	TestTrue(TEXT("inward pull succeeds"), Pull.bSucceeded);
	TestEqual(TEXT("loaded inward pull drains cat stamina from current power instead of fish strength"),
		Pull.CatStaminaDrain,
		Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower * Config.PrimaryPowerAlpha, 1e-6);
	TestEqual(TEXT("loaded inward pull still drains fish stamina with the calm coefficient"),
		Pull.FishStaminaDrain, 50.0 * 0.08 * Config.BaseDrainMultiplier, 1e-6);
	TestEqual(TEXT("inward direction publishes negative alignment"), Pull.FishLineAlignment, -1.0, 1e-6);
	TestEqual(TEXT("inward direction publishes zero line load"), Pull.NormalizedLineLoad, 0.0, 1e-6);
	TestEqual(TEXT("calm intent publishes its unconstrained swim speed"),
		Pull.IntendedSwimSpeedCentimetersPerSecond, Config.FishCalmSpeedCentimetersPerSecond, 1e-6);
	TestEqual(TEXT("inward fish motion and rod traction compose"), Pull.ProposedFishWorldPosition.X, 375.0, 1e-6);
	TestEqual(TEXT("reel request shortens paid line without erasing fish-led inward motion"),
		Pull.LineLengthCentimeters, 400.0, 1e-6);
	TestEqual(TEXT("fish moving inward faster than paid line reduction creates physical slack"),
		Pull.SlackLineLengthCentimeters, 25.0, 1e-6);
	TestEqual(TEXT("inward pull has no outcome"), static_cast<int32>(Pull.Outcome), static_cast<int32>(ECatFightStepOutcome::None));
	FCatFightSimulationConfig HalfPowerConfig = Config;
	HalfPowerConfig.PrimaryOperatorCatStrength *= 0.5;
	HalfPowerConfig.PrimaryPowerAlpha = 0.5;
	const FCatFightStepResult HalfPowerPull = Run(HalfPowerConfig,
		MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull));
	TestEqual(TEXT("half power halves primary cat stamina drain"), HalfPowerPull.CatStaminaDrain,
		Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower * 0.5, 1e-9);
	TestEqual(TEXT("half power also halves the retained fish stamina formula input"),
		HalfPowerPull.FishStaminaDrain, Pull.FishStaminaDrain * 0.5, 1e-9);
	const FCatFightStepResult StrugglePull = RunDirection(Config,
		MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull), FVector::RightVector);
	TestEqual(TEXT("fish personality no longer changes cat stamina pricing at equal power"),
		StrugglePull.CatStaminaDrain, Pull.CatStaminaDrain, 1e-9);
	TestTrue(TEXT("struggling pull drains more fish stamina than calm pull"),
		StrugglePull.FishStaminaDrain > Pull.FishStaminaDrain);

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::None));
	TestEqual(TEXT("inward idle is free"), Idle.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("inward idle lets fish approach slowly"), Idle.ProposedFishWorldPosition.X, 475.0, 1e-6);
	TestEqual(TEXT("inward idle keeps line slack"), Idle.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("inward idle accumulates twenty five centimeters of slack"),
		Idle.SlackLineLengthCentimeters, 25.0, 1e-6);

	FCatFightSimulationState InwardSlackState = MakeState(
		ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Slack);
	InwardSlackState.CatStamina = 90.0;
	const FCatFightStepResult Slack = Run(Config, InwardSlackState);
	TestEqual(TEXT("inward slack behaves like idle"), Slack.ProposedFishWorldPosition.X, Idle.ProposedFishWorldPosition.X, 1e-9);
	TestEqual(TEXT("holding right regens while the fish moves inward"),
		Slack.CatStaminaDrain, -Config.SlackStaminaRegenPerSecond, 1e-9);
	TestEqual(TEXT("right input does not actively pay out while fish moves inward"),
		Slack.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("inward fish still creates slack while spool is open"),
		Slack.SlackLineLengthCentimeters, 25.0, 1e-6);
	FCatFightSimulationConfig StationaryConfig = Config;
	StationaryConfig.FishCalmSpeedCentimetersPerSecond = 0.0;
	const FCatFightStepResult StationarySlack = Run(StationaryConfig, InwardSlackState);
	TestEqual(TEXT("holding right while fish is stationary never manufactures extra line"),
		StationarySlack.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("holding right regens while the fish is stationary"),
		StationarySlack.CatStaminaDrain, -Config.SlackStaminaRegenPerSecond, 1e-9);
	return !HasAnyErrors();
}

bool FCatFishingFightSimulatorOutwardJudgmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationState Pull = MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull);

	// ④ 僵持：猫 50 / 竿 60 / 鱼 40。
	{
		const FCatFightStepResult Step = Run(MakeConfig(), Pull);
		TestTrue(TEXT("stalemate flagged"), Step.bStalemate);
		TestEqual(TEXT("stalemate rod wear = fish x 0.1"), Step.AbsoluteRodWear, 40.0 * 0.1, 1e-6);
		TestEqual(TEXT("stalemate fish drain uses the struggle multiplier"), Step.FishStaminaDrain,
			50.0 * 0.08 * MakeConfig().StruggleDrainMultiplier, 1e-6);
		TestEqual(TEXT("stalemate cat drain uses the document power percentage rule"), Step.CatStaminaDrain,
			MakeConfig().PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower, 1e-6);
		TestEqual(TEXT("stalemate keeps distance"), Step.ProposedFishWorldPosition.X, 500.0, 1e-6);
		TestEqual(TEXT("restrained fish keeps publishing its struggle swim intent"),
			Step.IntendedSwimSpeedCentimetersPerSecond,
			MakeConfig().FishStruggleSpeedCentimetersPerSecond, 1e-6);
		TestEqual(TEXT("stalemate has no instant outcome"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::None));
	}
	// ① 瞬断：钓组承载 ≤ min(猫力, 鱼力)，取等从严，结果是断线而非损坏鱼竿。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.RodStrength = 40.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("line at equality snaps"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::LineBroken));
		TestEqual(TEXT("equality reports strength overload"), static_cast<int32>(Step.LineBreakCause),
			static_cast<int32>(ECatFightLineBreakCause::StrengthOverload));
		TestFalse(TEXT("snap is not stalemate"), Step.bStalemate);
	}
	// ② 拖下水：鱼力 ≥ 猫力（竿够强）。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = 50.0;
		Config.RodStrength = 130.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("fish at equality drags cat"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::DraggedIntoWater));
	}
	// ③ 碾压：猫力 ≥ 鱼力 × 2，鱼在当前位置力竭，无消耗；后续由 ExhaustedReel 有限速收近。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = 25.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("overpower outcome"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::Overpowered));
		TestTrue(TEXT("overpower preserves the fish location instead of teleporting to the rod tip"),
			Step.ProposedFishWorldPosition.Equals(Pull.FishWorldPosition, UE_KINDA_SMALL_NUMBER));
		TestEqual(TEXT("overpower preserves paid-out line until exhausted reel takes over"),
			Step.LineLengthCentimeters, Pull.LineLengthCentimeters, 1e-6);
		TestEqual(TEXT("overpower publishes the preserved straight-line distance"),
			Step.StraightLineDistanceCentimeters,
			FVector::Distance(FVector::ZeroVector, Pull.FishWorldPosition), 1e-6);
		TestEqual(TEXT("overpower frame still prices the active full-power input"), Step.CatStaminaDrain,
			Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower, 1e-9);
	}
	// ① 优先于 ②③：钓组承载不足且鱼强 → 断线而不是拖下水。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = 60.0;
		Config.RodStrength = 30.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("line snap wins over drag"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::LineBroken));
	}
	TestEqual(TEXT("same input is deterministic"), Run(MakeConfig(), Pull).AbsoluteRodWear, Run(MakeConfig(), Pull).AbsoluteRodWear);
	return !HasAnyErrors();
}

bool FCatFishingFightSimulatorSlackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();

	FCatFightSimulationState Slack = MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Slack);
	Slack.CatStamina = 90.0;
	const FCatFightStepResult SlackStep = Run(Config, Slack);
	TestEqual(TEXT("slack regens 1.5 per second"), SlackStep.CatStaminaDrain, -1.5, 1e-6);
	TestEqual(TEXT("slack lets fish run out"), SlackStep.ProposedFishWorldPosition.X, 575.0, 1e-6);
	TestEqual(TEXT("open spool follows only the fish actual outward distance"),
		SlackStep.LineLengthCentimeters, 575.0, 1e-6);
	TestEqual(TEXT("open spool creates no artificial surplus line while fish runs outward"),
		SlackStep.SlackLineLengthCentimeters, 0.0, 1e-6);
	TestEqual(TEXT("open spool below maximum creates no tension"),
		SlackStep.NormalizedTension, 0.0, 1e-9);
	TestFalse(TEXT("slack is not stalemate"), SlackStep.bStalemate);
	FCatFightSimulationConfig DisruptedConfig = Config;
	DisruptedConfig.PrimaryOperatorCatStrength = 0.0;
	DisruptedConfig.PrimaryPowerAlpha = 0.0;
	DisruptedConfig.SecondCatStrength = 30.0;
	DisruptedConfig.PrimaryDisruptionStaminaDrainPerSecond = 7.5;
	const FCatFightStepResult DisruptedSlack = Run(DisruptedConfig, Slack);
	TestEqual(TEXT("helper disruption replaces primary slack recovery with the configured penalty"),
		DisruptedSlack.CatStaminaDrain, 7.5, 1e-9);

	FCatFightSimulationState ExistingSlack = Slack;
	ExistingSlack.LineLengthCentimeters = 600.0;
	const FCatFightStepResult ExistingSlackStep = Run(Config, ExistingSlack);
	TestEqual(TEXT("open spool first lets fish consume existing slack"),
		ExistingSlackStep.LineLengthCentimeters, 600.0, 1e-6);
	TestEqual(TEXT("remaining existing slack stays physical"),
		ExistingSlackStep.SlackLineLengthCentimeters, 25.0, 1e-6);
	TestEqual(TEXT("fish remains unrestricted while consuming existing slack"),
		ExistingSlackStep.ProposedFishWorldPosition.X, 575.0, 1e-6);

	FCatFightSimulationConfig StationaryConfig = Config;
	StationaryConfig.FishCalmSpeedCentimetersPerSecond = 0.0;
	FCatFightSimulationState StationarySlack = MakeState(
		ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Slack);
	const FCatFightStepResult StationarySlackStep = Run(StationaryConfig, StationarySlack);
	TestEqual(TEXT("open spool never pays out line while fish is stationary"),
		StationarySlackStep.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("stationary open spool creates no artificial slack"),
		StationarySlackStep.SlackLineLengthCentimeters, 0.0, 1e-6);

	FCatFightSimulationState Full = Slack;
	Full.CatStamina = 100.0;
	TestEqual(TEXT("regen is capped at maximum"), Run(Config, Full).CatStaminaDrain, 0.0, 1e-9);

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::None));
	TestEqual(TEXT("idle zero-power state does not use the removed fish-strength cat drain"),
		Idle.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("idle outward cannot automatically pay out line"), Idle.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("idle outward is constrained at paid out length"), Idle.ProposedFishWorldPosition.X, 500.0, 1e-6);
	TestTrue(TEXT("idle outward taut line drains fish"), Idle.FishStaminaDrain > 0.0);
	TestTrue(TEXT("idle outward publishes tension"), Idle.NormalizedTension > 0.0);

	FCatFightSimulationState CalmButActuallyOutward = MakeState(
		ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::None);
	const FCatFightStepResult CalmTaut = RunDirection(Config, CalmButActuallyOutward,
		FVector::ForwardVector);
	TestTrue(TEXT("actual outward direction creates tension even in calm StateTree state"),
		CalmTaut.NormalizedTension > 0.0);
	TestTrue(TEXT("actual outward direction still drains fish and line resources"),
		CalmTaut.FishStaminaDrain > 0.0 && CalmTaut.AbsoluteRodWear > 0.0);
	TestEqual(TEXT("zero-power cat remains free during the same outward constraint"),
		CalmTaut.CatStaminaDrain, 0.0, 1e-9);

	// 线已被带到上限且鱼顶在线端：不动 / 继续松线都会重新形成对抗（此配置落入僵持）。
	FCatFightSimulationState Taut = MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Slack);
	Taut.CatStamina = 90.0;
	Taut.LineLengthCentimeters = 1000.0;
	Taut.FishWorldPosition = FVector(1000, 0, 0);
	const FCatFightStepResult TautStep = Run(Config, Taut);
	TestTrue(TEXT("taut line forces pull judgment"), TautStep.bStalemate);
	TestEqual(TEXT("taut line never exceeds max"), TautStep.ProposedFishWorldPosition.X, 1000.0, 1e-6);
	TestEqual(TEXT("holding right still regens when paid line is already at maximum"),
		TautStep.CatStaminaDrain, -Config.SlackStaminaRegenPerSecond, 1e-9);
	return !HasAnyErrors();
}

bool FCatFishingFightUnattendedSlackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState Unattended = MakeState(
		ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Slack);
	Unattended.bOperatorPresent = false;
	Unattended.CatStamina = 1.0;

	const FCatFightStepResult PayingOut = Run(Config, Unattended);
	TestTrue(TEXT("无人值守松线步成功"), PayingOut.bSucceeded);
	TestEqual(TEXT("无人值守时鱼按实际外游带出线"),
		PayingOut.LineLengthCentimeters, 575.0, 1e-6);
	TestEqual(TEXT("未到线长极限前不磨损鱼线"), PayingOut.AbsoluteRodWear, 0.0, 1e-9);
	TestEqual(TEXT("无人值守不扣也不恢复任何玩家体力"), PayingOut.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("无人值守不借用旧玩家力量消耗鱼体力"), PayingOut.FishStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("无人值守不会把旧玩家判为力竭或落水"),
		PayingOut.Outcome, ECatFightStepOutcome::None);

	Unattended.LineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	Unattended.FishWorldPosition = FVector(Config.MaximumLineLengthCentimeters, 0.0, 0.0);
	const FCatFightStepResult AtLimit = Run(Config, Unattended);
	TestTrue(TEXT("线放尽后开始累计耐久磨损"), AtLimit.AbsoluteRodWear > 0.0);
	TestFalse(TEXT("无人值守线端受力不进入玩家力量僵持"), AtLimit.bStalemate);
	TestFalse(TEXT("无人值守不触发拖下水或力量碾压"), AtLimit.bStrongConfrontation);
	TestEqual(TEXT("线放尽后仍不结算玩家体力"), AtLimit.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("线放尽后仍不结算鱼体力"), AtLimit.FishStaminaDrain, 0.0, 1e-9);

	Unattended.AbsoluteRodWear = Config.RodDurability - 1.0;
	const FCatFightStepResult Broken = Run(Config, Unattended);
	TestEqual(TEXT("无人接管期间允许耐久耗尽断线"),
		Broken.Outcome, ECatFightStepOutcome::LineBroken);
	TestEqual(TEXT("无人接管断线原因是耐久耗尽"),
		Broken.LineBreakCause, ECatFightLineBreakCause::DurabilityDepleted);
	return !HasAnyErrors();
}

bool FCatFishingFightVerticalProjectionRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FVector RodTipWorldPosition(0.0, 0.0, 200.0);
	FCatFightSimulationState ProjectionState = MakeState(
		ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Slack);
	ProjectionState.CatStamina = 90.0;
	ProjectionState.FishWorldPosition = FVector::ZeroVector;
	ProjectionState.LineLengthCentimeters = 200.0;

	// 鱼在竿尖 XY 投影下时，右键只解锁线杯；鱼的真实水平游动会带出恰好所需的线。
	const FCatFightStepResult SlackX = FCatFishingFightSimulator::Step(
		Config, ProjectionState, RodTipWorldPosition, FVector::ForwardVector);
	const double ExpectedDistance = FVector::Distance(
		RodTipWorldPosition, FVector(Config.FishStruggleSpeedCentimetersPerSecond, 0.0, 0.0));
	TestTrue(TEXT("vertical projection slack step succeeds"), SlackX.bSucceeded);
	TestEqual(TEXT("open spool preserves the fish full horizontal X movement"),
		SlackX.ProposedFishWorldPosition.X, Config.FishStruggleSpeedCentimetersPerSecond, 1e-6);
	TestEqual(TEXT("X movement does not invent lateral drift"), SlackX.ProposedFishWorldPosition.Y, 0.0, 1e-6);
	TestEqual(TEXT("open spool pays out exactly to the moved fish"),
		SlackX.LineLengthCentimeters, ExpectedDistance, 1e-6);
	TestEqual(TEXT("projection recovery publishes the moved straight-line distance"),
		SlackX.StraightLineDistanceCentimeters, ExpectedDistance, 1e-6);
	TestEqual(TEXT("open spool projection recovery creates no tension"), SlackX.TensionCentimeters, 0.0, 1e-9);
	TestEqual(TEXT("undefined projection radial basis publishes neutral alignment"),
		SlackX.FishLineAlignment, 0.0, 1e-9);
	TestEqual(TEXT("undefined projection radial basis publishes no arbitrary load"),
		SlackX.NormalizedLineLoad, 0.0, 1e-9);
	TestFalse(TEXT("projection recovery cannot trigger a strong confrontation"), SlackX.bStrongConfrontation);
	TestEqual(TEXT("high-stamina projection recovery has no terminal outcome"),
		static_cast<int32>(SlackX.Outcome), static_cast<int32>(ECatFightStepOutcome::None));

	// 同样的游速换成 Y 方向必须得到旋转等价结果，不能被世界 Forward 假方向偏置。
	const FCatFightStepResult SlackY = FCatFishingFightSimulator::Step(
		Config, ProjectionState, RodTipWorldPosition, FVector::RightVector);
	TestTrue(TEXT("rotated vertical projection slack step succeeds"), SlackY.bSucceeded);
	TestEqual(TEXT("rotated recovery preserves the fish full horizontal Y movement"),
		SlackY.ProposedFishWorldPosition.Y, Config.FishStruggleSpeedCentimetersPerSecond, 1e-6);
	TestEqual(TEXT("rotated recovery does not drift toward world forward"),
		SlackY.ProposedFishWorldPosition.X, 0.0, 1e-6);
	TestEqual(TEXT("rotated recovery pays out the same scalar line length"),
		SlackY.LineLengthCentimeters, SlackX.LineLengthCentimeters, 1e-6);

	// 线杯锁住时候选游动仍会被线长截回：本修复只消除奇点，不让鱼无视绷紧的鱼线。
	FCatFightSimulationState LockedState = ProjectionState;
	LockedState.CatAction = ECatFightCatAction::None;
	const FCatFightStepResult Locked = FCatFishingFightSimulator::Step(
		Config, LockedState, RodTipWorldPosition, FVector::ForwardVector);
	TestTrue(TEXT("locked projection step succeeds"), Locked.bSucceeded);
	TestEqual(TEXT("locked spool keeps the fish at the only reachable water point"),
		FVector::Dist2D(Locked.ProposedFishWorldPosition, RodTipWorldPosition), 0.0, 1e-6);
	TestEqual(TEXT("locked spool never pays out line"), Locked.LineLengthCentimeters, 200.0, 1e-6);
	TestTrue(TEXT("blocked horizontal swim is reported as tension"), Locked.TensionCentimeters > 0.0);

	// 投影奇点没有真实水平径向，继续左键也不得因为世界轴 fallback 触发碾压。
	FCatFightSimulationState PullState = ProjectionState;
	PullState.CatAction = ECatFightCatAction::Pull;
	FCatFightSimulationConfig WeakFishConfig = Config;
	WeakFishConfig.FishStrength = 25.0;
	const FCatFightStepResult Pull = FCatFishingFightSimulator::Step(
		WeakFishConfig, PullState, RodTipWorldPosition, FVector::ForwardVector);
	TestFalse(TEXT("pulling at the undefined radial basis is not a strong confrontation"),
		Pull.bStrongConfrontation);
	TestEqual(TEXT("projection fallback cannot classify the fish as overpowered"),
		static_cast<int32>(Pull.Outcome), static_cast<int32>(ECatFightStepOutcome::None));

	// 靠近投影点时，4cm 的带载收线最多只能给鱼叠加 4cm 水平牵引，不能经三维球面公式放大成 40cm 吸附。
	FCatFightSimulationConfig TractionConfig = Config;
	TractionConfig.FixedStepSeconds = 0.05;
	TractionConfig.ReelSpeedCentimetersPerSecond = 80.0;
	TractionConfig.FishCalmSpeedCentimetersPerSecond = 0.0;
	FCatFightSimulationState NearProjection = MakeState(
		ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull);
	NearProjection.FishWorldPosition = FVector(40.0, 0.0, 0.0);
	NearProjection.LineLengthCentimeters = FVector::Distance(
		RodTipWorldPosition, NearProjection.FishWorldPosition);
	const FCatFightStepResult BoundedTraction = FCatFishingFightSimulator::Step(
		TractionConfig, NearProjection, RodTipWorldPosition, FVector::RightVector);
	const double MaximumPullDisplacement = TractionConfig.ReelSpeedCentimetersPerSecond
		* TractionConfig.FixedStepSeconds;
	TestTrue(TEXT("near-projection bounded traction succeeds"), BoundedTraction.bSucceeded);
	TestEqual(TEXT("near-projection pull correction is limited to one configured reel step"),
		FVector::Dist2D(NearProjection.FishWorldPosition, BoundedTraction.ProposedFishWorldPosition),
		MaximumPullDisplacement, 1e-6);
	TestEqual(TEXT("bounded traction never invents lateral drift"),
		BoundedTraction.ProposedFishWorldPosition.Y, 0.0, 1e-6);
	TestEqual(TEXT("paid line reconciles to the fish actually reached distance"),
		BoundedTraction.LineLengthCentimeters,
		FVector::Distance(RodTipWorldPosition, BoundedTraction.ProposedFishWorldPosition), 1e-6);
	TestTrue(TEXT("near-projection reel request may stall instead of teleporting the fish"),
		BoundedTraction.LineLengthCentimeters
			> NearProjection.LineLengthCentimeters - MaximumPullDisplacement + 1e-6);

	// 恢复鱼速后，最终位移必须同时保留鱼的横向游动和鱼竿的有限向内牵引，而不是只剩径向漂移。
	FCatFightSimulationState SwimmingNearProjection = NearProjection;
	SwimmingNearProjection.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	const FCatFightStepResult ComposedMotion = FCatFishingFightSimulator::Step(
		TractionConfig, SwimmingNearProjection, RodTipWorldPosition, FVector::RightVector);
	TestTrue(TEXT("fish-led composed motion succeeds"), ComposedMotion.bSucceeded);
	TestTrue(TEXT("fish keeps its own lateral movement while the rod pulls inward"),
		ComposedMotion.ProposedFishWorldPosition.Y > 0.0);
	TestEqual(TEXT("rod traction reduces only one horizontal reel step after the fish move"),
		FVector::Dist2D(ComposedMotion.ProposedFishWorldPosition, RodTipWorldPosition),
		40.0 - MaximumPullDisplacement, 1e-6);
	return !HasAnyErrors();
}

bool FCatFishingFightSimulatorPriorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState State = MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull);

	// 三者同帧归零 → 鱼体力优先。
	State.FishStamina = 1.0;
	State.CatStamina = 1.0;
	State.AbsoluteRodWear = Config.RodDurability - 1.0;
	TestEqual(TEXT("fish exhausted first"), static_cast<int32>(Run(Config, State).Outcome), static_cast<int32>(ECatFightStepOutcome::FishExhausted));

	State.FishStamina = 50.0;
	TestEqual(TEXT("cat exhausted second"), static_cast<int32>(Run(Config, State).Outcome), static_cast<int32>(ECatFightStepOutcome::CatStaminaExhausted));

	State.CatStamina = 100.0;
	const FCatFightStepResult LineWear = Run(Config, State);
	TestEqual(TEXT("line wear third"), static_cast<int32>(LineWear.Outcome), static_cast<int32>(ECatFightStepOutcome::LineBroken));
	TestEqual(TEXT("line wear reports durability depletion"), static_cast<int32>(LineWear.LineBreakCause),
		static_cast<int32>(ECatFightLineBreakCause::DurabilityDepleted));
	return !HasAnyErrors();
}

bool FCatFishingFightExhaustionThresholdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.FixedStepSeconds = 0.05;
	Config.FishExhaustionThreshold = 0.5;
	FCatFightSimulationState State = MakeState(
		ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull);

	// 满负载时本步公式消耗 0.4（含挣扎倍率 2.0）。0.91 -> 0.51 仍可继续，不能过早翻肚。
	State.FishStamina = 0.91;
	const FCatFightStepResult Above = RunDirection(Config, State, FVector::ForwardVector);
	TestEqual(TEXT("stamina above display threshold keeps fighting"),
		static_cast<int32>(Above.Outcome), static_cast<int32>(ECatFightStepOutcome::None));
	TestEqual(TEXT("struggle-multiplied drain is preserved above threshold"), Above.FishStaminaDrain, 0.4, 1e-6);

	// 0.90 -> 0.50 正好进入阈值：本步把尾数全部扣到 0，并立刻报告 FishExhausted。
	State.FishStamina = 0.90;
	const FCatFightStepResult Exhausted = RunDirection(Config, State, FVector::ForwardVector);
	TestEqual(TEXT("sub-display stamina enters exhausted outcome"),
		static_cast<int32>(Exhausted.Outcome), static_cast<int32>(ECatFightStepOutcome::FishExhausted));
	TestEqual(TEXT("exhausted drain zeroes authoritative stamina"), Exhausted.FishStaminaDrain, 0.90, 1e-6);

	Config.FishExhaustionThreshold = 1.01;
	TestFalse(TEXT("threshold above one stamina unit is rejected"), Config.IsValid());
	return !HasAnyErrors();
}

bool FCatFishingFightMotionSolverTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishMotionSolveInput Input;
	Input.RodTipWorldPosition = FVector::ZeroVector;
	Input.ProposedFishWorldPosition = FVector(2000, 2000, 0);
	Input.WaterBounds = FBox(FVector(-500, -500, -50), FVector(500, 500, 50));
	Input.MaximumLineLengthCentimeters = 400.0;
	const FCatFishMotionSolveResult Result = FCatFishFightMotionSolver::Solve(Input);
	TestTrue(TEXT("solver succeeds for intersecting bounds and reach"), Result.bSucceeded);
	TestTrue(TEXT("projected fish remains in water"), Input.WaterBounds.IsInsideOrOn(Result.FishWorldPosition));
	TestTrue(TEXT("projected fish remains in line reach"), Result.FishWorldPosition.Length() <= 400.01);

	// 回归：完美提竿会把初始线长从原钩点距离缩短。必须先用缩短后的线长投影 Actor，
	// 否则 Runner 第一固定步的岸线解算会面对“当前鱼在线长球外”的矛盾状态并拒绝运行。
	Input.ProposedFishWorldPosition = FVector(500.0, 0.0, 0.0);
	Input.WaterBounds = FBox(FVector(-1000.0), FVector(1000.0));
	Input.MaximumLineLengthCentimeters = 425.0;
	const FCatFishMotionSolveResult PerfectHookProjection = FCatFishFightMotionSolver::Solve(Input);
	TestTrue(TEXT("perfect hook initial contraction solves"), PerfectHookProjection.bSucceeded);
	TestTrue(TEXT("perfect hook moves actor inside shortened paid line"),
		FVector::Distance(Input.RodTipWorldPosition, PerfectHookProjection.FishWorldPosition) <= 425.01);
	FCatFishShoreContactInput FirstStep;
	FirstStep.CurrentFishWorldPosition = PerfectHookProjection.FishWorldPosition;
	FirstStep.CandidateFishWorldPosition = PerfectHookProjection.FishWorldPosition;
	FirstStep.ResolvedWaterWorldPosition = PerfectHookProjection.FishWorldPosition;
	FirstStep.WaterwardDirection = FVector::ForwardVector;
	FirstStep.RodTipWorldPosition = Input.RodTipWorldPosition;
	FirstStep.PreviousLineLengthCentimeters = 425.0;
	FirstStep.ProposedLineLengthCentimeters = 425.0;
	TestTrue(TEXT("first fight step accepts perfect-hook contracted initial geometry"),
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(FirstStep).bSucceeded);
	return !HasAnyErrors();
}

bool FCatFishingFightAngleProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState Pull = MakeState(
		ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Pull);

	// 60°：cos=0.5，低于 0.55 强对抗阈值；线负载/牵引效率按夹角，带载左键的双方体力用挣扎档。
	const FVector SixtyDegrees(0.5, FMath::Sqrt(0.75), 0.0);
	const FCatFightStepResult Oblique = RunDirection(Config, Pull, SixtyDegrees);
	TestTrue(TEXT("oblique step succeeds"), Oblique.bSucceeded);
	TestEqual(TEXT("sixty degree alignment is one half"), Oblique.FishLineAlignment, 0.5, 1e-6);
	TestEqual(TEXT("linear angle curve keeps half load"), Oblique.NormalizedLineLoad, 0.5, 1e-6);
	TestFalse(TEXT("half load is below strong confrontation threshold"), Oblique.bStrongConfrontation);
	TestFalse(TEXT("partial load is not a locked stalemate"), Oblique.bStalemate);
	TestEqual(TEXT("oblique loaded pull prices cat stamina by power"), Oblique.CatStaminaDrain,
		Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower, 1e-6);
	TestEqual(TEXT("oblique loaded pull still drains fish stamina in the struggle tier"),
		Oblique.FishStaminaDrain, 50.0 * 0.08 * Config.StruggleDrainMultiplier, 1e-6);
	TestEqual(TEXT("partial load leaves half reel speed"), Oblique.ProposedFishWorldPosition.Size(), 450.0, 1e-6);
	TestTrue(TEXT("oblique direction produces lateral movement"), Oblique.ProposedFishWorldPosition.Y > 0.0);

	// 90°：没有鱼线方向分量，不产生正面对抗；鱼仍绕竿横向游，玩家获得完整收线窗口。
	const FCatFightStepResult Lateral = RunDirection(Config, Pull, FVector::RightVector);
	TestEqual(TEXT("lateral alignment is zero"), Lateral.FishLineAlignment, 0.0, 1e-6);
	TestEqual(TEXT("lateral load is zero"), Lateral.NormalizedLineLoad, 0.0, 1e-6);
	TestEqual(TEXT("lateral loaded pull still drains cat stamina"), Lateral.CatStaminaDrain,
		Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower, 1e-6);
	TestEqual(TEXT("lateral loaded pull still drains fish stamina"), Lateral.FishStaminaDrain,
		50.0 * 0.08 * Config.StruggleDrainMultiplier, 1e-6);
	TestEqual(TEXT("lateral window uses full reel speed"), Lateral.ProposedFishWorldPosition.Size(), 400.0, 1e-6);
	TestTrue(TEXT("lateral direction still circles around rod"), Lateral.ProposedFishWorldPosition.Y > 0.0);

	// 约 37°：cos=0.8，超过强对抗阈值；现有三方力量落入僵持，磨损按夹角、双方体力按带载挣扎档。
	const FVector StrongOblique(0.8, 0.6, 0.0);
	const FCatFightStepResult Strong = RunDirection(Config, Pull, StrongOblique);
	TestTrue(TEXT("high projection enters strong confrontation"), Strong.bStrongConfrontation);
	TestTrue(TEXT("high projection preserves stalemate judgment"), Strong.bStalemate);
	TestEqual(TEXT("strong oblique uses loaded struggle cat drain"), Strong.CatStaminaDrain,
		Config.PowerTuning.PrimaryStaminaDrainPerSecondAtFullPower, 1e-6);
	TestEqual(TEXT("strong oblique confrontation keeps the unopposed twenty percent reel"),
		Strong.ProposedFishWorldPosition.Size(), 480.0, 1e-6);

	// 运行时 0.05 秒固定步：持续 0.2 秒才确认，前 3 步不会被随机方向过阈值一帧触发重大结局。
	FCatFightSimulationConfig ConfirmConfig = Config;
	ConfirmConfig.FixedStepSeconds = 0.05;
	FCatFightSimulationState ConfirmState = Pull;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FCatFightStepResult ConfirmStep = RunDirection(ConfirmConfig, ConfirmState, FVector::ForwardVector);
		TestEqual(FString::Printf(TEXT("confirmation state on fixed step %d"), Index + 1),
			ConfirmStep.bStrongConfrontation, Index == 3);
		ConfirmState.StrongConfrontationBuildUpSeconds = ConfirmStep.StrongConfrontationBuildUpSeconds;
	}
	return !HasAnyErrors();
}

bool FCatFishSteeringDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.RetargetDurationRangeSeconds = FVector2D(0.2, 0.4);
	Config.MaximumTurnRateDegreesPerSecond = 90.0;
	Config.StruggleOutwardBias = 0.55;
	Config.CalmInwardBias = 0.65;
	Config.LateralMovementBias = 0.8;
	Config.FeintProbability = 0.2;
	TestTrue(TEXT("steering config is ready"), Config.IsValid());

	FRandomStream RandomA(1337);
	FRandomStream RandomB(1337);
	FCatFishSteeringState StateA;
	FCatFishSteeringState StateB;
	FVector Previous = FVector::ForwardVector;
	bool bEverTurned = false;
	for (int32 Index = 0; Index < 30; ++Index)
	{
		FVector DirectionA;
		FVector DirectionB;
		TestTrue(TEXT("first deterministic steering step succeeds"), FCatFishSteeringModel::Step(Config,
			FVector::ForwardVector, ECatFishMotionIntent::StrugglingOutward, 1.0, 0.05,
			RandomA, StateA, DirectionA));
		TestTrue(TEXT("second deterministic steering step succeeds"), FCatFishSteeringModel::Step(Config,
			FVector::ForwardVector, ECatFishMotionIntent::StrugglingOutward, 1.0, 0.05,
			RandomB, StateB, DirectionB));
		TestTrue(TEXT("same seed produces same direction"), DirectionA.Equals(DirectionB, 1e-9));
		TestEqual(TEXT("steering direction remains normalized"), DirectionA.Size(), 1.0, 1e-6);
		const double TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Previous, DirectionA), -1.0, 1.0)));
		TestTrue(TEXT("turn never exceeds configured angular speed"), TurnDegrees <= 4.5001);
		bEverTurned |= !DirectionA.Equals(FVector::ForwardVector, 1e-3);
		Previous = DirectionA;
	}
	TestTrue(TEXT("personality steering eventually creates a non-radial direction"), bEverTurned);

	FCatFishSteeringConfig Invalid = Config;
	Invalid.RetargetDurationRangeSeconds = FVector2D(1.0, 0.5);
	TestFalse(TEXT("reversed retarget range is rejected"), Invalid.IsValid());
	return !HasAnyErrors();
}

bool FCatFishSteeringShoreRedirectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	FCatFishSteeringState State;
	State.bInitialized = true;
	State.CurrentDirection = -FVector::ForwardVector; // Forward 表示水内，当前正朝岸外撞。
	State.TargetDirection = -FVector::ForwardVector;
	State.RetargetSecondsRemaining = 0.05;
	State.LastMotionIntent = ECatFishMotionIntent::CalmOrInward;
	FRandomStream Random(9);

	TestTrue(TEXT("shore collision redirects live fish"),
		FCatFishSteeringModel::RedirectFromWaterBoundary(Config, FVector::ForwardVector, Random, State));
	TestTrue(TEXT("current direction points back into water"), State.CurrentDirection.X > 0.0);
	TestTrue(TEXT("target direction points back into water"), State.TargetDirection.X > 0.0);
	TestTrue(TEXT("shore redirect adds visible lateral escape"), FMath::Abs(State.TargetDirection.Y) > 0.25);
	TestTrue(TEXT("redirect survives at least one retarget window"),
		State.RetargetSecondsRemaining >= Config.RetargetDurationRangeSeconds.Y);

	FVector Direction;
	TestTrue(TEXT("redirected steering continues stepping"), FCatFishSteeringModel::Step(Config,
		FVector::ForwardVector, ECatFishMotionIntent::CalmOrInward, 0.5, 0.05, Random, State, Direction));
	TestTrue(TEXT("next fixed step remains waterward"), Direction.X > 0.0);
	TestFalse(TEXT("invalid boundary normal is rejected"),
		FCatFishSteeringModel::RedirectFromWaterBoundary(Config, FVector::ZeroVector, Random, State));
	return !HasAnyErrors();
}

bool FCatFishShoreContactContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishShoreContactInput Contact;
	Contact.CurrentFishWorldPosition = FVector(10.0, 20.0, 0.0);
	Contact.CandidateFishWorldPosition = FVector(-2.0, 24.0, 0.0);
	Contact.ResolvedWaterWorldPosition = FVector(100.0, 24.0, 0.0); // 模拟较大的 MinimumWaterInset 回推。
	Contact.WaterwardDirection = FVector::ForwardVector;
	Contact.RodTipWorldPosition = FVector(60.0, 20.0, 0.0);
	Contact.PreviousLineLengthCentimeters = 50.1;
	Contact.ProposedLineLengthCentimeters = 50.1;
	const FCatFishShoreContactResult Hit = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Contact);
	TestTrue(TEXT("shore contact solve succeeds"), Hit.bSucceeded);
	TestTrue(TEXT("large water correction is classified as shore contact"), Hit.bShoreContact);
	TestEqual(TEXT("contact discards large normal inset teleport"), Hit.FishWorldPosition.X, 10.0, 1e-9);
	TestTrue(TEXT("contact preserves as much along-shore movement as paid line permits"),
		Hit.FishWorldPosition.Y > Contact.CurrentFishWorldPosition.Y);
	TestEqual(TEXT("shore contact never auto-pays out line"),
		Hit.LineLengthCentimeters, Contact.ProposedLineLengthCentimeters, 1e-6);
	TestEqual(TEXT("taut shore move is clamped to paid line"),
		FVector::Distance(Contact.RodTipWorldPosition, Hit.FishWorldPosition),
		Contact.ProposedLineLengthCentimeters, 1e-6);

	// 松开线杯时 Simulator 会按候选点给出临时线长，但撞岸后的最终 L_paid 必须只按实际落点增长。
	Contact.PreviousLineLengthCentimeters = 50.1;
	Contact.ProposedLineLengthCentimeters = 80.0;
	Contact.bSlacking = true;
	const FCatFishShoreContactResult SlackHit =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Contact);
	const double ActualSlackDistance = FVector::Distance(
		Contact.RodTipWorldPosition, SlackHit.FishWorldPosition);
	TestTrue(TEXT("open spool shore contact still solves"), SlackHit.bSucceeded);
	TestEqual(TEXT("open spool pays only actual resolved movement"),
		SlackHit.LineLengthCentimeters,
		FMath::Max(Contact.PreviousLineLengthCentimeters, ActualSlackDistance), 1e-6);
	TestTrue(TEXT("blocked candidate does not create phantom paid line"),
		SlackHit.LineLengthCentimeters < Contact.ProposedLineLengthCentimeters - 1.0);

	Contact.PreviousLineLengthCentimeters = 50.1;
	Contact.ProposedLineLengthCentimeters = 49.0;
	Contact.bReeling = true;
	Contact.bSlacking = false;
	const FCatFishShoreContactResult BlockedReel =
		FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Contact);
	TestTrue(TEXT("shore-blocked reel still solves"), BlockedReel.bSucceeded);
	TestTrue(TEXT("blocked reel may roll back only the requested reduction"),
		BlockedReel.LineLengthCentimeters > Contact.ProposedLineLengthCentimeters
		&& BlockedReel.LineLengthCentimeters <= Contact.PreviousLineLengthCentimeters + 1e-6);

	Contact.CandidateFishWorldPosition = FVector(20.0, 24.0, 0.0);
	Contact.ResolvedWaterWorldPosition = FVector(20.25, 24.0, 0.0);
	Contact.PreviousLineLengthCentimeters = 80.0;
	Contact.ProposedLineLengthCentimeters = 80.0;
	Contact.bReeling = false;
	const FCatFishShoreContactResult Free = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(Contact);
	TestTrue(TEXT("small water correction remains a normal move"), Free.bSucceeded);
	TestFalse(TEXT("sub-tolerance correction is not shore contact"), Free.bShoreContact);
	TestTrue(TEXT("normal move adopts resolved water point"),
		Free.FishWorldPosition.Equals(Contact.ResolvedWaterWorldPosition, 1e-9));
	TestEqual(TEXT("normal move preserves proposed line length"), Free.LineLengthCentimeters, 80.0, 1e-6);
	return !HasAnyErrors();
}

bool FCatFishStaminaDrivenInwardProbabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishSteeringConfig Config;
	Config.FullStaminaInwardProbability = 0.1;
	Config.ExhaustedInwardProbability = 0.8;
	Config.InwardProbabilityExponent = 1.0;
	Config.InwardConeHalfAngleDegrees = 60.0;
	TestEqual(TEXT("full stamina uses configured low inward probability"),
		FCatFishSteeringModel::ComputeInwardProbability(Config, 1.0), 0.1, 1e-9);
	TestEqual(TEXT("zero stamina uses configured high inward probability"),
		FCatFishSteeringModel::ComputeInwardProbability(Config, 0.0), 0.8, 1e-9);
	TestEqual(TEXT("linear curve interpolates at half stamina"),
		FCatFishSteeringModel::ComputeInwardProbability(Config, 0.5), 0.45, 1e-9);

	const auto CountInwardSelections = [&Config](const double StaminaRatio)
	{
		FRandomStream Random(20260825);
		int32 Count = 0;
		const double InwardThreshold = FMath::Cos(FMath::DegreesToRadians(Config.InwardConeHalfAngleDegrees));
		for (int32 Index = 0; Index < 400; ++Index)
		{
			FCatFishSteeringState State;
			if (FCatFishSteeringModel::Initialize(Config, FVector::ForwardVector,
				ECatFishMotionIntent::CalmOrInward, StaminaRatio, Random, State)
				&& FVector::DotProduct(State.TargetDirection, -FVector::ForwardVector)
					>= InwardThreshold - UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				++Count;
			}
		}
		return Count;
	};
	const int32 FullStaminaInwardCount = CountInwardSelections(1.0);
	const int32 ExhaustedInwardCount = CountInwardSelections(0.0);
	TestTrue(TEXT("full stamina fish mostly selects directions outside inward cone"),
		FullStaminaInwardCount < 100);
	TestTrue(TEXT("exhausted fish selects inward cone much more often"),
		ExhaustedInwardCount > 250 && ExhaustedInwardCount > FullStaminaInwardCount * 4);
	return !HasAnyErrors();
}

#endif
