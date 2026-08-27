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
	"Catfishing.Unit.Fishing.Simulation.OutwardSlackRegensCatAndTautLineForcesJudgment",
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

namespace CatFishingSimulationTest
{
	// 规格快照：猫 50 / 竿强 60 / 鱼 40 → 向外游+拖 落入 ④ 僵持。
	static FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 1.0; // 用 1 秒步长让每秒速率直接可读
		Config.CatStrength = 50.0;
		Config.FishStrength = 40.0;
		Config.RodStrength = 60.0;
		Config.CatStaminaMaximum = 100.0;
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

bool FCatFishingFightSimulatorInwardTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();

	const FCatFightStepResult Pull = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull));
	TestTrue(TEXT("inward pull succeeds"), Pull.bSucceeded);
	TestEqual(TEXT("inward fish direction has no opposing cat drain"), Pull.CatStaminaDrain, 0.0, 1e-6);
	TestEqual(TEXT("inward fish direction has no opposing fish drain"), Pull.FishStaminaDrain, 0.0, 1e-6);
	TestEqual(TEXT("inward direction publishes negative alignment"), Pull.FishLineAlignment, -1.0, 1e-6);
	TestEqual(TEXT("inward direction publishes zero line load"), Pull.NormalizedLineLoad, 0.0, 1e-6);
	TestEqual(TEXT("inward pull reels distance by reel speed"), Pull.ProposedFishWorldPosition.X, 400.0, 1e-6);
	TestEqual(TEXT("inward pull reels line to fish"), Pull.LineLengthCentimeters, 400.0, 1e-6);
	TestEqual(TEXT("inward pull has no outcome"), static_cast<int32>(Pull.Outcome), static_cast<int32>(ECatFightStepOutcome::None));

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::None));
	TestEqual(TEXT("inward idle is free"), Idle.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("inward idle lets fish approach slowly"), Idle.ProposedFishWorldPosition.X, 475.0, 1e-6);
	TestEqual(TEXT("inward idle keeps line slack"), Idle.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("inward idle accumulates twenty five centimeters of slack"),
		Idle.SlackLineLengthCentimeters, 25.0, 1e-6);

	const FCatFightStepResult Slack = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Slack));
	TestEqual(TEXT("inward slack behaves like idle"), Slack.ProposedFishWorldPosition.X, Idle.ProposedFishWorldPosition.X, 1e-9);
	TestEqual(TEXT("inward slack does not regen"), Slack.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("right input does not actively pay out while fish moves inward"),
		Slack.LineLengthCentimeters, 500.0, 1e-6);
	TestEqual(TEXT("inward fish still creates slack while spool is open"),
		Slack.SlackLineLengthCentimeters, 25.0, 1e-6);
	FCatFightSimulationConfig StationaryConfig = Config;
	StationaryConfig.FishCalmSpeedCentimetersPerSecond = 0.0;
	const FCatFightStepResult StationarySlack = Run(StationaryConfig,
		MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Slack));
	TestEqual(TEXT("holding right while fish is stationary never manufactures extra line"),
		StationarySlack.LineLengthCentimeters, 500.0, 1e-6);
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
		TestEqual(TEXT("stalemate fish drain = cat x 0.08"), Step.FishStaminaDrain, 50.0 * 0.08, 1e-6);
		TestEqual(TEXT("stalemate cat drain = fish x 0.12"), Step.CatStaminaDrain, 40.0 * 0.12, 1e-6);
		TestEqual(TEXT("stalemate keeps distance"), Step.ProposedFishWorldPosition.X, 500.0, 1e-6);
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
	// ③ 碾压：猫力 ≥ 鱼力 × 2，D 归零，无消耗。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = 25.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("overpower outcome"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::Overpowered));
		TestEqual(TEXT("overpower zeroes distance"), Step.ProposedFishWorldPosition.X, 0.0, 1e-6);
		TestEqual(TEXT("overpower costs nothing"), Step.CatStaminaDrain, 0.0, 1e-9);
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

	FCatFightSimulationState ExistingSlack = Slack;
	ExistingSlack.LineLengthCentimeters = 600.0;
	const FCatFightStepResult ExistingSlackStep = Run(Config, ExistingSlack);
	TestEqual(TEXT("open spool first lets fish consume existing slack"),
		ExistingSlackStep.LineLengthCentimeters, 600.0, 1e-6);
	TestEqual(TEXT("remaining existing slack stays physical"),
		ExistingSlackStep.SlackLineLengthCentimeters, 25.0, 1e-6);
	TestEqual(TEXT("fish remains unrestricted while consuming existing slack"),
		ExistingSlackStep.ProposedFishWorldPosition.X, 575.0, 1e-6);

	FCatFightSimulationState Full = Slack;
	Full.CatStamina = 100.0;
	TestEqual(TEXT("regen is capped at maximum"), Run(Config, Full).CatStaminaDrain, 0.0, 1e-9);

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::None));
	TestTrue(TEXT("idle outward taut line drains instead of regenerating"), Idle.CatStaminaDrain > 0.0);
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
	TestTrue(TEXT("actual outward direction drains resources regardless of StateTree state name"),
		CalmTaut.FishStaminaDrain > 0.0 && CalmTaut.CatStaminaDrain > 0.0
		&& CalmTaut.AbsoluteRodWear > 0.0);

	// 线已被带到上限且鱼顶在线端：不动 / 继续松线都会重新形成对抗（此配置落入僵持）。
	FCatFightSimulationState Taut = MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::Slack);
	Taut.LineLengthCentimeters = 1000.0;
	Taut.FishWorldPosition = FVector(1000, 0, 0);
	const FCatFightStepResult TautStep = Run(Config, Taut);
	TestTrue(TEXT("taut line forces pull judgment"), TautStep.bStalemate);
	TestEqual(TEXT("taut line never exceeds max"), TautStep.ProposedFishWorldPosition.X, 1000.0, 1e-6);
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

	// 满负载时本步公式消耗 0.2。0.71 -> 0.51 仍可继续，不能过早翻肚。
	State.FishStamina = 0.71;
	const FCatFightStepResult Above = RunDirection(Config, State, FVector::ForwardVector);
	TestEqual(TEXT("stamina above display threshold keeps fighting"),
		static_cast<int32>(Above.Outcome), static_cast<int32>(ECatFightStepOutcome::None));
	TestEqual(TEXT("normal projected drain is preserved above threshold"), Above.FishStaminaDrain, 0.2, 1e-6);

	// 0.70 -> 0.50 正好进入阈值：本步把尾数全部扣到 0，并立刻报告 FishExhausted。
	State.FishStamina = 0.70;
	const FCatFightStepResult Exhausted = RunDirection(Config, State, FVector::ForwardVector);
	TestEqual(TEXT("sub-display stamina enters exhausted outcome"),
		static_cast<int32>(Exhausted.Outcome), static_cast<int32>(ECatFightStepOutcome::FishExhausted));
	TestEqual(TEXT("exhausted drain zeroes authoritative stamina"), Exhausted.FishStaminaDrain, 0.70, 1e-6);

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

	// 60°：cos=0.5，低于 0.55 强对抗阈值；体力/磨损按一半力量，仍有一半收线速度。
	const FVector SixtyDegrees(0.5, FMath::Sqrt(0.75), 0.0);
	const FCatFightStepResult Oblique = RunDirection(Config, Pull, SixtyDegrees);
	TestTrue(TEXT("oblique step succeeds"), Oblique.bSucceeded);
	TestEqual(TEXT("sixty degree alignment is one half"), Oblique.FishLineAlignment, 0.5, 1e-6);
	TestEqual(TEXT("linear angle curve keeps half load"), Oblique.NormalizedLineLoad, 0.5, 1e-6);
	TestFalse(TEXT("half load is below strong confrontation threshold"), Oblique.bStrongConfrontation);
	TestFalse(TEXT("partial load is not a locked stalemate"), Oblique.bStalemate);
	TestEqual(TEXT("partial load halves cat drain"), Oblique.CatStaminaDrain, 40.0 * 0.12 * 0.5, 1e-6);
	TestEqual(TEXT("partial load halves fish drain"), Oblique.FishStaminaDrain, 50.0 * 0.08 * 0.5, 1e-6);
	TestEqual(TEXT("partial load leaves half reel speed"), Oblique.ProposedFishWorldPosition.Size(), 450.0, 1e-6);
	TestTrue(TEXT("oblique direction produces lateral movement"), Oblique.ProposedFishWorldPosition.Y > 0.0);

	// 90°：没有鱼线方向分量，不产生正面对抗；鱼仍绕竿横向游，玩家获得完整收线窗口。
	const FCatFightStepResult Lateral = RunDirection(Config, Pull, FVector::RightVector);
	TestEqual(TEXT("lateral alignment is zero"), Lateral.FishLineAlignment, 0.0, 1e-6);
	TestEqual(TEXT("lateral load is zero"), Lateral.NormalizedLineLoad, 0.0, 1e-6);
	TestEqual(TEXT("lateral movement drains no cat stamina"), Lateral.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("lateral movement drains no fish stamina"), Lateral.FishStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("lateral window uses full reel speed"), Lateral.ProposedFishWorldPosition.Size(), 400.0, 1e-6);
	TestTrue(TEXT("lateral direction still circles around rod"), Lateral.ProposedFishWorldPosition.Y > 0.0);

	// 约 37°：cos=0.8，超过强对抗阈值；现有三方力量落入僵持，但消耗乘 0.8。
	const FVector StrongOblique(0.8, 0.6, 0.0);
	const FCatFightStepResult Strong = RunDirection(Config, Pull, StrongOblique);
	TestTrue(TEXT("high projection enters strong confrontation"), Strong.bStrongConfrontation);
	TestTrue(TEXT("high projection preserves stalemate judgment"), Strong.bStalemate);
	TestEqual(TEXT("strong oblique scales stalemate cat drain"), Strong.CatStaminaDrain, 40.0 * 0.12 * 0.8, 1e-6);
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
