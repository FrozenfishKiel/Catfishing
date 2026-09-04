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
		Config.RodStrength = 1000.0;
		Config.CatStaminaMaximum = 1000.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishCalmSpeedCentimetersPerSecond = 25.0;
		Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
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
		Constraint.CatRodIntentArcCentimeters = 4.0;
		Constraint.CatRodActualArcCentimeters = 2.0;
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
	Constraint.CatRodIntentArcCentimeters = 4.0;
	Constraint.CatRodActualArcCentimeters = 2.0;
	Constraint.RodTipVelocityCentimetersPerSecond = FVector(-200.0, 0.0, 0.0);
	const auto HoldBaseline = Step(MakeConfig(), MakeState(), MakeHeldConstraint());
	const auto Result = Step(MakeConfig(), MakeState(), Constraint);
	TestTrue(TEXT("主动转杆步骤有效"), Result.bSucceeded);
	TestTrue(TEXT("转杆拥有独立正耗体"), Result.CatRodStaminaDrain > 0.0);
	TestEqual(TEXT("转杆不会冒充身体移动"), Result.CatMovementStaminaDrain, 0.0);
	TestEqual(TEXT("转杆时持竿只补主动费用尚未覆盖的差额"), Result.CatHoldStaminaDrain,
		FMath::Max(0.0, HoldBaseline.CatStaminaDrain - Result.CatRodStaminaDrain), 1e-6);
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
		Result.FishNormalizedEffortLoad > 0.0 && Result.FishStaminaDrain > 0.0);
	TestEqual(TEXT("本例主动费用覆盖保持基线后不重复收取持竿"), Result.CatHoldStaminaDrain, 0.0);
	TestEqual(TEXT("总耗体等于三种操作分项之和"), Result.CatStaminaDrain,
		Result.CatMovementStaminaDrain + Result.CatReelStaminaDrain + Result.CatRodStaminaDrain, 1e-6);
	TestEqual(TEXT("主位与共同分担的耗体之和守恒"), Result.CatStaminaDrain,
		Result.GetPrimaryCatStaminaDrain() + Result.GetSharedCatStaminaDrain(), 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFishEffortLoadTest,
	"Catfishing.Unit.Fishing.Effort.FishPaysForResistanceAndItsOwnRelativeLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFishEffortLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig LowLoadConfig = MakeConfig();
	LowLoadConfig.PrimaryOperatorCatStrength = 20.0;
	LowLoadConfig.FishStrength = 80.0;
	LowLoadConfig.IsometricEffortMultiplier = 1.0;
	FCatFightSimulationConfig HighLoadConfig = LowLoadConfig;
	HighLoadConfig.PrimaryOperatorCatStrength = 60.0;
	const auto LowLoad = Step(LowLoadConfig, MakeState(), MakeHeldConstraint());
	const auto HighLoad = Step(HighLoadConfig, MakeState(), MakeHeldConstraint());
	TestTrue(TEXT("不同相对负载步骤都有效"), LowLoad.bSucceeded && HighLoad.bSucceeded);
	TestEqual(TEXT("比较保持鱼主动意图距离相同"), LowLoad.FishIntendedLineDistanceCentimeters,
		HighLoad.FishIntendedLineDistanceCentimeters, 1e-6);
	TestEqual(TEXT("比较保持几何张力相同"), LowLoad.NormalizedTension, HighLoad.NormalizedTension, 1e-6);
	TestTrue(TEXT("更强约束提高鱼自己的归一化负载"),
		HighLoad.FishNormalizedEffortLoad > LowLoad.FishNormalizedEffortLoad);
	TestTrue(TEXT("相同意图在更高相对负载下鱼更耗体"), HighLoad.FishStaminaDrain > LowLoad.FishStaminaDrain);

	FCatFightSimulationConfig DragConfig = MakeConfig();
	DragConfig.PrimaryOperatorCatStrength = 200.0;
	const FCatFightSimulationState DragState = MakeState(ECatFightCatAction::Pull);
	const auto Dragged = Step(DragConfig, DragState, MakeHeldConstraint());
	TestTrue(TEXT("强猫收线步骤有效"), Dragged.bSucceeded);
	TestTrue(TEXT("鱼被实际拖向主动游动的反方向"), Dragged.ProposedFishWorldPosition.X < DragState.FishWorldPosition.X);
	TestEqual(TEXT("反向被拖不能算成鱼主动完成距离"), Dragged.FishActualLineDistanceCentimeters, 0.0);
	TestTrue(TEXT("僵持倍率为一时反抗被拖仍消耗鱼体力"), Dragged.FishStaminaDrain > 0.0);
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
	FishPricing.FishStaminaCostPerStrengthCentimeter *= 2.0;
	FishPricing.FishLoadStaminaMultiplier = 2.0;
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
	TestEqual(TEXT("完整放线后鱼没有对抗负载"), Released.FishNormalizedEffortLoad, 0.0);
	TestEqual(TEXT("完整放线后鱼自身游动不扣体力"), Released.FishStaminaDrain, 0.0);
	TestEqual(TEXT("无主动操作时放线不收取分项费用"),
		Released.GetPrimaryCatStaminaDrain() + Released.GetSharedCatStaminaDrain(), 0.0);
	TestTrue(TEXT("解除约束且无主动努力时猫恢复体力"), Released.CatStaminaDrain < 0.0);
	TestTrue(TEXT("放线恢复不突破体力上限"),
		MakeState().CatStamina - Released.CatStaminaDrain <= Config.CatStaminaMaximum);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingIsometricEffortTuningTest,
	"Catfishing.Unit.Fishing.Effort.BlockedEffortHonorsZeroOneAndTwoIsometricMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingIsometricEffortTuningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CatRodIntentArcCentimeters = 4.0;
	Constraint.CatRodActualArcCentimeters = 0.0;
	Config.IsometricEffortMultiplier = 0.0;
	const auto Zero = Step(Config, MakeState(), Constraint);
	Config.IsometricEffortMultiplier = 1.0;
	const auto One = Step(Config, MakeState(), Constraint);
	Config.IsometricEffortMultiplier = 2.0;
	const auto Two = Step(Config, MakeState(), Constraint);
	TestTrue(TEXT("三种僵持倍率都可求解"), Zero.bSucceeded && One.bSucceeded && Two.bSucceeded);
	TestEqual(TEXT("零僵持倍率可关闭完全受阻的转杆耗体"), Zero.CatRodStaminaDrain, 0.0);
	TestTrue(TEXT("一倍僵持倍率使受阻转杆耗体"), One.CatRodStaminaDrain > 0.0);
	TestTrue(TEXT("更高僵持倍率提高受阻转杆耗体"), Two.CatRodStaminaDrain > One.CatRodStaminaDrain);
	TestTrue(TEXT("鱼受阻努力也受同一僵持规则控制"),
		Two.FishStaminaDrain > One.FishStaminaDrain && One.FishStaminaDrain > Zero.FishStaminaDrain);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRightButtonRecoveryTest,
	"Catfishing.Unit.Fishing.Effort.RightButtonWaivesAllCostsAndRecoversThroughMovementRodAndFullLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRightButtonRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	auto Config = MakeConfig();
	Config.SlackStaminaRegenPerSecond = 2.75;
	Config.TensionResponseRangeCentimeters = 1.0;
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
				State.FishStamina = 0.1; // 低于力竭尾数阈值，也不能因右键期间的张力被扣空。
				State.MotionIntent = Motion;
				State.LineLengthCentimeters = bAtLineLimit ? Config.MaximumLineLengthCentimeters : 800.0;
				State.FishWorldPosition.X = bAtLineLimit ? Config.MaximumLineLengthCentimeters : 500.0;
				const auto Result = Step(Config, State, Constraint);
				if (!TestTrue(TEXT("正常右键在各阶段、线长和体力边界均有效"), Result.bSucceeded && Result.bSlackRecoveryActive)) return false;
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
				if (bAtLineLimit)
				{
					TestTrue(TEXT("满线仍保留实际对抗张力和鱼负载"), Result.NormalizedTension > 0.0 && Result.FishNormalizedEffortLoad > 0.0);
					TestEqual(TEXT("回体不会绕过物理最大线长"), Result.LineLengthCentimeters, Config.MaximumLineLengthCentimeters);
				}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingLoadEffortTuningTest,
	"Catfishing.Unit.Fishing.Effort.LoadPricingHonorsZeroOneAndTwoMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingLoadEffortTuningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	const FCatFightSimulationState State = MakeState(ECatFightCatAction::Pull);
	const FCatFightRodConstraintInput Constraint = MakeCombinedEffortConstraint();
	Config.CatLoadStaminaMultiplier = 0.0;
	Config.FishLoadStaminaMultiplier = 0.0;
	const auto Zero = Step(Config, State, Constraint);
	Config.CatLoadStaminaMultiplier = 1.0;
	Config.FishLoadStaminaMultiplier = 1.0;
	const auto One = Step(Config, State, Constraint);
	Config.CatLoadStaminaMultiplier = 2.0;
	Config.FishLoadStaminaMultiplier = 2.0;
	const auto Two = Step(Config, State, Constraint);
	TestTrue(TEXT("三种负载倍率都可求解"), Zero.bSucceeded && One.bSucceeded && Two.bSucceeded);
	TestTrue(TEXT("零负载倍率仍保留猫的基础努力费用"), Zero.CatStaminaDrain > 0.0);
	TestEqual(TEXT("鱼没有基础游动费用，零负载倍率关闭鱼耗体"), Zero.FishStaminaDrain, 0.0);
	TestTrue(TEXT("猫负载倍率依次提高猫耗体"),
		Two.CatStaminaDrain > One.CatStaminaDrain && One.CatStaminaDrain > Zero.CatStaminaDrain);
	TestTrue(TEXT("鱼负载倍率依次提高鱼耗体"),
		Two.FishStaminaDrain > One.FishStaminaDrain && One.FishStaminaDrain > Zero.FishStaminaDrain);
	TestEqual(TEXT("鱼对抗倍率翻倍时费用翻倍且没有基础偏移"),
		Two.FishStaminaDrain, One.FishStaminaDrain * 2.0, 1e-6);
	TestEqual(TEXT("调价不改变鱼的物理负载事实"), Zero.FishNormalizedEffortLoad, Two.FishNormalizedEffortLoad, 1e-6);
	TestEqual(TEXT("调价不改变猫的物理负载事实"), Zero.CatNormalizedEffortLoad, Two.CatNormalizedEffortLoad, 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedPrimaryEffortOwnershipTest,
	"Catfishing.Unit.Fishing.Effort.ExhaustedPrimaryCannotBorrowFreeRotationFromHelper",
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
	RotationInput.CatTorqueCapacity = Config.PrimaryOperatorCatStrength;
	RotationInput.MaximumFishTorque = 100.0;
	RotationInput.CurrentAim = FRotator(0.0, 30.0, 0.0);
	RotationInput.RequestedAim = FRotator(0.0, 90.0, 0.0);
	RotationInput.PullAxis = FVector::ForwardVector;
	RotationInput.DeltaSeconds = Config.FixedStepSeconds;
	const auto Rotation = FCatFishingRodResistanceModel::StepRotation(RotationInput);
	TestTrue(TEXT("主位力竭后仍能求解鱼对竿的被动拖动"), Rotation.bSucceeded);
	TestEqual(TEXT("辅助位力量不能生成主位主动转杆努力"), Rotation.CatIntentArcCentimeters, 0.0);
	Constraint.CatRodIntentArcCentimeters = Rotation.CatIntentArcCentimeters;
	Constraint.CatRodActualArcCentimeters = Rotation.CatActualArcCentimeters;
	const auto Result = Step(Config, State, Constraint);
	TestTrue(TEXT("主位力竭但辅助仍有力时步骤有效"), Result.bSucceeded);
	TestTrue(TEXT("辅助合力仍可参与收线"), Result.RequestedReelDistanceCentimeters > 0.0);
	TestTrue(TEXT("辅助支持的收线仍产生共同费用"), Result.GetSharedCatStaminaDrain() > 0.0);
	TestTrue(TEXT("主位力竭时辅助的有效力量仍能给鱼造成对抗耗体"),
		Result.FishNormalizedEffortLoad > 0.0 && Result.FishStaminaDrain > 0.0);
	TestEqual(TEXT("没有主位主动转矩就没有转杆扣费"), Result.CatRodStaminaDrain, 0.0);
	TestEqual(TEXT("无身体输入时该步全部费用属于共同收线"),
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
	FCatFightWorkInput Work;
	Work.Strength = 1.0;
	Work.IntendedLineDistanceCentimeters = 1.0;
	Work.ActualLineDistanceCentimeters = 1.0;
	Work.CostPerStrengthCentimeter = LargeFinite;
	double Drain = 0.0;
	double Effort = 0.0;
	TestTrue(TEXT("单项有限的大费用仍可计算"), FCatFishingFightWorkModel::ComputeDrain(Work, Drain, Effort));
	TestTrue(TEXT("单项结果确实有限"), FMath::IsFinite(Drain));
	Work.NormalizedLoad = 1.0;
	Work.LoadStaminaMultiplier = 2.0;
	TestFalse(TEXT("负载加价导致单项溢出时拒绝结算"),
		FCatFishingFightWorkModel::ComputeDrain(Work, Drain, Effort));

	FCatFightSimulationConfig Config = MakeConfig();
	Config.StrengthPerKilogram = 1.0;
	Config.CatStaminaCostPerStrengthCentimeter = LargeFinite;
	Config.CatLoadStaminaMultiplier = 0.0;
	Config.BaseDrainMultiplier = 1.0;
	Config.StruggleDrainMultiplier = 1.0;
	Config.ReelSpeedCentimetersPerSecond = 10.0;
	Config.FishStruggleSpeedCentimetersPerSecond = 0.1;
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector(-10.0, 0.0, 0.0);
	Constraint.CarrierVelocityCentimetersPerSecond = Constraint.CarrierDesiredVelocityCentimetersPerSecond;
	Constraint.CatRodIntentArcCentimeters = 1.0;
	Constraint.CatRodActualArcCentimeters = 1.0;
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
	Config.TensionResponseRangeCentimeters = 1.0;
	const FCatFightSimulationState State = MakeState();
	const auto Baseline = Step(Config, State, MakeHeldConstraint());
	TestTrue(TEXT("无操作时存在合法的持续保持费用"), Baseline.bSucceeded && Baseline.CatHoldStaminaDrain > 0.0);
	for (const double SmallDistance : {0.0001, 0.001, 0.01})
	{
		FCatFightRodConstraintInput Rod = MakeHeldConstraint();
		Rod.CatRodIntentArcCentimeters = SmallDistance;
		Rod.CatRodActualArcCentimeters = SmallDistance;
		const auto SmallRod = Step(Config, State, Rod);
		TestTrue(TEXT("微小转杆产生自身费用并保留必要的持竿差额"),
			SmallRod.bSucceeded && SmallRod.CatRodStaminaDrain > 0.0 && SmallRod.CatHoldStaminaDrain > 0.0);
		TestEqual(TEXT("微小转杆不能使总费用跌破同负载保持基线"),
			SmallRod.CatStaminaDrain, Baseline.CatStaminaDrain, 1e-6);

		FCatFightRodConstraintInput Movement = MakeHeldConstraint();
		Movement.CarrierDesiredVelocityCentimetersPerSecond = FVector(-SmallDistance / Config.FixedStepSeconds, 0.0, 0.0);
		Movement.CarrierVelocityCentimetersPerSecond = Movement.CarrierDesiredVelocityCentimetersPerSecond;
		const auto SmallMovement = Step(Config, State, Movement);
		TestTrue(TEXT("微小后退产生自身费用并保留必要的持竿差额"),
			SmallMovement.bSucceeded && SmallMovement.CatMovementStaminaDrain > 0.0 && SmallMovement.CatHoldStaminaDrain > 0.0);
		TestEqual(TEXT("微小后退不能使总费用跌破同负载保持基线"),
			SmallMovement.CatStaminaDrain, Baseline.CatStaminaDrain, 1e-6);

		FCatFightSimulationConfig SlowReelConfig = Config;
		SlowReelConfig.ReelSpeedCentimetersPerSecond = SmallDistance / Config.FixedStepSeconds;
		const auto SmallReel = Step(SlowReelConfig, MakeState(ECatFightCatAction::Pull), MakeHeldConstraint());
		TestTrue(TEXT("微小收线产生自身费用并保留必要的持竿差额"),
			SmallReel.bSucceeded && SmallReel.CatReelStaminaDrain > 0.0 && SmallReel.CatHoldStaminaDrain > 0.0);
		TestEqual(TEXT("微小收线不能使总费用跌破同负载保持基线"),
			SmallReel.CatStaminaDrain, Baseline.CatStaminaDrain, 1e-6);
	}

	FCatFightRodConstraintInput StrongRod = MakeHeldConstraint();
	StrongRod.CatRodIntentArcCentimeters = 100.0;
	StrongRod.CatRodActualArcCentimeters = 100.0;
	const auto AboveFloor = Step(Config, State, StrongRod);
	TestTrue(TEXT("足够大的主动操作费用可以超过保持基线"),
		AboveFloor.bSucceeded && AboveFloor.CatStaminaDrain > Baseline.CatStaminaDrain);
	TestEqual(TEXT("主动费用超过基线后无需持竿补差"), AboveFloor.CatHoldStaminaDrain, 0.0);
	Config.CatRodStaminaMultiplier *= 2.0;
	const auto HigherRodPrice = Step(Config, State, StrongRod);
	TestTrue(TEXT("超过保持基线后独立转杆倍率仍提高总费用"),
		HigherRodPrice.bSucceeded && HigherRodPrice.CatStaminaDrain > AboveFloor.CatStaminaDrain);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPersonalEffortCoverageBudgetTest,
	"Catfishing.Unit.Fishing.Effort.UnpaidPrimaryEffortCannotEraseHelperHoldCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingPersonalEffortCoverageBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingEffortTest;
	FCatFightSimulationConfig Config = MakeConfig();
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 50.0;
	Config.HelperMassKilograms = 10.0;
	Config.TensionResponseRangeCentimeters = 1.0;
	FCatFightSimulationState State = MakeState();
	State.CatStamina = 0.0;
	FCatFightRodConstraintInput Constraint = MakeCombinedEffortConstraint();
	const auto ExhaustedBaseline = Step(Config, State, MakeHeldConstraint());
	const auto ExhaustedActive = Step(Config, State, Constraint);
	TestTrue(TEXT("主位力竭且辅助发力时两种步骤都有效"),
		ExhaustedBaseline.bSucceeded && ExhaustedActive.bSucceeded);
	TestTrue(TEXT("辅助承担的完整保持费用存在"), ExhaustedBaseline.GetSharedCatStaminaDrain() > 0.0);
	TestEqual(TEXT("力竭主位的后退输入不生成个人计费意图"), ExhaustedActive.CatMovementIntentCentimeters, 0.0);
	TestEqual(TEXT("力竭主位的转杆快照不生成个人计费意图"), ExhaustedActive.CatRodIntentArcCentimeters, 0.0);
	TestEqual(TEXT("力竭主位不能用无支付能力的动作减少助手保持费用"),
		ExhaustedActive.GetSharedCatStaminaDrain(), ExhaustedBaseline.GetSharedCatStaminaDrain(), 1e-6);
	TestTrue(TEXT("计费守卫保留原来的身体移动物理解算"),
		ExhaustedActive.ConstraintErrorCentimeters > ExhaustedBaseline.ConstraintErrorCentimeters);

	Config.PrimaryOperatorCatStrength = 0.001;
	State.CatStamina = 0.001;
	const auto NearlyExhaustedBaseline = Step(Config, State, MakeHeldConstraint());
	const auto NearlyExhaustedActive = Step(Config, State, Constraint);
	TestTrue(TEXT("主位接近力竭时两种步骤都有效"),
		NearlyExhaustedBaseline.bSucceeded && NearlyExhaustedActive.bSucceeded);
	TestTrue(TEXT("该用例个人原始费用确实超过当下支付能力"),
		NearlyExhaustedActive.GetPrimaryCatStaminaDrain() > State.CatStamina);
	TestEqual(TEXT("只能用主位可支付的个人体力抵扣助手保持费用"),
		NearlyExhaustedBaseline.GetSharedCatStaminaDrain() - NearlyExhaustedActive.GetSharedCatStaminaDrain(),
		State.CatStamina, 1e-6);
	TestTrue(TEXT("不可支付的个人费用不能免除其余保持"), NearlyExhaustedActive.GetSharedCatStaminaDrain() > 0.0);
	TestEqual(TEXT("个人请求费用与剩余共享费用仍可完整诊断"), NearlyExhaustedActive.CatStaminaDrain,
		NearlyExhaustedActive.GetPrimaryCatStaminaDrain() + NearlyExhaustedActive.GetSharedCatStaminaDrain(), 1e-6);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFreeSwimmingHasNoStaminaCostTest,
	"Catfishing.Unit.Fishing.Effort.FreeSwimmingNeverCostsFishStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFreeSwimmingHasNoStaminaCostTest::RunTest(const FString& Parameters)
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
			// 剩余量已经低于吸附阈值；没有真实对抗扣费时必须保留，不能靠阈值偷偷耗尽。
			State.FishStamina = 0.1;
			for (const FVector& Direction : {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector})
			{
				const auto Result = FCatFishingFightSimulator::Step(Config, State, MakeHeldConstraint(), Direction);
				TestTrue(TEXT("强弱鱼各阶段自由游动均可继续"), Result.bSucceeded);
				TestTrue(TEXT("不扣体仍保留真实游动"),
					!Result.ProposedFishWorldPosition.Equals(State.FishWorldPosition, 0.01));
				TestEqual(TEXT("余线内自由游动没有对抗负载"), Result.FishNormalizedEffortLoad, 0.0);
				TestEqual(TEXT("向外、向内和横向自由游动均不扣鱼体力"), Result.FishStaminaDrain, 0.0);
				TestEqual(TEXT("无对抗扣费时低体力鱼不因吸附阈值力竭"), Result.Outcome, ECatFightStepOutcome::None);
			}
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
	TestEqual(TEXT("被动转杆没有猫主动转杆意图"), Rotation.CatIntentArcCentimeters, 0.0);
	FCatFightRodConstraintInput Constraint = MakeHeldConstraint();
	Constraint.CarrierVelocityCentimetersPerSecond = FVector(10.0, 0.0, 0.0);
	Constraint.RodTipVelocityCentimetersPerSecond = Constraint.CarrierVelocityCentimetersPerSecond
		+ (Rotation.ActualAim.Vector() - RotationInput.CurrentAim.Vector()) * 200.0 / Config.FixedStepSeconds;
	Constraint.CatRodIntentArcCentimeters = Rotation.CatIntentArcCentimeters;
	Constraint.CatRodActualArcCentimeters = Rotation.CatActualArcCentimeters;
	const auto Result = Step(Config, State, Constraint);
	TestTrue(TEXT("猫力竭后仍能求解鱼拉人和鱼线约束"), Result.bSucceeded);
	TestTrue(TEXT("本例鱼线张紧且鱼能被动拉动猫"),
		Result.NormalizedTension > 0.0 && Result.CarrierTargetPullSpeedCentimetersPerSecond > 0.0);
	TestEqual(TEXT("张紧和被动转杆不能冒充猫对鱼的主动负载"), Result.FishNormalizedEffortLoad, 0.0);
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
	for (const bool bDisableLoadPrice : {false, true})
	{
		FCatFightSimulationConfig Config = MakeConfig();
		if (bDisableLoadPrice) Config.FishLoadStaminaMultiplier = 0.0;
		else Config.FishStaminaCostPerStrengthCentimeter = 0.0;
		const auto Result = Step(Config, State, MakeCombinedEffortConstraint());
		TestTrue(TEXT("零鱼价格时受载步骤仍有效"), Result.bSucceeded && Result.FishNormalizedEffortLoad > 0.0);
		TestEqual(TEXT("鱼系数或对抗倍率为零时原始费用为零"), Result.FishUncappedStaminaDrain, 0.0);
		TestEqual(TEXT("零费用不会被吸附阈值变成全额扣费"), Result.FishStaminaDrain, 0.0);
		TestEqual(TEXT("零费用不触发鱼力竭结果"), Result.Outcome, ECatFightStepOutcome::None);
		TestTrue(TEXT("鱼价格关闭不影响猫的三个操作耗体"),
			Result.CatMovementStaminaDrain > 0.0 && Result.CatReelStaminaDrain > 0.0 && Result.CatRodStaminaDrain > 0.0);
	}
	FCatFightSimulationConfig Priced = MakeConfig();
	Priced.FishStaminaCostPerStrengthCentimeter = 0.00001;
	const auto Charged = Step(Priced, State, MakeHeldConstraint());
	TestTrue(TEXT("正负载产生真实且小于剩余量的原始费用"),
		Charged.bSucceeded && Charged.FishUncappedStaminaDrain > 0.0 && Charged.FishUncappedStaminaDrain < State.FishStamina);
	TestEqual(TEXT("本步真实正扣费后仍可按阈值吸附剩余体力"), Charged.FishStaminaDrain, State.FishStamina, 1e-9);
	TestEqual(TEXT("有效阈值吸附进入力竭结果"), Charged.Outcome, ECatFightStepOutcome::FishExhausted);
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
