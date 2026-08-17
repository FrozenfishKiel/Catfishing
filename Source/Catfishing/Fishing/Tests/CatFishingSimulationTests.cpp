#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightSimulatorDrainTest,
	"Catfishing.Unit.Fishing.Simulation.ReelingAlwaysDrainsBothAndStruggleUsesMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFightMotionSolverTest,
	"Catfishing.Unit.Fishing.Simulation.MotionSolverProjectsDeterministicallyIntoWaterAndLineReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingSimulationTest
{
	static FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.1;
		Config.BaseDrainPerSecond = 2.0;
		Config.BaseDrainMultiplier = 1.0;
		Config.StruggleDrainMultiplier = 3.0;
		Config.ReelSpeedCentimetersPerSecond = 100.0;
		Config.FishCalmSpeedCentimetersPerSecond = 25.0;
		Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
		Config.MaximumLineLengthCentimeters = 1000.0;
		Config.RodWearPerTensionSecond = 0.5;
		return Config;
	}
}

bool FCatFishingFightSimulatorDrainTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFightSimulationConfig Config = CatFishingSimulationTest::MakeConfig();
	FCatFightSimulationState Calm;
	Calm.CatStamina = 10.0;
	Calm.FishStamina = 10.0;
	Calm.LineLengthCentimeters = 500.0;
	Calm.FishWorldPosition = FVector(500, 0, 0);
	Calm.bReeling = true;
	Calm.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	const FCatFightStepResult CalmStep = FCatFishingFightSimulator::Step(Config, Calm, FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("calm reeling drains cat"), CalmStep.CatStaminaDrain > 0.0);
	TestTrue(TEXT("calm reeling drains fish"), CalmStep.FishStaminaDrain > 0.0);

	FCatFightSimulationState Struggle = Calm;
	Struggle.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	const FCatFightStepResult StruggleStep = FCatFishingFightSimulator::Step(Config, Struggle, FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("outward struggle increases cat drain"), StruggleStep.CatStaminaDrain > CalmStep.CatStaminaDrain);
	TestTrue(TEXT("outward struggle increases fish drain"), StruggleStep.FishStaminaDrain > CalmStep.FishStaminaDrain);
	TestEqual(TEXT("same input is deterministic"),
		FCatFishingFightSimulator::Step(Config, Struggle, FVector::ZeroVector, FVector::ForwardVector).AbsoluteRodWear,
		StruggleStep.AbsoluteRodWear);
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
