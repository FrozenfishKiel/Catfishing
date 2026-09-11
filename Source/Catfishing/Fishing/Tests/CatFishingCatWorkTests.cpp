#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

namespace CatActualWorkTest
{
	FCatFightSimulationConfig Config()
	{
		FCatFightSimulationConfig Value;
		Value.FixedStepSeconds = 0.05;
		Value.PrimaryOperatorCatStrength = 50.0;
		Value.PrimaryOperatorMassKilograms = 5.0;
		Value.FishStrength = 50.0;
		Value.FishMassKilograms = 5.0;
		Value.RodDurability = 1000.0;
		Value.CatStaminaMaximum = 60.0;
		Value.ReelSpeedCentimetersPerSecond = 80.0;
		Value.FishFullEffortSpeedCentimetersPerSecond = 180.0;
		Value.MaximumLineLengthCentimeters = 1500.0;
		return Value;
	}
	FCatFightSimulationState State()
	{
		FCatFightSimulationState Value;
		Value.CatStamina = 60.0;
		Value.FishStamina = 72.0;
		Value.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		Value.LineLengthCentimeters = 500.0;
		Value.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		return Value;
	}
	FCatFightRodConstraintInput Rod()
	{
		FCatFightRodConstraintInput Value;
		Value.bRodHeld = true;
		Value.RodForwardWorld = FVector::RightVector;
		return Value;
	}
	FCatFightStepResult Step(const FCatFightSimulationConfig& Settings, const FCatFightSimulationState& Current,
		const FCatFightRodConstraintInput& Constraint)
	{
		return FCatFishingFightSimulator::Step(Settings, Current, Constraint, FVector::ForwardVector);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingTimedSupportTorqueTest,
	"Catfishing.Unit.Fishing.Effort.BlockedRodSupportCannotScaleWithMaximumAngularSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingTimedSupportTorqueTest::RunTest(const FString& Parameters)
{
	using namespace CatActualWorkTest;
	const auto Settings = Config();
	// A fully blocked physical motor reports effort*time but zero positive angular work.
	// Maximum angular speed is absent from this pricing input and cannot fabricate work.
	auto Constraint = Rod();
	Constraint.CatRodExertionSquaredSeconds = Settings.FixedStepSeconds;
	Constraint.CatRodPositiveWorkRadians = 0.0;
	const auto Result = Step(Settings, State(), Constraint);
	TestTrue(TEXT("physical support observation enters the same fee step"), Result.bSucceeded);
	TestEqual(TEXT("blocked support is billed by actual duration"), Result.CatStaminaDrain / Settings.FixedStepSeconds,
		Settings.CatSupportStaminaPerSecond, 1e-6);
	TestEqual(TEXT("the same load is charged once"), Result.CatRodSupportStaminaDrain, 0.0, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCatWorkPacingTest,
	"Catfishing.Unit.Fishing.Effort.ActualWorkAndSupportPreservePricingIndependenceUnderFiniteReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCatWorkPacingTest::RunTest(const FString& Parameters)
{
	using namespace CatActualWorkTest;
	auto Settings = Config();
	auto Current = State();
	auto Constraint = Rod();
	Constraint.CatRodExertionSquaredSeconds = Settings.FixedStepSeconds;
	const auto Blocked = Step(Settings, Current, Constraint);
	const double BlockedRate = Blocked.CatStaminaDrain / Settings.FixedStepSeconds;
	TestTrue(TEXT("持续受阻的参考耗体约两点每秒"), Blocked.bSucceeded && BlockedRate >= 1.9 && BlockedRate <= 2.1);
	Current.CatAction = ECatFightCatAction::Pull;
	Settings.FishStrength = 20.0;
	Current.FishVelocityCentimetersPerSecond = FVector(-80.0, 0.0, 0.0);
	Constraint.CarrierVelocityCentimetersPerSecond = FVector(-40.0, 0.0, 0.0);
	Constraint.CatRodPositiveWorkRadians = 0.16;
	const auto Heavy = Step(Settings, Current, Constraint);
	FCatFightOperatorMovementCostInput Movement;
	Movement.MoveIntentWorld = -FVector::ForwardVector;
	Movement.ActualDisplacementCentimeters = Constraint.CarrierVelocityCentimetersPerSecond * Settings.FixedStepSeconds;
	Movement.MaximumMoveSpeedCentimetersPerSecond = 40.0;
	Movement.FixedStepSeconds = Settings.FixedStepSeconds;
	Movement.ActiveStrength = Settings.PrimaryOperatorCatStrength;
	FCatFightOperatorMovementCostResult PersonalMovement;
	if (!TestTrue(TEXT("真实身体位移独立计算个人账"), FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(Movement, PersonalMovement))) return false;
	const double HeavyRate = (PersonalMovement.StaminaDrain + Heavy.GetRodActionStaminaDrain()) / Settings.FixedStepSeconds;
	TestTrue(TEXT("完成身体移动意图后只支付真实竿操作账"), Heavy.bSucceeded && FMath::IsFinite(HeavyRate) && HeavyRate > 0.0);
	TestTrue(TEXT("完成意图的身体不耗体，收线与转杆仍各支付实际做功"),
		PersonalMovement.StaminaDrain == 0.0 && Heavy.CatReelStaminaDrain > 0.0 && Heavy.CatRodWorkStaminaDrain > 0.0);

	Settings.CatRodStaminaCostPerStrengthRadian *= 20.0;
	Settings.CatStaminaCostPerStrengthCentimeter *= 10.0;
	Settings.CatSupportStaminaPerSecond *= 10.0;
	Settings.CatUnloadedWorkMultiplier *= 10.0;
	const auto Repriced = Step(Settings, Current, Constraint);
	TestTrue(TEXT("费用配置实际改变耗体"), Repriced.bSucceeded && Repriced.CatStaminaDrain > Heavy.CatStaminaDrain);
	TestTrue(TEXT("费用不改变鱼的位置求解"), Repriced.ProposedFishWorldPosition.Equals(Heavy.ProposedFishWorldPosition, 1e-9));
	TestEqual(TEXT("费用不改变线长"), Repriced.LineLengthCentimeters, Heavy.LineLengthCentimeters);
	TestEqual(TEXT("费用不改变收线速度"), Repriced.ActualReelDistanceCentimeters, Heavy.ActualReelDistanceCentimeters);
	TestEqual(TEXT("费用不改变提交给竿的完整鱼线力"), Repriced.LineTensionNewtons, Heavy.LineTensionNewtons);
	TestEqual(TEXT("费用不改变杆杠杆与实际张力"), Repriced.RodLeverageMultiplier, Heavy.RodLeverageMultiplier);
	TestEqual(TEXT("费用不改变张力"), Repriced.NormalizedTension, Heavy.NormalizedTension);
	TestEqual(TEXT("费用不改变鱼耗体"), Repriced.FishStaminaDrain, Heavy.FishStaminaDrain);
	TestEqual(TEXT("费用不改变鱼竿磨损"), Repriced.RodWearDelta, Heavy.RodWearDelta);

	Settings = Config();
	Settings.FishStrength = 20.0;
	Settings.FishStaminaPerUnfulfilledMeter = 20.0;
	const auto PhaseChanged = Step(Settings, Current, Constraint);
	TestEqual(TEXT("鱼每秒耗体价格不放大猫的同一工作量"), PhaseChanged.CatStaminaDrain, Heavy.CatStaminaDrain, 1e-9);

	Current = State();
	Current.LineLengthCentimeters = 800.0;
	Constraint = Rod();
	Constraint.CatRodExertionSquaredSeconds = Settings.FixedStepSeconds * 0.1;
	Constraint.CatRodPositiveWorkRadians = 0.03;
	const auto Light = Step(Settings, Current, Constraint);
	const double LightRate = Light.CatStaminaDrain / Settings.FixedStepSeconds;
	TestTrue(TEXT("无负载微调保留少量费用"), Light.bSucceeded && LightRate >= 0.2 && LightRate <= 0.5);
	Current.CatStamina = 30.0;
	Current.CatAction = ECatFightCatAction::Slack;
	const auto Recovery = Step(Settings, Current, Constraint);
	TestTrue(TEXT("右键仍恢复体力且双方无正向费用"), Recovery.bSucceeded && Recovery.CatStaminaDrain < 0.0
		&& Recovery.GetRodActionStaminaDrain() == 0.0 && Recovery.FishStaminaDrain == 0.0);
	AddInfo(FString::Printf(TEXT("Event=fishing_cat_work_reference_rates Source=ControlledSnapshot LightPerSecond=%.3f BlockedPerSecond=%.3f HeavyPerSecond=%.3f StaminaPool=60"),
		LightRate, BlockedRate, HeavyRate));
	return !HasAnyErrors();
}

#endif
