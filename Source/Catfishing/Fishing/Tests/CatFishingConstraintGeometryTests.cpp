#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"

namespace
{
	FCatFightSimulationConfig MakeGeometryConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.05;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 5.0;
		Config.FishMassKilograms = 5.0;
		Config.FishStrength = 50.0;
		Config.CatStaminaMaximum = 100.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		Config.MaximumLineLengthCentimeters = 1000.0;
		Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 10000.0;
		Config.RodDurability = 1000.0;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPlanarLineGeometryTest,
	"Catfishing.Unit.Fishing.Simulation.PlanarConstraintResolvesThreeDimensionalLineLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPlanarLineGeometryTest::RunTest(const FString& Parameters)
{
	const auto Config = MakeGeometryConfig();
	for (const double Height : {0.0, 10.0, 60.0, 99.0, 100.0, 120.0})
	{
		FCatFightSimulationState State;
		State.bFishExhausted = true;
		State.FishWorldPosition = FVector(70.0, 20.0, 0.0);
		State.LineLengthCentimeters = 100.0;
		FCatFightRodConstraintInput Rod;
		Rod.bRodHeld = true;
		Rod.RodTipWorldPosition = FVector(-30.0, 20.0, Height);
		const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
		if (!TestTrue(TEXT("各高差下均返回合法运动"), Step.bSucceeded)) return false;
		const double Distance = FVector::Distance(Rod.RodTipWorldPosition, Step.ProposedFishWorldPosition);
		TestEqual(TEXT("可行时满足三维线长，不可行时仅保留真实高差"), Distance, FMath::Max(100.0, Height), 1e-6);
		TestEqual(TEXT("几何修正不能凭空放线"), Step.LineLengthCentimeters, State.LineLengthCentimeters);
		TestEqual(TEXT("水平求解保持水面高度"), Step.ProposedFishWorldPosition.Z, 0.0);
		TestTrue(TEXT("力竭收尾保留实际竿尖观察"), Step.Trace.ConstraintRodEndWorldPosition.Equals(Rod.RodTipWorldPosition, 1e-6));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPlanarCorrectionBoundTest,
	"Catfishing.Unit.Fishing.Simulation.PlanarConstraintKeepsSpeedBoundAndElevatedStalemate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPlanarCorrectionBoundTest::RunTest(const FString& Parameters)
{
	auto Config = MakeGeometryConfig();
	FCatFightSimulationState State;
	State.CatStamina = 100.0;
	State.FishStamina = 100.0;
	State.LineLengthCentimeters = 100.0;
	State.FishWorldPosition = FVector(80.0, 0.0, 0.0);
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true;
	Rod.RodTipWorldPosition = FVector(0.0, 0.0, 60.0);
	Rod.RodForwardWorld = (State.FishWorldPosition - Rod.RodTipWorldPosition).GetSafeNormal();
	for (const double Dt : {0.025, 0.05, 0.1})
	{
		Config.FixedStepSeconds = Dt;
		const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
		if (!TestTrue(TEXT("高差僵持步骤成功"), Step.bSucceeded)) return false;
		TestTrue(TEXT("相同对抗力在高差下也能抵消外游"), Step.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 1e-6));
	}
	Config.FixedStepSeconds = 0.05;
	Config.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.FishWorldPosition.X = 200.0;
	const auto Limited = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ZeroVector);
	TestTrue(TEXT("限速修正步骤成功"), Limited.bSucceeded);
	TestEqual(TEXT("大误差仍只能按配置速度逐步修正"),
		FVector::Distance(State.FishWorldPosition, Limited.ProposedFishWorldPosition), 8.0, 1e-6);
	TestTrue(TEXT("未消除的超长仍可从最终位置观察"), Limited.StraightLineDistanceCentimeters > Limited.LineLengthCentimeters);
	return !HasAnyErrors();
}

#endif
