#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingFightWorkModel.h"

namespace CatFishingFishIntentTests
{
	constexpr double PricePerMeter = 5.0 / 3.0;

	FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.1;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 10.0;
		Config.FishStrength = 80.0;
		Config.FishMassKilograms = 3.0;
		Config.ForcePerStrengthNewtons = 1.0;
		Config.CatStaminaMaximum = 100.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
		Config.FishStaminaPerUnfulfilledMeter = PricePerMeter;
		Config.MaximumLineLengthCentimeters = 1500.0;
		Config.RodDurability = 1000.0;
		return Config;
	}

	FCatFightSimulationState MakeState()
	{
		FCatFightSimulationState State;
		State.CatStamina = 100.0;
		State.FishStamina = 100.0;
		State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		State.LineLengthCentimeters = 1000.0;
		State.FishEffortRatio = 1.0;
		State.CatAction = ECatFightCatAction::None;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		return State;
	}

	FCatFightRodConstraintInput MakeRod()
	{
		FCatFightRodConstraintInput Rod;
		Rod.bRodHeld = true;
		Rod.RodForwardWorld = FVector::ForwardVector;
		return Rod;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentProjectionTest,
	"Catfishing.Unit.Fishing.IntentCost.SignedProjectionChargesMissingProgressWithoutReverseCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FExample
	{
		double AngleDegrees;
		double ExpectedProgressCentimeters;
		double ExpectedUnfulfilledCentimeters;
	};
	// 同样想游5m、实际游3m：角度是实际运动与意图之间的夹角，与鱼线方向无关。
	const FExample Examples[] = {
		{0.0, 300.0, 200.0}, {60.0, 150.0, 350.0}, {90.0, 0.0, 500.0},
		{120.0, -150.0, 650.0}, {180.0, -300.0, 800.0}
	};
	for (const FExample& Example : Examples)
	{
		FCatFightFishIntentInput Input;
		Input.IntendedDisplacementCentimeters = FVector(500.0, 0.0, 0.0);
		const double Radians = FMath::DegreesToRadians(Example.AngleDegrees);
		Input.ActualDisplacementCentimeters = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0) * 300.0;
		Input.StaminaPerUnfulfilledMeter = CatFishingFishIntentTests::PricePerMeter;
		FCatFightFishIntentResult Result;
		if (!TestTrue(FString::Printf(TEXT("%.0f度运动可结算"), Example.AngleDegrees),
			FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
		TestEqual(TEXT("五米意图不因实际方向变化而改写"), Result.IntendedDistanceCentimeters, 500.0, 1e-7);
		TestEqual(TEXT("实际进展保留正反方向"), Result.ActualProgressCentimeters, Example.ExpectedProgressCentimeters, 1e-7);
		TestEqual(TEXT("意图缺失按投影计算且反拖超过五米不截断"),
			Result.UnfulfilledDistanceCentimeters, Example.ExpectedUnfulfilledCentimeters, 1e-7);
		TestEqual(TEXT("厘米缺失按每米单价只转换一次"), Result.StaminaDrain,
			Example.ExpectedUnfulfilledCentimeters / 100.0 * Input.StaminaPerUnfulfilledMeter, 1e-7);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentBoundariesTest,
	"Catfishing.Unit.Fishing.IntentCost.ExtraForwardOrSideMotionCannotCreateMissingProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentBoundariesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightFishIntentInput Input;
	Input.IntendedDisplacementCentimeters = FVector(500.0, 0.0, 0.0);
	Input.StaminaPerUnfulfilledMeter = CatFishingFishIntentTests::PricePerMeter;
	FCatFightFishIntentResult Result;
	for (const FVector& Actual : {FVector(600.0, 0.0, 0.0), FVector(500.0, 1000.0, 0.0), FVector(500.0, 0.0, 1000.0)})
	{
		Input.ActualDisplacementCentimeters = Actual;
		if (!TestTrue(TEXT("超额前进或额外侧移是合法运动"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
		TestEqual(TEXT("兑现完整前进意图后侧移不能被向量差模长误收费"), Result.UnfulfilledDistanceCentimeters, 0.0);
		TestEqual(TEXT("超额前进不收费也不负扣费回血"), Result.StaminaDrain, 0.0);
	}
	Input.ActualDisplacementCentimeters = FVector::ZeroVector;
	if (!TestTrue(TEXT("零位移僵持可结算"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
	TestEqual(TEXT("僵持没有实际进展"), Result.ActualProgressCentimeters, 0.0);
	TestEqual(TEXT("僵持未兑现全部五米意图"), Result.UnfulfilledDistanceCentimeters, 500.0, 1e-9);
	TestEqual(TEXT("僵持按未兑现米数收费"), Result.StaminaDrain, 25.0 / 3.0, 1e-9);

	Input.StaminaPerUnfulfilledMeter = 0.0;
	if (!TestTrue(TEXT("零单价仍能观察意图缺失"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
	TestEqual(TEXT("调价不修改实际缺失"), Result.UnfulfilledDistanceCentimeters, 500.0, 1e-9);
	TestEqual(TEXT("零价没有隐藏基础游动费"), Result.StaminaDrain, 0.0);
	Input.StaminaPerUnfulfilledMeter = CatFishingFishIntentTests::PricePerMeter;
	Input.IntendedDisplacementCentimeters = FVector::ZeroVector;
	Input.ActualDisplacementCentimeters = FVector(-1000.0, 500.0, 0.0);
	if (!TestTrue(TEXT("没有主动意图时被拖动仍合法"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
	TestEqual(TEXT("零意图没有可被取消的主动距离"), Result.UnfulfilledDistanceCentimeters, 0.0);
	TestEqual(TEXT("零意图的被动拖动不制造鱼费用"), Result.StaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentInvalidInputTest,
	"Catfishing.Unit.Fishing.IntentCost.InvalidObservationsAndOverflowAreRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentInvalidInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightFishIntentInput Valid;
	Valid.IntendedDisplacementCentimeters = FVector(200.0, 0.0, 0.0);
	Valid.StaminaPerUnfulfilledMeter = CatFishingFishIntentTests::PricePerMeter;
	FCatFightFishIntentResult Result;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	for (const double Invalid : {NaN, Infinity})
	{
		auto Input = Valid;
		Input.IntendedDisplacementCentimeters.X = Invalid;
		TestFalse(TEXT("非有限意图拒绝结算"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result));
		Input = Valid;
		Input.ActualDisplacementCentimeters.Y = Invalid;
		TestFalse(TEXT("非有限实际位移拒绝结算"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result));
		Input = Valid;
		Input.StaminaPerUnfulfilledMeter = Invalid;
		TestFalse(TEXT("非有限单价拒绝结算"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result));
	}
	auto Input = Valid;
	Input.StaminaPerUnfulfilledMeter = -1.0;
	TestFalse(TEXT("负价不能让受阻转为回血"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result));
	Input = Valid;
	if (!TestTrue(TEXT("拒绝前先保留一份真实正费用结果"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
	Input.StaminaPerUnfulfilledMeter = std::numeric_limits<double>::max();
	TestFalse(TEXT("单项有限但最终费用溢出仍拒绝"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result));
	TestEqual(TEXT("计算失败不会遗留上次有效费用"), Result.StaminaDrain, 0.0);
	TestEqual(TEXT("计算失败清除未完成的诊断结果"), Result.UnfulfilledDistanceCentimeters, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentTimeTest,
	"Catfishing.Unit.Fishing.IntentCost.FixedStepPartitionPreservesDistanceAndPrice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FTrajectory
	{
		double EffortRatio;
		double ActualSpeedCentimetersPerSecond;
		double ExpectedTotalDistanceCentimeters;
		double ExpectedTotalDrain;
	};
	const FTrajectory Trajectories[] = {
		{1.0, 0.0, 360.0, 6.0}, {0.5, 0.0, 180.0, 3.0},
		{1.0, 90.0, 180.0, 3.0}, {1.0, -90.0, 540.0, 9.0}, {1.0, 180.0, 0.0, 0.0}
	};
	for (const double Dt : {0.025, 0.05, 0.1})
	{
		for (const FTrajectory& Trajectory : Trajectories)
		{
			double TotalUnfulfilled = 0.0;
			double TotalDrain = 0.0;
			for (int32 Index = 0; Index < FMath::RoundToInt(2.0 / Dt); ++Index)
			{
				FCatFightFishIntentInput Input;
				Input.IntendedDisplacementCentimeters = FVector::ForwardVector * (180.0 * Trajectory.EffortRatio * Dt);
				Input.ActualDisplacementCentimeters = FVector::ForwardVector * (Trajectory.ActualSpeedCentimetersPerSecond * Dt);
				Input.StaminaPerUnfulfilledMeter = CatFishingFishIntentTests::PricePerMeter;
				FCatFightFishIntentResult Result;
				if (!TestTrue(TEXT("相同两秒轨迹的每个分步均合法"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Input, Result))) return false;
				TotalUnfulfilled += Result.UnfulfilledDistanceCentimeters;
				TotalDrain += Result.StaminaDrain;
			}
			TestEqual(TEXT("不同固定步长累计相同未兑现距离"), TotalUnfulfilled, Trajectory.ExpectedTotalDistanceCentimeters, 1e-7);
			TestEqual(TEXT("按距离单价累计不重复乘dt或u平方"), TotalDrain, Trajectory.ExpectedTotalDrain, 1e-7);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentResolvedStepTest,
	"Catfishing.Unit.Fishing.IntentCost.FinalPositionExcludesHistoricalCorrectionAndReplacesProvisionalCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentResolvedStepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFishIntentTests;
	const auto Config = MakeConfig();
	const auto State = MakeState();
	const auto Rod = MakeRod();
	auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	if (!TestTrue(TEXT("从真实模拟器取得待结算结果"), Step.bSucceeded)) return false;
	// 意图为18cm，最终真实前进6cm。额外侧移与历史几何纠偏都不能冒充进展缺失。
	Step.ProposedFishWorldPosition = State.FishWorldPosition + FVector(6.0, 8.0, 0.0);
	Step.FishPositionCorrectionWorldDisplacement = FVector::ZeroVector;
	if (!TestTrue(TEXT("地形后的最终落点重新结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step))) return false;
	TestEqual(TEXT("本步意图采用参考游速与步长"), Step.FishIntendedDistanceCentimeters, 18.0, 1e-7);
	TestEqual(TEXT("最终进展使用实际落点在意图上的投影"), Step.FishActualIntentProgressCentimeters, 6.0, 1e-7);
	TestEqual(TEXT("最终落点缺失十二厘米"), Step.FishUnfulfilledDistanceCentimeters, 12.0, 1e-7);
	TestEqual(TEXT("最终费用替换模拟器的候选费用"), Step.FishStaminaDrain, 0.2, 1e-7);
	TestEqual(TEXT("诊断意图距离来自同一最终结果"), Step.Trace.FishIntendedDistanceCentimeters, Step.FishIntendedDistanceCentimeters);
	TestEqual(TEXT("诊断实际进展保留同一有符号投影"), Step.Trace.FishActualIntentProgressCentimeters, Step.FishActualIntentProgressCentimeters);
	TestEqual(TEXT("诊断缺失距离不保留候选旧值"), Step.Trace.FishUnfulfilledDistanceCentimeters, Step.FishUnfulfilledDistanceCentimeters);
	const double FinalDrain = Step.FishStaminaDrain;
	const FVector HistoricalCorrection(-80.0, 30.0, 0.0);
	Step.ProposedFishWorldPosition += HistoricalCorrection;
	Step.FishPositionCorrectionWorldDisplacement = HistoricalCorrection;
	if (!TestTrue(TEXT("含历史纠偏的最终快照仍可结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step))) return false;
	TestEqual(TEXT("历史纠偏不能伪装成反向拖鱼而增加缺失"), Step.FishUnfulfilledDistanceCentimeters, 12.0, 1e-7);
	TestEqual(TEXT("历史纠偏不增加鱼费用"), Step.FishStaminaDrain, FinalDrain, 1e-7);
	if (!TestTrue(TEXT("相同最终快照允许重复校核"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step))) return false;
	TestEqual(TEXT("重复Finalize不叠加鱼费用"), Step.FishStaminaDrain, FinalDrain, 1e-7);
	Step.FishPositionCorrectionWorldDisplacement = FVector::ZeroVector;
	Step.ProposedFishWorldPosition = State.FishWorldPosition + FVector(18.0, 50.0, 0.0);
	if (!TestTrue(TEXT("最终兑现完整意图后重新结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step))) return false;
	TestEqual(TEXT("最终完整进展清除之前的缺失"), Step.FishUnfulfilledDistanceCentimeters, 0.0);
	TestEqual(TEXT("最终零缺失不残留临时扣费"), Step.FishStaminaDrain, 0.0);
	TestEqual(TEXT("零缺失也清除待裁剪费用"), Step.FishUncappedStaminaDrain, 0.0);
	for (const FVector& IntentDirection : {FVector::RightVector, -FVector::ForwardVector})
	{
		Step.FishEffortDirection = IntentDirection;
		Step.ProposedFishWorldPosition = State.FishWorldPosition + IntentDirection * 6.0;
		if (!TestTrue(TEXT("横游及内游的最终意图进展可结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Step))) return false;
		TestEqual(TEXT("横游或内游被阻不再被鱼线夹角抹去"), Step.FishUnfulfilledDistanceCentimeters, 12.0, 1e-7);
		TestEqual(TEXT("相同意图缺失不按鱼线朝向打折"), Step.FishStaminaDrain, 0.2, 1e-7);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishIntentFreeMotionTest,
	"Catfishing.Unit.Fishing.IntentCost.StartupCanMissIntentWithoutTensionWhileExplicitExemptionsRemain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishIntentFreeMotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingFishIntentTests;
	const auto Config = MakeConfig();
	const auto Rod = MakeRod();
	auto State = MakeState();
	State.FishEffortRatio = 0.5;
	const auto Starting = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	if (!TestTrue(TEXT("未绷线的半力起步可求解"), Starting.bSucceeded)) return false;
	TestEqual(TEXT("该夹具确实没有鱼线张力"), Starting.LineTensionNewtons, 0.0);
	TestEqual(TEXT("半力意图只把参考游速乘一次u"), Starting.FishIntendedDistanceCentimeters, 9.0, 1e-7);
	TestTrue(TEXT("起步尚未兑现意图也按缺失收费"), Starting.FishActualIntentProgressCentimeters < 9.0
		&& Starting.FishUnfulfilledDistanceCentimeters > 0.0 && Starting.FishStaminaDrain > 0.0);
	State.FishVelocityCentimetersPerSecond = FVector(90.0, 0.0, 0.0);
	const auto Fulfilled = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	if (!TestTrue(TEXT("达到半力参考速度的自由游动可求解"), Fulfilled.bSucceeded)) return false;
	TestEqual(TEXT("完整兑现意图的自由游动没有基础费"), Fulfilled.FishStaminaDrain, 0.0, 1e-7);
	State = MakeState();
	State.bOperatorPresent = false;
	const auto Unattended = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("没有操作手的活鱼仍能推进"), Unattended.bSucceeded);
	TestEqual(TEXT("无人操作不扣鱼意图费"), Unattended.FishStaminaDrain, 0.0);
	State = MakeState();
	State.CatAction = ECatFightCatAction::Slack;
	const auto Released = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("未满线右键仍进入恢复豁免"), Released.bSucceeded && Released.bSlackRecoveryActive);
	TestEqual(TEXT("未满线右键仍免鱼费用"), Released.FishStaminaDrain, 0.0);
	auto ExpensiveConfig = Config;
	ExpensiveConfig.FishStaminaPerUnfulfilledMeter = std::numeric_limits<double>::max();
	auto Waived = Released;
	Waived.ProposedFishWorldPosition = State.FishWorldPosition - FVector(1000.0, 0.0, 0.0);
	TestTrue(TEXT("有效右键豁免不会因未收取的高价乘积溢出而拒绝运动"),
		FCatFishingFightSimulator::FinalizeResolvedStep(ExpensiveConfig, State, Rod, Waived));
	TestTrue(TEXT("豁免仍记录完整缺失距离"), Waived.FishUnfulfilledDistanceCentimeters > 1000.0);
	TestEqual(TEXT("豁免不会提交高价费用"), Waived.FishStaminaDrain, 0.0);

	State.LineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	const auto FullLineRight = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	State.CatAction = ECatFightCatAction::None;
	const auto FullLineLocked = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("满线右键与普通锁线均可结算"), FullLineRight.bSucceeded && FullLineLocked.bSucceeded && !FullLineRight.bSlackRecoveryActive);
	TestTrue(TEXT("满线右键不能免除未兑现的起步意图"), FullLineRight.FishStaminaDrain > 0.0);
	TestEqual(TEXT("满线右键完全沿用普通锁线鱼费用"), FullLineRight.FishStaminaDrain, FullLineLocked.FishStaminaDrain, 1e-7);
	State = MakeState();
	State.CatStamina = 0.0;
	auto ExhaustedConfig = Config;
	ExhaustedConfig.PrimaryOperatorCatStrength = 0.0;
	const auto ForcedEscape = FCatFishingFightSimulator::Step(ExhaustedConfig, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("零体力且无助手仍由强拖策略接管"), ForcedEscape.bSucceeded && ForcedEscape.bExhaustedCatEscape);
	TestEqual(TEXT("强拖豁免不被新意图费用破坏"), ForcedEscape.FishStaminaDrain, 0.0);
	State = MakeState();
	State.FishEffortRatio = 0.0;
	State.FishVelocityCentimetersPerSecond = FVector(-100.0, 0.0, 0.0);
	const auto Passive = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
	TestTrue(TEXT("零出力鱼仍保留真实惯性运动"), Passive.bSucceeded
		&& Passive.ProposedFishWorldPosition.X < State.FishWorldPosition.X);
	TestEqual(TEXT("零出力的反向位移不能制造未兑现意图费"), Passive.FishStaminaDrain, 0.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
