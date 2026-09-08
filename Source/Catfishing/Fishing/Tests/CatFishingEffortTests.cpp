#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingFightWorkModel.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"

#include <limits>

namespace CatFishingEffortTest
{
	FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.1;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 10.0;
		Config.FishMassKilograms = 3.0;
		Config.FishStrength = 40.0;
		Config.CatStaminaMaximum = 1000.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		Config.MaximumLineLengthCentimeters = 1000.0;
		Config.RodDurability = 1000.0;
		return Config;
	}

	FCatFightSimulationState MakeState(const ECatFightCatAction Action = ECatFightCatAction::None)
	{
		FCatFightSimulationState State;
		State.CatStamina = 500.0;
		State.FishStamina = 1000.0;
		State.LineLengthCentimeters = 500.0;
		State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		State.CatAction = Action;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		return State;
	}

	FCatFightRodConstraintInput MakeHeldConstraint()
	{
		FCatFightRodConstraintInput Constraint;
		Constraint.bRodHeld = true;
		Constraint.RodForwardWorld = FVector::ForwardVector;
		return Constraint;
	}

	FCatFightRodConstraintInput MakeCombinedEffortConstraint()
	{
		FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
		Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(-100.0, 0.0, 0.0);
		Constraint.CarrierVelocityCentimetersPerSecond = FVector(-40.0, 0.0, 0.0);
		Constraint.RodTipVelocityCentimetersPerSecond = FVector(-200.0, 0.0, 0.0);
		Constraint.CatRodExertionSquaredSeconds = 0.04;
		Constraint.CatRodPositiveWorkRadians = 0.02;
		return Constraint;
	}

	FCatFightStepResult Step(const FCatFightSimulationConfig& Config,
		const FCatFightSimulationState& State, const FCatFightRodConstraintInput& Constraint)
	{
		return FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMovementEffortIntentTest,
	"Catfishing.Unit.Fishing.Effort.MovementRequiresActiveOpposingIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingMovementEffortIntentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState State = MakeState();
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(-100.0, 0.0, 0.0);
	Constraint.CarrierVelocityCentimetersPerSecond = FVector(-40.0, 0.0, 0.0);
	Constraint.RodTipVelocityCentimetersPerSecond = FVector(-200.0, 0.0, 0.0);
	const auto Away = Step(Config, State, Constraint);
	TestTrue(TEXT("主动后退的绷线步骤有效"), Away.bSucceeded && Away.bLineTaut);
	TestTrue(TEXT("主动后退产生独立移动耗体"), Away.CatMovementStaminaDrain > 0.0);
	TestEqual(TEXT("移动实际努力只读取身体平移，不含竿尖转动"),
		Away.CatMovementActualCentimeters,
		Constraint.CarrierVelocityCentimetersPerSecond.Size() * Config.FixedStepSeconds, 1e-6);

	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector::ZeroVector;
	const auto Passive = Step(Config, State, Constraint);
	TestTrue(TEXT("身体被动移动仍可求解约束"), Passive.bSucceeded);
	TestEqual(TEXT("没有主动输入就没有移动努力"), Passive.CatMovementIntentCentimeters, 0.0);
	TestEqual(TEXT("被动移动不生成移动耗体"), Passive.CatMovementStaminaDrain, 0.0);

	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(100.0, 0.0, 0.0);
	const auto Toward = Step(Config, State, Constraint);
	TestTrue(TEXT("朝鱼移动步骤有效"), Toward.bSucceeded);
	TestEqual(TEXT("朝鱼移动没有额外对抗移动耗体"), Toward.CatMovementStaminaDrain, 0.0);
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(0.0, 100.0, 0.0);
	const auto Across = Step(Config, State, Constraint);
	TestTrue(TEXT("纯横向移动步骤有效"), Across.bSucceeded);
	TestEqual(TEXT("纯横向移动没有额外沿线移动耗体"), Across.CatMovementStaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEffortIsolationTest,
	"Catfishing.Unit.Fishing.Effort.RodEffortDoesNotDuplicateMovementOrHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEffortIsolationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CatRodExertionSquaredSeconds = 0.04;
	Constraint.CatRodPositiveWorkRadians = 0.02;
	Constraint.RodTipVelocityCentimetersPerSecond = FVector(-200.0, 0.0, 0.0);
	const auto HoldBaseline = Step(MakeConfig(), MakeState(), MakeHeldConstraint());
	const auto Result = Step(MakeConfig(), MakeState(), Constraint);
	TestTrue(TEXT("主动转杆步骤有效"), Result.bSucceeded);
	TestTrue(TEXT("转杆拥有独立正耗体"), Result.CatRodStaminaDrain > 0.0);
	TestEqual(TEXT("转杆不会冒充身体移动"), Result.CatMovementStaminaDrain, 0.0);
	TestEqual(TEXT("转杆实际做功不能抵扣共享支撑"), Result.CatHoldStaminaDrain,
		HoldBaseline.CatStaminaDrain, 1e-6);
	TestEqual(TEXT("转杆支撑仅收超出共享支撑的部分"), Result.CatRodSupportStaminaDrain,
		FMath::Max(0.0, MakeConfig().CatSupportStaminaPerSecond * Constraint.CatRodExertionSquaredSeconds - HoldBaseline.CatHoldStaminaDrain), 1e-6);
	TestEqual(TEXT("仅转杆时总耗体由转杆与必要持竿差额组成"), Result.CatStaminaDrain,
		Result.CatRodStaminaDrain + Result.CatHoldStaminaDrain, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCombinedEffortAccountingTest,
	"Catfishing.Unit.Fishing.Effort.ConcurrentMovementReelAndRodUseSeparateCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingCombinedEffortAccountingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const auto Result = Step(MakeConfig(), MakeState(ECatFightCatAction::Pull), MakeCombinedEffortConstraint());
	TestTrue(TEXT("三个操作同时参与的步骤有效"), Result.bSucceeded);
	TestTrue(TEXT("移动分项独立耗体"), Result.CatMovementStaminaDrain > 0.0);
	TestTrue(TEXT("收线分项独立耗体"), Result.CatReelStaminaDrain > 0.0);
	TestTrue(TEXT("转杆分项独立耗体"), Result.CatRodStaminaDrain > 0.0);
	TestTrue(TEXT("三个操作参与受载对抗时鱼仍消耗对抗体力"),
		Result.FishUnfulfilledDistanceCentimeters > 0.0 && Result.FishStaminaDrain > 0.0);
	TestTrue(TEXT("三个操作仍只保留一份共享支撑"), Result.CatHoldStaminaDrain > 0.0 && Result.CatRodSupportStaminaDrain == 0.0);
	TestEqual(TEXT("总耗体等于三操作实际费用及一次共享支撑"), Result.CatStaminaDrain,
		Result.CatMovementStaminaDrain + Result.CatReelStaminaDrain + Result.CatRodStaminaDrain + Result.CatHoldStaminaDrain, 1e-6);
	TestEqual(TEXT("主位与共同分担的耗体之和守恒"), Result.CatStaminaDrain,
		Result.CatMovementStaminaDrain + Result.GetSharedCatStaminaDrain(), 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishEffortLoadTest,
	"Catfishing.Unit.Fishing.Effort.FishPaysForUnfulfilledProgressInsteadOfCatStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishEffortLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig LowLoadConfig = MakeConfig();
	LowLoadConfig.PrimaryOperatorCatStrength = 20.0;
	LowLoadConfig.FishStrength = 80.0;
	FCatFightSimulationConfig HighLoadConfig = LowLoadConfig;
	HighLoadConfig.PrimaryOperatorCatStrength = 60.0;
	const auto LowLoad = Step(LowLoadConfig, MakeState(), MakeHeldConstraint());
	const auto HighLoad = Step(HighLoadConfig, MakeState(), MakeHeldConstraint());
	TestTrue(TEXT("不同相对负载步骤都有效"), LowLoad.bSucceeded && HighLoad.bSucceeded);
	TestEqual(TEXT("比较保持鱼主动意图距离相同"), LowLoad.FishIntendedDistanceCentimeters,
		HighLoad.FishIntendedDistanceCentimeters, 1e-6);
	TestEqual(TEXT("比较保持几何张力相同"), LowLoad.NormalizedTension, HighLoad.NormalizedTension, 1e-6);
	TestEqual(TEXT("相同实际端点不因猫属性不同而伪造意图缺失"),
		HighLoad.FishUnfulfilledDistanceCentimeters, LowLoad.FishUnfulfilledDistanceCentimeters, 1e-6);
	TestEqual(TEXT("尚未发生不同位移时鱼的努力费用相同"), HighLoad.FishStaminaDrain, LowLoad.FishStaminaDrain, 1e-6);
	auto MovingRod = MakeHeldConstraint(); MovingRod.RodTipWorldPosition.X = 1.0;
	const auto Relieved = Step(LowLoadConfig, MakeState(), MovingRod);
	TestTrue(TEXT("猫实际向鱼移动让鱼完成更多主动进展并降低费用"),
		Relieved.FishActualIntentProgressCentimeters > LowLoad.FishActualIntentProgressCentimeters
		&& Relieved.FishStaminaDrain < LowLoad.FishStaminaDrain);

	FCatFightSimulationConfig DragConfig = MakeConfig();
	DragConfig.PrimaryOperatorCatStrength = 200.0;
	const FCatFightSimulationState DragState = MakeState(ECatFightCatAction::Pull);
	const auto Dragged = Step(DragConfig, DragState, MakeHeldConstraint());
	TestTrue(TEXT("强猫收线步骤有效"), Dragged.bSucceeded);
	TestTrue(TEXT("鱼被实际拖向主动游动的反方向"), Dragged.ProposedFishWorldPosition.X < DragState.FishWorldPosition.X);
	TestTrue(TEXT("反向被拖保留负进展，缺失距离不截到意图长度"),
		Dragged.FishActualIntentProgressCentimeters < 0.0
		&& Dragged.FishUnfulfilledDistanceCentimeters > Dragged.FishIntendedDistanceCentimeters);
	TestTrue(TEXT("反抗被拖按全部缺失距离消耗鱼体力"), Dragged.FishStaminaDrain > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingIndependentStaminaPricingTest,
	"Catfishing.Unit.Fishing.Effort.CatAndFishPricingRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingIndependentStaminaPricingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	const FCatFightRodConstraintInput Constraint = MakeCombinedEffortConstraint();
	const auto Baseline = Step(Config, State, Constraint);
	FCatFightSimulationConfig CatPricing = Config;
	CatPricing.CatStaminaCostPerStrengthCentimeter *= 2.0;
	CatPricing.CatMovementStaminaMultiplier = 2.0;
	CatPricing.CatReelStaminaMultiplier = 2.0;
	CatPricing.CatRodStaminaMultiplier = 2.0;
	CatPricing.CatLoadStaminaMultiplier = 2.0;
	const auto ChangedCat = Step(CatPricing, State, Constraint);
	TestTrue(TEXT("猫独立调价步骤有效"), Baseline.bSucceeded && ChangedCat.bSucceeded);
	TestTrue(TEXT("猫调价提高猫耗体"), ChangedCat.CatStaminaDrain > Baseline.CatStaminaDrain);
	TestEqual(TEXT("仅改猫体力参数不会改变鱼耗体"), ChangedCat.FishStaminaDrain, Baseline.FishStaminaDrain, 1e-6);

	FCatFightSimulationConfig FishPricing = Config;
	FishPricing.FishStaminaPerUnfulfilledMeter *= 2.0;
	const auto ChangedFish = Step(FishPricing, State, Constraint);
	TestTrue(TEXT("鱼独立调价步骤有效"), ChangedFish.bSucceeded);
	TestTrue(TEXT("鱼调价提高鱼耗体"), ChangedFish.FishStaminaDrain > Baseline.FishStaminaDrain);
	TestEqual(TEXT("仅改鱼体力参数不会改变猫耗体"), ChangedFish.CatStaminaDrain, Baseline.CatStaminaDrain, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingEffortReleaseAndExhaustionTest,
	"Catfishing.Unit.Fishing.Effort.ExhaustionAndReleasedSlackRespectEffortBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingEffortReleaseAndExhaustionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const FCatFightSimulationConfig Config = MakeConfig();
	FCatFightSimulationState ExhaustedState = MakeState(ECatFightCatAction::Pull);
	ExhaustedState.bFishExhausted = true;
	ExhaustedState.FishStamina = 0.0;
	const auto Exhausted = Step(Config, ExhaustedState, MakeCombinedEffortConstraint());
	TestTrue(TEXT("力竭后仍可求解三个操作的收尾约束"), Exhausted.bSucceeded);
	TestEqual(TEXT("力竭鱼收尾不再扣猫体力"), Exhausted.CatStaminaDrain, 0.0);
	TestEqual(TEXT("力竭后移动不扣体"), Exhausted.CatMovementStaminaDrain, 0.0);
	TestEqual(TEXT("力竭后收线不扣体"), Exhausted.CatReelStaminaDrain, 0.0);
	TestEqual(TEXT("力竭后转杆不扣体"), Exhausted.CatRodStaminaDrain, 0.0);
	TestEqual(TEXT("力竭后持竿不扣体"), Exhausted.CatHoldStaminaDrain, 0.0);
	TestEqual(TEXT("力竭鱼不再扣自身体力"), Exhausted.FishStaminaDrain, 0.0);

	const auto Released = Step(Config, MakeState(ECatFightCatAction::Slack), MakeHeldConstraint());
	TestTrue(TEXT("完全放线步骤有效"), Released.bSucceeded);
	TestEqual(TEXT("真正解除约束后无猫负载"), Released.CatNormalizedEffortLoad, 0.0);
	TestTrue(TEXT("未满线右键保留恢复豁免，即使起步尚未完成全部意图"),
		Released.bSlackRecoveryActive && Released.FishUnfulfilledDistanceCentimeters > 0.0);
	TestEqual(TEXT("右键恢复期间鱼自身游动不扣体力"), Released.FishStaminaDrain, 0.0);
	TestEqual(TEXT("无主动操作时放线不收取分项费用"),
		Released.CatMovementStaminaDrain + Released.GetSharedCatStaminaDrain(), 0.0);
	TestTrue(TEXT("解除约束且无主动努力时猫恢复体力"), Released.CatStaminaDrain < 0.0);
	TestTrue(TEXT("放线恢复不突破体力上限"),
		MakeState().CatStamina - Released.CatStaminaDrain <= Config.CatStaminaMaximum);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingTimedFishEffortTuningTest,
	"Catfishing.Unit.Fishing.Effort.CatTimedSupportIsIndependentOfFishIntentDistancePricing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingTimedFishEffortTuningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CatRodExertionSquaredSeconds = 0.04;
	Constraint.CatRodPositiveWorkRadians = 0.0;
	Config.FishStaminaPerUnfulfilledMeter = 0.0;
	const auto Zero = Step(Config, MakeState(), Constraint);
	Config.FishStaminaPerUnfulfilledMeter = 1.0;
	const auto One = Step(Config, MakeState(), Constraint);
	Config.FishStaminaPerUnfulfilledMeter = 2.0;
	const auto Two = Step(Config, MakeState(), Constraint);
	TestTrue(TEXT("三种鱼每米费率都可求解"), Zero.bSucceeded && One.bSucceeded && Two.bSucceeded);
	TestTrue(TEXT("猫受阻仍承担支撑"), Zero.CatStaminaDrain > 0.0);
	TestEqual(TEXT("鱼每米费率不改变猫费用"), Zero.CatStaminaDrain, One.CatStaminaDrain);
	TestEqual(TEXT("提高鱼每米费率仍不改变猫费用"), Two.CatStaminaDrain, One.CatStaminaDrain);
	TestEqual(TEXT("完全受阻不伪造转杆正功费用"), One.CatRodWorkStaminaDrain, 0.0);
	TestTrue(TEXT("鱼受阻仍按未完成意图的每米费率付费"),
		Two.FishStaminaDrain > One.FishStaminaDrain && One.FishStaminaDrain > Zero.FishStaminaDrain);
	Config.CatSupportStaminaPerSecond = 0.0;
	const auto NoSupport = Step(Config, MakeState(), Constraint);
	TestEqual(TEXT("关闭时间支撑后受阻猫不再被意图距离计费"), NoSupport.CatStaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRightButtonRecoveryTest,
	"Catfishing.Unit.Fishing.Effort.RightButtonWaivesCostsOnlyWhileLineRemainsAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRightButtonRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	auto Config = MakeConfig();
	Config.SlackStaminaRegenPerSecond = 2.75;
	Config.SecondCatStrength = 30.0;
	const auto Constraint = MakeCombinedEffortConstraint();
	for (const auto Motion : {ECatFishMotionIntent::CalmOrInward, ECatFishMotionIntent::StrugglingOutward})
	{
		for (const bool bAtLineLimit : {false, true})
		{
			for (const double Stamina : {1e-9, 500.0, 999.99, 1000.0, 1001.0})
			{
				auto State = MakeState(ECatFightCatAction::Slack);
				State.CatStamina = Stamina;
				State.FishStamina = 0.1; // 未满线右键免耗仍需保留低体力尾数。
				State.MotionIntent = Motion;
				State.LineLengthCentimeters = bAtLineLimit ? Config.MaximumLineLengthCentimeters : 800.0;
				State.FishWorldPosition.X = bAtLineLimit ? Config.MaximumLineLengthCentimeters : 500.0;
				const auto Result = Step(Config, State, Constraint);
				if (!TestTrue(TEXT("正常右键在各阶段、线长和体力边界均可求解"), Result.bSucceeded)) return false;
				if (bAtLineLimit)
				{
					State.CatAction = ECatFightCatAction::None;
					const auto Locked = Step(Config, State, Constraint);
					TestFalse(TEXT("满线时不能靠右键恢复体力"), Result.bSlackRecoveryActive);
					TestTrue(TEXT("满线时恢复普通角力耗体"), Result.CatStaminaDrain > 0.0 && Result.FishStaminaDrain > 0.0);
					TestEqual(TEXT("满线移动转杆与支撑费用等同无右键"), Result.CatStaminaDrain, Locked.CatStaminaDrain, 1e-9);
					TestEqual(TEXT("满线助手分摊费用等同无右键"), Result.GetSharedCatStaminaDrain(), Locked.GetSharedCatStaminaDrain(), 1e-9);
					TestEqual(TEXT("满线鱼低体力结算等同无右键"), Result.FishStaminaDrain, Locked.FishStaminaDrain, 1e-9);
					TestEqual(TEXT("满线磨损仍只使用普通受力费用"), Result.RodWearDelta, Locked.RodWearDelta, 1e-9);
					TestEqual(TEXT("满线不会继续出线"), Result.LineLengthCentimeters, Config.MaximumLineLengthCentimeters);
					continue;
				}
				TestTrue(TEXT("未满线右键继续恢复体力"), Result.bSlackRecoveryActive);
				TestEqual(TEXT("右键移动不扣体"), Result.CatMovementStaminaDrain, 0.0);
				TestEqual(TEXT("右键收线不扣体"), Result.CatReelStaminaDrain, 0.0);
				TestEqual(TEXT("右键转杆不扣体"), Result.CatRodStaminaDrain, 0.0);
				TestEqual(TEXT("右键保持不扣体"), Result.CatHoldStaminaDrain, 0.0);
				TestEqual(TEXT("辅助出力也不产生共同扣费"), Result.GetSharedCatStaminaDrain(), 0.0);
				TestEqual(TEXT("右键期间鱼不产生待结算费用"), Result.FishUncappedStaminaDrain, 0.0);
				TestEqual(TEXT("右键期间鱼不耗体或清空低体力尾数"), Result.FishStaminaDrain, 0.0);
				TestEqual(TEXT("回体只取配置速度及距上限的余量，已达或超过上限不倒扣"), Result.CatStaminaDrain,
					-FMath::Min(FMath::Max(0.0, Config.CatStaminaMaximum - Stamina), 0.275), 1e-9);
				TestEqual(TEXT("左右键裁决后的右键不主动收线"), Result.RequestedReelDistanceCentimeters, 0.0);
			}
		}
	}

	auto State = MakeState(ECatFightCatAction::Slack);
	Config.SlackStaminaRegenPerSecond *= 2.0;
	TestEqual(TEXT("回体速度继续由配置控制"), Step(Config, State, Constraint).CatStaminaDrain, -0.55, 1e-9);
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.CatStamina = 0.0;
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 0.0;
	const auto ExhaustedFish = Step(Config, State, Constraint);
	TestTrue(TEXT("鱼已力竭的收尾仍可右键回体"), ExhaustedFish.bSucceeded && ExhaustedFish.bSlackRecoveryActive && !ExhaustedFish.bExhaustedCatEscape);
	TestEqual(TEXT("收尾零体力回体按原配置结算"), ExhaustedFish.CatStaminaDrain, -0.55, 1e-9);
	State.bOperatorPresent = false;
	const auto Unattended = Step(Config, State, Constraint);
	TestFalse(TEXT("无人值守放线不冒充玩家右键恢复"), Unattended.bSlackRecoveryActive);
	TestEqual(TEXT("离竿不再给旧玩家回体"), Unattended.CatStaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingLineLimitRecoveryTransitionTest,
	"Catfishing.Unit.Fishing.Effort.LineLimitStopsRecoveryOnArrivalAndUsesFinalPaidLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingLineLimitRecoveryTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const auto Config = MakeConfig();
	const auto Constraint = MakeHeldConstraint();
	auto State = MakeState(ECatFightCatAction::Slack);
	State.LineLengthCentimeters = Config.MaximumLineLengthCentimeters - 1.0;
	State.FishWorldPosition.X = State.LineLengthCentimeters;
	State.FishVelocityCentimetersPerSecond = FVector(75.0, 0.0, 0.0);
	auto ReachedLimit = Step(Config, State, Constraint);
	if (!TestTrue(TEXT("从未满线向外游的当步确实到达上限"), ReachedLimit.bSucceeded
		&& ReachedLimit.LineLengthCentimeters == Config.MaximumLineLengthCentimeters)) return false;
	TestFalse(TEXT("跨到上限当步立即停止右键恢复"), ReachedLimit.bSlackRecoveryActive);
	TestTrue(TEXT("跨上限当步按实际张力承担角力耗体"), ReachedLimit.CatStaminaDrain > 0.0 && ReachedLimit.FishStaminaDrain > 0.0);
	const auto BeforeRefinalize = ReachedLimit;
	TestTrue(TEXT("满线结果可以按最终落点再次结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Constraint, ReachedLimit));
	TestEqual(TEXT("再次结算不会叠加猫费用"), ReachedLimit.CatStaminaDrain, BeforeRefinalize.CatStaminaDrain, 1e-9);
	TestEqual(TEXT("再次结算不会叠加鱼费用"), ReachedLimit.FishStaminaDrain, BeforeRefinalize.FishStaminaDrain, 1e-9);
	TestEqual(TEXT("再次结算不会叠加鱼竿磨损"), ReachedLimit.AbsoluteRodWear, BeforeRefinalize.AbsoluteRodWear, 1e-9);

	// 模拟候选出线后被岸线挡回：最终未用尽线杯，应重新依据最终事实恢复，而非沿用临时满线判断。
	auto ShoreBlocked = BeforeRefinalize;
	ShoreBlocked.LineLengthCentimeters = State.LineLengthCentimeters;
	ShoreBlocked.ProposedFishWorldPosition = State.FishWorldPosition;
	ShoreBlocked.LineTensionNewtons = 0.0;
	ShoreBlocked.NormalizedTension = 0.0;
	TestTrue(TEXT("岸线最终落点仍可重算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Constraint, ShoreBlocked));
	TestTrue(TEXT("最终没有放到底时重新启用正常右键回体"), ShoreBlocked.bSlackRecoveryActive && ShoreBlocked.CatStaminaDrain < 0.0);
	TestEqual(TEXT("最终松线不会留下临时鱼耗体"), ShoreBlocked.FishStaminaDrain, 0.0);
	TestEqual(TEXT("最终松线不会留下临时磨损"), ShoreBlocked.AbsoluteRodWear, State.AbsoluteRodWear);

	State.LineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	State.FishWorldPosition.X = Config.MaximumLineLengthCentimeters - 100.0;
	State.FishVelocityCentimetersPerSecond = FVector::ZeroVector;
	const auto FishReturns = FCatFishingFightSimulator::Step(Config, State, Constraint, -FVector::ForwardVector);
	TestTrue(TEXT("满线后鱼回游确实产生余线"), FishReturns.bSucceeded && FishReturns.SlackLineLengthCentimeters > 0.0);
	TestFalse(TEXT("鱼靠近产生余线不能冒充线杯重新有线"), FishReturns.bSlackRecoveryActive);
	TestEqual(TEXT("满线且无负载时既不回体也不凭空收费"), FishReturns.CatStaminaDrain, 0.0);
	TestEqual(TEXT("鱼回游不能自动收回已放线"), FishReturns.LineLengthCentimeters, Config.MaximumLineLengthCentimeters);

	State.LineLengthCentimeters -= 50.0;
	const auto ReeledShorter = FCatFishingFightSimulator::Step(Config, State, Constraint, -FVector::ForwardVector);
	TestTrue(TEXT("实际收短已放线后持续右键重新恢复体力"), ReeledShorter.bSucceeded
		&& ReeledShorter.bSlackRecoveryActive && ReeledShorter.CatStaminaDrain < 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingLoadEffortTuningTest,
	"Catfishing.Unit.Fishing.Effort.CatLoadPricingAndFishDistancePricingRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingLoadEffortTuningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	const FCatFightRodConstraintInput Constraint = MakeCombinedEffortConstraint();
	Config.CatLoadStaminaMultiplier = 0.0;
	Config.FishStaminaPerUnfulfilledMeter = 0.0;
	const auto Zero = Step(Config, State, Constraint);
	Config.CatLoadStaminaMultiplier = 1.0;
	Config.FishStaminaPerUnfulfilledMeter = 1.0;
	const auto One = Step(Config, State, Constraint);
	Config.CatLoadStaminaMultiplier = 2.0;
	Config.FishStaminaPerUnfulfilledMeter = 2.0;
	const auto Two = Step(Config, State, Constraint);
	TestTrue(TEXT("三种负载倍率都可求解"), Zero.bSucceeded && One.bSucceeded && Two.bSucceeded);
	TestTrue(TEXT("零负载倍率仍保留猫的基础努力费用"), Zero.CatStaminaDrain > 0.0);
	TestEqual(TEXT("零鱼每米费率关闭鱼耗体"), Zero.FishStaminaDrain, 0.0);
	TestTrue(TEXT("猫负载倍率依次提高猫耗体"),
		Two.CatStaminaDrain > One.CatStaminaDrain && One.CatStaminaDrain > Zero.CatStaminaDrain);
	TestTrue(TEXT("鱼每米费率依次提高鱼耗体"),
		Two.FishStaminaDrain > One.FishStaminaDrain && One.FishStaminaDrain > Zero.FishStaminaDrain);
	TestEqual(TEXT("鱼每米费率翻倍时费用翻倍且没有基础偏移"),
		Two.FishStaminaDrain, One.FishStaminaDrain * 2.0, 1e-6);
	TestEqual(TEXT("调价不改变鱼的意图缺失事实"),
		Zero.FishUnfulfilledDistanceCentimeters, Two.FishUnfulfilledDistanceCentimeters, 1e-6);
	TestEqual(TEXT("调价不改变猫的物理负载事实"), Zero.CatNormalizedEffortLoad, Two.CatNormalizedEffortLoad, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedPrimaryEffortOwnershipTest,
	"Catfishing.Unit.Fishing.Effort.ExhaustedPrimaryDirectsTeamPoweredRotationWithSharedCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedPrimaryEffortOwnershipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 50.0;
	Config.HelperMassKilograms = 10.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.CatStamina = 0.0;
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();

	// 这里验证 Runner 所需的纯模型连接契约；真实多人 ASC 分摊仍由运行验收覆盖。
	FCatFishingRodRotationInput RotationInput;
	RotationInput.CatTorqueCapacity = Config.GetCombinedCatStrength();
	RotationInput.MaximumFishTorque = 10.0;
	RotationInput.CurrentAim = FRotator(0.0, 30.0, 0.0);
	RotationInput.RequestedAim = FRotator(0.0, 90.0, 0.0);
	RotationInput.PullAxis = FVector::ForwardVector;
	RotationInput.DeltaSeconds = Config.FixedStepSeconds;
	const auto Rotation = FCatFishingRodResistanceModel::StepRotation(RotationInput);
	TestTrue(TEXT("主位力竭后仍可指挥队友力量转杆"), Rotation.bSucceeded);
	TestTrue(TEXT("转杆由有力队友提供真实主动努力"), Rotation.CatExertionSquaredSeconds > 0.0 && Rotation.CatPositiveWorkRadians > 0.0);
	Constraint.CatRodExertionSquaredSeconds = Rotation.CatExertionSquaredSeconds;
	Constraint.CatRodPositiveWorkRadians = Rotation.CatPositiveWorkRadians;
	const auto Result = Step(Config, State, Constraint);
	TestTrue(TEXT("主位力竭但辅助仍有力时步骤有效"), Result.bSucceeded);
	TestTrue(TEXT("辅助合力仍可参与收线"), Result.RequestedReelDistanceCentimeters > 0.0);
	TestTrue(TEXT("辅助支持的收线仍产生共同费用"), Result.GetSharedCatStaminaDrain() > 0.0);
	TestTrue(TEXT("主位力竭时辅助的有效力量仍能给鱼造成对抗耗体"),
		Result.FishUnfulfilledDistanceCentimeters > 0.0 && Result.FishStaminaDrain > 0.0);
	TestTrue(TEXT("实际转杆做功产生共同支付费用，不是免费借力"), Result.CatRodStaminaDrain > 0.0);
	TestEqual(TEXT("无身体输入时收线转杆支撑全部属于共同负担"),
		Result.CatStaminaDrain, Result.GetSharedCatStaminaDrain(), 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingEffortFiniteTotalsTest,
	"Catfishing.Unit.Fishing.Effort.NonFiniteIndividualAndCombinedChargesFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingEffortFiniteTotalsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	const double LargeFinite = std::numeric_limits<double>::max() * 0.6;
	FCatFightFishIntentInput Work;
	Work.IntendedDisplacementCentimeters = FVector(100.0, 0.0, 0.0);
	Work.StaminaPerUnfulfilledMeter = LargeFinite;
	FCatFightFishIntentResult WorkResult;
	TestTrue(TEXT("一米缺失对应的有限大费用仍可计算"), FCatFishingFightWorkModel::ComputeFishIntentDrain(Work, WorkResult));
	TestTrue(TEXT("单项结果确实有限"), FMath::IsFinite(WorkResult.StaminaDrain));
	Work.IntendedDisplacementCentimeters.X = 200.0;
	TestFalse(TEXT("每米费率与缺失米数乘积溢出时拒绝结算"),
		FCatFishingFightWorkModel::ComputeFishIntentDrain(Work, WorkResult));
	TestEqual(TEXT("拒绝费用溢出后不泄漏上次结果"), WorkResult.StaminaDrain, 0.0);

	FCatFightSimulationConfig Config = MakeConfig();
	Config.StrengthPerKilogram = 1.0;
	Config.CatStaminaCostPerStrengthCentimeter = LargeFinite;
	Config.CatRodStaminaCostPerStrengthRadian = LargeFinite;
	Config.CatUnloadedWorkMultiplier = 1.0;
	Config.CatLoadStaminaMultiplier = 0.0;
	Config.ReelSpeedCentimetersPerSecond = 10.0;
	Config.FishFullEffortSpeedCentimetersPerSecond = 0.1;
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(-10.0, 0.0, 0.0);
	Constraint.CarrierVelocityCentimetersPerSecond = Constraint.CarrierDesiredVelocityCentimetersPerSecond;
	Constraint.CatRodExertionSquaredSeconds = 0.1;
	Constraint.CatRodPositiveWorkRadians = 1.0;
	TestTrue(TEXT("求和溢出用例的各配置字段均有限合法"), Config.IsValid());
	const auto Overflow = Step(Config, MakeState(ECatFightCatAction::Pull), Constraint);
	TestFalse(TEXT("三个单项均有限但合计溢出时拒绝整个步骤"), Overflow.bSucceeded);
	TestTrue(TEXT("拒绝后的默认结果不会泄漏无穷总费用"), FMath::IsFinite(Overflow.CatStaminaDrain));

	FCatFightSimulationConfig StrengthOverflow = MakeConfig();
	StrengthOverflow.PrimaryOperatorCatStrength = LargeFinite;
	StrengthOverflow.SecondCatStrength = LargeFinite;
	TestFalse(TEXT("主辅力量各自有限但合力溢出时拒绝配置"), StrengthOverflow.IsValid());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSmallActionsPreserveHoldFloorTest,
	"Catfishing.Unit.Fishing.Effort.SmallActionsCannotEraseContinuousHoldCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSmallActionsPreserveHoldFloorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	// 三种微小操作均保持满张力，比较同一负载下的连续费用。
	const FCatFightSimulationState State = MakeState();
	const auto Baseline = Step(Config, State, MakeHeldConstraint());
	TestTrue(TEXT("无操作时存在合法的持续保持费用"), Baseline.bSucceeded && Baseline.CatHoldStaminaDrain > 0.0);
	for (const double SmallDistance : {0.0001, 0.001, 0.01})
	{
		FCatFightRodConstraintInput Rod = MakeHeldConstraint();
		Rod.CatRodExertionSquaredSeconds = SmallDistance;
		Rod.CatRodPositiveWorkRadians = SmallDistance;
		const auto SmallRod = Step(Config, State, Rod);
		TestTrue(TEXT("微小转杆产生自身费用并保留必要的持竿差额"),
			SmallRod.bSucceeded && SmallRod.CatRodStaminaDrain > 0.0 && SmallRod.CatHoldStaminaDrain > 0.0);
		TestEqual(TEXT("微小转杆完整保留共享支撑"),
			SmallRod.CatHoldStaminaDrain, Baseline.CatHoldStaminaDrain, 1e-6);

		FCatFightRodConstraintInput Movement = MakeHeldConstraint();
		Movement.CarrierDesiredVelocityCentimetersPerSecond = FVector(-SmallDistance / Config.FixedStepSeconds, 0.0, 0.0);
		Movement.CarrierVelocityCentimetersPerSecond = Movement.CarrierDesiredVelocityCentimetersPerSecond;
		const auto SmallMovement = Step(Config, State, Movement);
		TestTrue(TEXT("微小后退产生自身费用并保留必要的持竿差额"),
			SmallMovement.bSucceeded && SmallMovement.CatMovementStaminaDrain > 0.0 && SmallMovement.CatHoldStaminaDrain > 0.0);
		TestEqual(TEXT("微小后退完整保留共享支撑"),
			SmallMovement.CatHoldStaminaDrain, Baseline.CatHoldStaminaDrain, 1e-6);

		FCatFightSimulationConfig SlowReelConfig = Config;
		SlowReelConfig.ReelSpeedCentimetersPerSecond = SmallDistance / Config.FixedStepSeconds;
		const auto SmallReel = Step(SlowReelConfig, MakeState(ECatFightCatAction::Pull), MakeHeldConstraint());
		TestTrue(TEXT("微小收线产生自身费用并保留必要的持竿差额"),
			SmallReel.bSucceeded && SmallReel.CatReelStaminaDrain > 0.0 && SmallReel.CatHoldStaminaDrain > 0.0);
		TestTrue(TEXT("微小收线增加负载也不能减少原共享支撑"), SmallReel.CatHoldStaminaDrain + 1e-6 >= Baseline.CatHoldStaminaDrain);
	}

	FCatFightRodConstraintInput StrongRod = MakeHeldConstraint();
	StrongRod.CatRodExertionSquaredSeconds = Config.FixedStepSeconds;
	StrongRod.CatRodPositiveWorkRadians = 0.5;
	const auto AboveFloor = Step(Config, State, StrongRod);
	TestTrue(TEXT("足够大的主动操作费用可以超过保持基线"),
		AboveFloor.bSucceeded && AboveFloor.CatStaminaDrain > Baseline.CatStaminaDrain);
	TestEqual(TEXT("大幅转杆只收超出共享支撑的差额"), AboveFloor.CatRodSupportStaminaDrain,
		Config.CatSupportStaminaPerSecond * Config.FixedStepSeconds - Baseline.CatHoldStaminaDrain, 1e-6);
	Config.CatRodStaminaMultiplier *= 2.0;
	const auto HigherRodPrice = Step(Config, State, StrongRod);
	TestTrue(TEXT("超过保持基线后独立转杆倍率仍提高总费用"),
		HigherRodPrice.bSucceeded && HigherRodPrice.CatStaminaDrain > AboveFloor.CatStaminaDrain);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPersonalEffortCoverageBudgetTest,
	"Catfishing.Unit.Fishing.Effort.TeamOperationsCannotEraseSharedHoldCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPersonalEffortCoverageBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 50.0;
	Config.HelperMassKilograms = 10.0;
	FCatFightSimulationState State = MakeState();
	State.CatStamina = 0.0;
	FCatFightRodConstraintInput Constraint = MakeCombinedEffortConstraint();
	const auto ExhaustedBaseline = Step(Config, State, MakeHeldConstraint());
	const auto ExhaustedActive = Step(Config, State, Constraint);
	TestTrue(TEXT("主位力竭且辅助发力时两种步骤都有效"),
		ExhaustedBaseline.bSucceeded && ExhaustedActive.bSucceeded);
	TestTrue(TEXT("辅助承担的完整保持费用存在"), ExhaustedBaseline.GetSharedCatStaminaDrain() > 0.0);
	TestTrue(TEXT("载体运动与转杆的团队努力不因主位个人耗尽被删掉"),
		ExhaustedActive.CatMovementIntentCentimeters > 0.0 && ExhaustedActive.CatRodExertionSquaredSeconds > 0.0);
	TestEqual(TEXT("额外团队操作不能抵扣已有沿线保持费用"),
		ExhaustedActive.CatHoldStaminaDrain, ExhaustedBaseline.CatHoldStaminaDrain, 1e-6);
	TestTrue(TEXT("转杆支撑与做功完整计入共同负担"),
		ExhaustedActive.GetSharedCatStaminaDrain() >= ExhaustedBaseline.GetSharedCatStaminaDrain());
	TestEqual(TEXT("未完成的身体意图不再伪造额外端点误差"), ExhaustedActive.ConstraintErrorCentimeters, ExhaustedBaseline.ConstraintErrorCentimeters, 1e-6);

	Config.PrimaryOperatorCatStrength = 0.001;
	State.CatStamina = 0.001;
	const auto NearlyExhaustedBaseline = Step(Config, State, MakeHeldConstraint());
	const auto NearlyExhaustedActive = Step(Config, State, Constraint);
	TestTrue(TEXT("主位接近力竭时两种步骤都有效"),
		NearlyExhaustedBaseline.bSucceeded && NearlyExhaustedActive.bSucceeded);
	TestTrue(TEXT("该用例个人原始费用确实超过当下支付能力"),
		NearlyExhaustedActive.CatMovementStaminaDrain > State.CatStamina);
	TestEqual(TEXT("个人费用完全不抵扣沿线支撑"),
		NearlyExhaustedBaseline.CatHoldStaminaDrain - NearlyExhaustedActive.CatHoldStaminaDrain,
		0.0, 1e-6);
	TestTrue(TEXT("不可支付的个人费用不能免除其余保持"), NearlyExhaustedActive.GetSharedCatStaminaDrain() > 0.0);
	TestEqual(TEXT("个人请求费用与剩余共享费用仍可完整诊断"), NearlyExhaustedActive.CatStaminaDrain,
		NearlyExhaustedActive.CatMovementStaminaDrain + NearlyExhaustedActive.GetSharedCatStaminaDrain(), 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFreeSwimmingIntentProgressTest,
	"Catfishing.Unit.Fishing.Effort.FreeSwimmingCostsOnlyForUnfulfilledIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFreeSwimmingIntentProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	for (const double FishStrength : {0.4, 80.0})
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStrength = FishStrength;
		for (const auto Motion : {ECatFishMotionIntent::CalmOrInward, ECatFishMotionIntent::StrugglingOutward})
		{
			FCatFightSimulationState State = MakeState();
			State.MotionIntent = Motion;
			State.LineLengthCentimeters = 800.0;
			for (const FVector& Direction : {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector})
			{
				const auto Result = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), Direction);
				TestTrue(TEXT("强弱鱼各阶段自由游动均可继续"), Result.bSucceeded);
				TestTrue(TEXT("起步仍保留真实游动"),
					!Result.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 0.01));
				TestEqual(TEXT("余线内自由游动没有鱼线张力"), Result.LineTensionNewtons, 0.0);
				TestTrue(TEXT("各主动方向起步尚未完成意图时都按缺失距离耗体"),
					Result.FishUnfulfilledDistanceCentimeters > 0.0 && Result.FishStaminaDrain > 0.0);

				auto CompletedState = State;
				CompletedState.FishStamina = 0.1;
				CompletedState.FishVelocityCentimetersPerSecond = Direction
					* (Config.FishFullEffortSpeedCentimetersPerSecond + 1.0);
				const auto Completed = FCatFishingFightSimulator::Step(Config, CompletedState, MakeHeldConstraint(), Direction);
				TestTrue(TEXT("实际进度已覆盖意图时自由游动仍可继续"), Completed.bSucceeded
					&& Completed.FishActualIntentProgressCentimeters >= Completed.FishIntendedDistanceCentimeters);
				TestEqual(TEXT("完成意图的自由游动没有未完成距离费用"), Completed.FishStaminaDrain, 0.0);
				TestEqual(TEXT("没有意图缺失时低体力鱼不因吸附阈值力竭"), Completed.Outcome, ECatFightStepOutcome::None);
			}
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSteadyDiagonalStaminaTailTest,
	"Catfishing.Unit.Fishing.Effort.SteadyDiagonalProgressPreservesPositiveStaminaTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSteadyDiagonalStaminaTailTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	auto Config = MakeConfig();
	Config.FixedStepSeconds = 0.05;
	Config.FishStrength = 42.1661;
	Config.FishMassKilograms = 4.21661;
	Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
	for (const FVector& RodPosition : {FVector::ZeroVector, FVector(2500.0, 1000.0, 0.0),
		FVector(1505.16, 22637.54, 0.0), FVector(9500.0, 20000.0, 0.0)})
	{
		for (const FVector& Direction : {FVector(0.8, 0.6, 0.0), FVector(0.6, 0.8, 0.0),
			FVector(1.0, 1.0, 0.0).GetSafeNormal()})
		{
			auto State = MakeState(ECatFightCatAction::None);
			State.FishStamina = 0.1;
			State.FishEffortRatio = 0.869865;
			State.FishWorldPosition = RodPosition + FVector(500.0, 0.0, 0.0);
			State.LineLengthCentimeters = 800.0;
			State.FishVelocityCentimetersPerSecond = Direction
				* (Config.FishFullEffortSpeedCentimetersPerSecond * State.FishEffortRatio);
			auto Rod = MakeHeldConstraint();
			Rod.RodTipWorldPosition = RodPosition;
			for (int32 Index = 0; Index < 20; ++Index)
			{
				const auto Result = FCatFishingFightSimulator::Step(Config, State, Rod, Direction);
				if (!TestTrue(TEXT("不同世界坐标的稳速斜向自由游动有效"), Result.bSucceeded)) return false;
				TestTrue(TEXT("余线自由游动没有借用右键恢复豁免"),
					Result.LineTensionNewtons == 0.0 && !Result.bSlackRecoveryActive);
				TestEqual(TEXT("世界位置相减的舍入误差不构成未完成意图"), Result.FishUnfulfilledDistanceCentimeters, 0.0);
				TestEqual(TEXT("稳速自由游动不以极小正费用吸干体力尾数"), Result.FishStaminaDrain, 0.0);
				TestEqual(TEXT("低于吸附阈值的正体力仍不误判力竭"), Result.Outcome, ECatFightStepOutcome::None);
				State.FishWorldPosition = Result.ProposedFishWorldPosition;
				State.FishVelocityCentimetersPerSecond = Result.ResolvedFishVelocityCentimetersPerSecond;
				State.FishStamina -= Result.FishStaminaDrain;
			}
			TestEqual(TEXT("连续一秒后仍完整保留原正体力尾数"), State.FishStamina, 0.1);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPassiveDragCannotExhaustFishTest,
	"Catfishing.Unit.Fishing.Effort.ExhaustedCatAndPassiveRodCannotDrainFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPassiveDragCannotExhaustFishTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.CatStamina = 0.0;
	State.FishStamina = 0.1;
	FCatFishingRodRotationInput RotationInput;
	RotationInput.CurrentAim.Yaw = 30.0;
	RotationInput.RequestedAim.Yaw = 90.0;
	RotationInput.CatTorqueCapacity = 0.0;
	RotationInput.MaximumFishTorque = 100.0;
	RotationInput.PreviousSmoothedFishPullStrengthMeters = FVector(100.0, 0.0, 0.0);
	RotationInput.DeltaSeconds = Config.FixedStepSeconds;
	const auto Rotation = FCatFishingRodResistanceModel::StepRotation(RotationInput);
	TestTrue(TEXT("零猫转矩时鱼仍能被动拉转鱼竿"),
		Rotation.bSucceeded && Rotation.ActualAim.Yaw < RotationInput.CurrentAim.Yaw);
	TestEqual(TEXT("被动转杆没有猫主动转杆意图"), Rotation.CatExertionSquaredSeconds, 0.0);
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CarrierVelocityCentimetersPerSecond = FVector(10.0, 0.0, 0.0);
	Constraint.RodTipVelocityCentimetersPerSecond = Constraint.CarrierVelocityCentimetersPerSecond
		+ (Rotation.ActualAim.Vector() - RotationInput.CurrentAim.Vector()) * 200.0 / Config.FixedStepSeconds;
	Constraint.CatRodExertionSquaredSeconds = Rotation.CatExertionSquaredSeconds;
	Constraint.CatRodPositiveWorkRadians = Rotation.CatPositiveWorkRadians;
	const auto Result = Step(Config, State, Constraint);
	TestTrue(TEXT("猫力竭后仍能求解鱼拉人和鱼线约束"), Result.bSucceeded);
	TestTrue(TEXT("本例鱼线张紧且鱼能被动拉动猫"),
		Result.NormalizedTension > 0.0 && Result.CarrierTargetPullSpeedCentimetersPerSecond > 0.0);
	TestTrue(TEXT("零猫合力进入明确的强拖免耗分支"), Result.bExhaustedCatEscape && Result.CombinedCatStrength == 0.0);
	TestEqual(TEXT("猫无力时鱼自身继续游动不扣体"), Result.FishStaminaDrain, 0.0);
	TestEqual(TEXT("被动拖动不能触发低体力吸附力竭"), Result.Outcome, ECatFightStepOutcome::None);
	TestEqual(TEXT("零体力猫的旧收线意图不能拉动活鱼"), Result.RequestedReelDistanceCentimeters, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingZeroPriceCannotSnapFishStaminaTest,
	"Catfishing.Unit.Fishing.Effort.ZeroFishPriceCannotTriggerExhaustionSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingZeroPriceCannotSnapFishStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	State.FishStamina = 0.1;
	{
		FCatFightSimulationConfig Config = MakeConfig();
		Config.FishStaminaPerUnfulfilledMeter = 0.0;
		const auto Result = Step(Config, State, MakeCombinedEffortConstraint());
		TestTrue(TEXT("零鱼价格时受载步骤仍有效"), Result.bSucceeded && Result.FishUnfulfilledDistanceCentimeters > 0.0);
		TestEqual(TEXT("鱼每米费率为零时原始费用为零"), Result.FishUncappedStaminaDrain, 0.0);
		TestEqual(TEXT("零费用不会被吸附阈值变成全额扣费"), Result.FishStaminaDrain, 0.0);
		TestEqual(TEXT("零费用不触发鱼力竭结果"), Result.Outcome, ECatFightStepOutcome::None);
		TestTrue(TEXT("鱼价格关闭不影响猫的三个操作耗体"),
			Result.CatMovementStaminaDrain > 0.0 && Result.CatReelStaminaDrain > 0.0 && Result.CatRodStaminaDrain > 0.0);
	}
	FCatFightSimulationConfig Priced = MakeConfig();
	// 独立米价明确选为0.001点/m，仅用于验证真实小额扣费之后的阈值吸附。
	Priced.FishStaminaPerUnfulfilledMeter = 0.001;
	const auto Charged = Step(Priced, State, MakeHeldConstraint());
	TestTrue(TEXT("正负载产生真实且小于剩余量的原始费用"),
		Charged.bSucceeded && Charged.FishUncappedStaminaDrain > 0.0 && Charged.FishUncappedStaminaDrain < State.FishStamina);
	TestEqual(TEXT("本步真实正扣费后仍可按阈值吸附剩余体力"), Charged.FishStaminaDrain, State.FishStamina, 1e-9);
	TestEqual(TEXT("有效阈值吸附进入力竭结果"), Charged.Outcome, ECatFightStepOutcome::FishExhausted);
	{
		// 1e-4 cm 虽然很小，仍比纯舍入容差大；0.001点/m 对应的真实小金额不能被过滤。
		auto TinyGapConfig = Priced;
		TinyGapConfig.FishFullEffortSpeedCentimetersPerSecond = 10.0;
		auto TinyGapState = MakeState(ECatFightCatAction::None);
		TinyGapState.FishStamina = 0.1;
		TinyGapState.LineLengthCentimeters = 800.0;
		const auto Rod = MakeHeldConstraint();
		auto TinyGap = Step(TinyGapConfig, TinyGapState, Rod);
		TinyGap.ProposedFishWorldPosition = TinyGapState.FishWorldPosition
			+ FVector::ForwardVector * (TinyGap.FishIntendedDistanceCentimeters - 1e-4);
		if (!TestTrue(TEXT("最终仍差1e-4厘米的落点可以重算"),
			FCatFishingFightSimulator::FinalizeResolvedStep(TinyGapConfig, TinyGapState, Rod, TinyGap))) return false;
		TestEqual(TEXT("数值容差不抹去真实的小段缺失"), TinyGap.FishUnfulfilledDistanceCentimeters, 1e-4, 1e-10);
		TestEqual(TEXT("极小正金额仍按0.001点每米收费"), TinyGap.FishUncappedStaminaDrain, 1e-9, 1e-14);
		TestEqual(TEXT("真实小额扣费仍保留原有尾数吸附规则"), TinyGap.FishStaminaDrain, TinyGapState.FishStamina);
		TestEqual(TEXT("真实小额扣费仍能进入力竭"), TinyGap.Outcome, ECatFightStepOutcome::FishExhausted);
	}
	State.FishStamina = 1e-9;
	State.CatAction = ECatFightCatAction::Slack;
	const auto FreeTail = Step(Priced, State, MakeHeldConstraint());
	TestTrue(TEXT("极小正体力尾数的鱼仍可自由游动"), FreeTail.bSucceeded);
	TestEqual(TEXT("免费游动不会消耗小于近零容差的正尾数"), FreeTail.FishStaminaDrain, 0.0);
	TestTrue(TEXT("免费游动后仍保留严格为正的体力"), State.FishStamina - FreeTail.FishStaminaDrain > 0.0);
	TestEqual(TEXT("全局终局检查不会把近零正尾数当作力竭"), FreeTail.Outcome, ECatFightStepOutcome::None);
	State.CatAction = ECatFightCatAction::Pull;
	const auto ChargedTail = Step(Priced, State, MakeHeldConstraint());
	TestTrue(TEXT("极小正尾数遇到真实负载仍可正常结算"),
		ChargedTail.bSucceeded && ChargedTail.FishUncappedStaminaDrain > 0.0);
	TestEqual(TEXT("真实对抗恰好扣完极小正尾数"), ChargedTail.FishStaminaDrain, State.FishStamina, 1e-15);
	TestEqual(TEXT("扣完极小正尾数后进入力竭"), ChargedTail.Outcome, ECatFightStepOutcome::FishExhausted);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
