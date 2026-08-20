#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorInwardTest,
	"Catfishing.Unit.Fishing.Simulation.InwardPullDrainsCatByFishStrengthAndIdleIsFree",
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
		return FCatFishingFightSimulator::Step(Config, State, FVector::ZeroVector, FVector::ForwardVector);
	}
}

using namespace CatFishingSimulationTest;

bool FCatFishingFightSimulatorInwardTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = MakeConfig();

	const FCatFightStepResult Pull = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Pull));
	TestTrue(TEXT("inward pull succeeds"), Pull.bSucceeded);
	TestEqual(TEXT("inward pull drains cat by fish strength x 0.15"), Pull.CatStaminaDrain, 40.0 * 0.15, 1e-6);
	TestEqual(TEXT("inward pull drains fish by cat strength x 0.08"), Pull.FishStaminaDrain, 50.0 * 0.08, 1e-6);
	TestEqual(TEXT("inward pull reels distance by reel speed"), Pull.ProposedFishWorldPosition.X, 400.0, 1e-6);
	TestEqual(TEXT("inward pull reels line to fish"), Pull.LineLengthCentimeters, 400.0, 1e-6);
	TestEqual(TEXT("inward pull has no outcome"), static_cast<int32>(Pull.Outcome), static_cast<int32>(ECatFightStepOutcome::None));

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::None));
	TestEqual(TEXT("inward idle is free"), Idle.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("inward idle lets fish approach slowly"), Idle.ProposedFishWorldPosition.X, 475.0, 1e-6);
	TestEqual(TEXT("inward idle keeps line slack"), Idle.LineLengthCentimeters, 500.0, 1e-6);

	const FCatFightStepResult Slack = Run(Config, MakeState(ECatFishMotionIntent::CalmOrInward, ECatFightCatAction::Slack));
	TestEqual(TEXT("inward slack behaves like idle"), Slack.ProposedFishWorldPosition.X, Idle.ProposedFishWorldPosition.X, 1e-9);
	TestEqual(TEXT("inward slack does not regen"), Slack.CatStaminaDrain, 0.0, 1e-9);
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
	// ① 瞬断：竿强度 ≤ min(猫力, 鱼力)，取等从严。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.RodStrength = 40.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("rod at equality snaps"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::RodBroken));
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
	// ① 优先于 ②③：竿弱且鱼强 → 断竿而不是拖下水。
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = 60.0;
		Config.RodStrength = 30.0;
		const FCatFightStepResult Step = Run(Config, Pull);
		TestEqual(TEXT("snap wins over drag"), static_cast<int32>(Step.Outcome), static_cast<int32>(ECatFightStepOutcome::RodBroken));
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
	TestEqual(TEXT("slack pays out line"), SlackStep.LineLengthCentimeters, 575.0, 1e-6);
	TestFalse(TEXT("slack is not stalemate"), SlackStep.bStalemate);

	FCatFightSimulationState Full = Slack;
	Full.CatStamina = 100.0;
	TestEqual(TEXT("regen is capped at maximum"), Run(Config, Full).CatStaminaDrain, 0.0, 1e-9);

	const FCatFightStepResult Idle = Run(Config, MakeState(ECatFishMotionIntent::StrugglingOutward, ECatFightCatAction::None));
	TestEqual(TEXT("idle outward does not regen"), Idle.CatStaminaDrain, 0.0, 1e-9);
	TestEqual(TEXT("idle outward lets fish run out"), Idle.ProposedFishWorldPosition.X, 575.0, 1e-6);

	// 线放尽且鱼顶在线端：不动 / 放线都按「拖」判定（此配置落入僵持）。
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
	TestEqual(TEXT("rod wear third"), static_cast<int32>(Run(Config, State).Outcome), static_cast<int32>(ECatFightStepOutcome::RodBroken));
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
	return !HasAnyErrors();
}

#endif
