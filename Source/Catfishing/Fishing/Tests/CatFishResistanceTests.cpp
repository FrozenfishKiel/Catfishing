#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatFishResistanceTest, Log, All);

namespace CatFishResistanceTests
{
	struct FMeasurements
	{
		double Seconds = 0.0;
		double ReeledCentimeters = 0.0;
		double RequestedCentimeters = 0.0;
		double TensionNewtonSeconds = 0.0;
		double MaximumTensionNewtons = 0.0;
		double EffortSeconds = 0.0;
		double ActiveRadialNewtonSeconds = 0.0;
		double FishStaminaDrain = 0.0;
		double RemainingFishStamina = 0.0;

		double ReelSpeed() const { return ReeledCentimeters / Seconds; }
		double MeanTension() const { return TensionNewtonSeconds / Seconds; }
	};

	FCatFishSteeringConfig PreviousSteeringConfig()
	{
		FCatFishSteeringConfig Config;
		Config.LateralEffortRange = FVector2D(0.4, 0.7);
		Config.LateralOutwardBias = 0.2;
		Config.EaseOffEffortRange = FVector2D(0.15, 0.35);
		Config.EaseOffInwardBias = 0.45;
		return Config;
	}

	// 隔离行为出力的物理效果：固定竿尖并始终沿线持竿，使双方比较均使用真实的 50 N 收线能力。
	// 同一命令持续执行六秒，前两秒仍完整积分入态转向、出力和惯性；后四秒统计稳定后的收线。
	// 此处不选择或切换树状态，不替代 StateTree、岸线、角色转杆输入或表现验收。
	bool MeasureBehavior(FAutomationTestBase& Test, const TCHAR* Scenario,
		const FCatFishSteeringConfig& SteeringConfig, const ECatFishBehavior Behavior,
		const double FishStrength, const int32 Seed, FMeasurements& Out)
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.05;
		Config.PrimaryOperatorCatStrength = 50.0;
		Config.PrimaryOperatorMassKilograms = 5.0;
		Config.FishStrength = FishStrength;
		Config.FishMassKilograms = FishStrength / Config.StrengthPerKilogram;
		Config.CatStaminaMaximum = 100.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
		Config.MaximumLineLengthCentimeters = 4000.0;
		Config.RodDurability = 1000.0;

		FCatFightSimulationState State;
		State.CatStamina = State.FishStamina = 100.0;
		State.FishWorldPosition = FVector(3000.0, 0.0, 0.0);
		State.LineLengthCentimeters = 3000.0;
		State.CatAction = ECatFightCatAction::Pull;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		FCatFightRodConstraintInput Rod;
		Rod.bRodHeld = true;
		FRandomStream Random(Seed);
		FCatFishSteeringState SteeringState;
		FCatFishBehaviorFeedback Feedback;
		if (!FCatFishSteeringModel::Initialize(SteeringConfig, FVector::ForwardVector,
			Behavior, 1.0, Random, SteeringState))
		{
			Test.AddError(FString::Printf(TEXT("%s Seed=%d: behavior initialization rejected"), Scenario, Seed));
			return false;
		}

		Out = FMeasurements{};
		for (int32 Index = 0; Index < 120; ++Index)
		{
			Feedback.FishStaminaRatio = State.FishStamina / 100.0;
			FVector DesiredDirection;
			const FVector Outward = State.FishWorldPosition.GetSafeNormal();
			Rod.RodForwardWorld = Outward;
			if (!FCatFishSteeringModel::AdvanceFeedback(SteeringConfig, Feedback,
				Config.FixedStepSeconds, SteeringState)
				|| !FCatFishSteeringModel::Step(SteeringConfig, Outward,
					Config.FixedStepSeconds, Random, SteeringState, DesiredDirection))
			{
				Test.AddError(FString::Printf(TEXT("%s Seed=%d Step=%d: steering rejected"), Scenario, Seed, Index));
				return false;
			}
			State.FishEffortRatio = SteeringState.CurrentEffortRatio;
			const auto Step = FCatFishingFightSimulator::Step(Config, State, Rod, DesiredDirection);
			if (!Step.bSucceeded || Step.Outcome != ECatFightStepOutcome::None || Step.bExhaustedCatEscape
				|| Step.Trace.FishThrustNewtons > FishStrength + 1e-6
				|| State.CatStamina - Step.CatStaminaDrain <= 0.0)
			{
				Test.AddError(FString::Printf(TEXT("%s Seed=%d Step=%d: unexpected physical result "
					"Accepted=%d Reject=%d Outcome=%d Tow=%d ThrustN=%.5f CatStamina=%.5f"),
					Scenario, Seed, Index, Step.bSucceeded, static_cast<int32>(Step.RejectReason),
					static_cast<int32>(Step.Outcome), Step.bExhaustedCatEscape,
					Step.Trace.FishThrustNewtons, State.CatStamina - Step.CatStaminaDrain));
				return false;
			}
			if (Index >= 40)
			{
				Out.Seconds += Config.FixedStepSeconds;
				Out.ReeledCentimeters += Step.ActualReelDistanceCentimeters;
				Out.RequestedCentimeters += Step.RequestedReelDistanceCentimeters;
				Out.TensionNewtonSeconds += Step.LineTensionNewtons * Config.FixedStepSeconds;
				Out.MaximumTensionNewtons = FMath::Max(Out.MaximumTensionNewtons, Step.LineTensionNewtons);
				Out.EffortSeconds += State.FishEffortRatio * Config.FixedStepSeconds;
				Out.ActiveRadialNewtonSeconds += Step.Trace.FishThrustNewtons
					* FVector::DotProduct(DesiredDirection, Outward) * Config.FixedStepSeconds;
				Out.FishStaminaDrain += Step.FishStaminaDrain;
			}
			// 每步只接收一次最终结果，下一步读取真正更新的惯性和资源，不能反复使用起始状态。
			State.FishWorldPosition = Step.ProposedFishWorldPosition;
			State.FishVelocityCentimetersPerSecond = Step.ResolvedFishVelocityCentimetersPerSecond;
			State.LineLengthCentimeters = Step.LineLengthCentimeters;
			State.AbsoluteRodWear = Step.AbsoluteRodWear;
			State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
			State.CatStamina -= Step.CatStaminaDrain;
			State.FishStamina -= Step.FishStaminaDrain;
			Feedback.NormalizedLineLoad = FMath::Clamp(Step.LineTensionNewtons / FishStrength, 0.0, 1.0);
			Feedback.ActualFishVelocityCentimetersPerSecond = State.FishVelocityCentimetersPerSecond;
			Feedback.ActiveSwimDirection = DesiredDirection;
			Feedback.ExpectedFreeSpeedCentimetersPerSecond = Step.IntendedSwimSpeedCentimetersPerSecond;
			Feedback.bLineTaut = Step.bLineTaut;
		}
		Out.RemainingFishStamina = State.FishStamina;
		UE_LOG(LogCatFishResistanceTest, Display,
			TEXT("Event=fish_resistance_measured Scenario=%s Seed=%d World=PureModel "
				"FishForceN=%.3f CatForceN=50.000 Seconds=%.3f ReelCm=%.3f ReelCmPerSec=%.3f "
				"MeanTensionN=%.3f MaxTensionN=%.3f MeanEffort=%.5f MeanActiveRadialN=%.3f "
				"FishDrain=%.5f FishStaminaRemaining=%.5f"),
			Scenario, Seed, FishStrength, Out.Seconds, Out.ReeledCentimeters, Out.ReelSpeed(),
			Out.MeanTension(), Out.MaximumTensionNewtons, Out.EffortSeconds / Out.Seconds,
			Out.ActiveRadialNewtonSeconds / Out.Seconds, Out.FishStaminaDrain, Out.RemainingFishStamina);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishLateralResistanceTest,
	"Catfishing.Unit.Fishing.Resistance.LateralResistsWhileEaseOffPermitsReel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishLateralResistanceTest::RunTest(const FString& Parameters)
{
	using namespace CatFishResistanceTests;
	const FCatFishSteeringConfig Current;
	const auto Previous = PreviousSteeringConfig();
	// 65 N 位于单人 50 力量、最高 1.35 挑战倍率可选范围内，不依赖目录之外的大鱼制造证据。
	for (const int32 Seed : {11, 37, 101})
	{
		FMeasurements OldLateral, NewLateral, OldEase, NewEase;
		if (!MeasureBehavior(*this, TEXT("OldLateral65"), Previous, ECatFishBehavior::LateralArc, 65.0, Seed, OldLateral)
			|| !MeasureBehavior(*this, TEXT("NewLateral65"), Current, ECatFishBehavior::LateralArc, 65.0, Seed, NewLateral)
			|| !MeasureBehavior(*this, TEXT("OldEase65"), Previous, ECatFishBehavior::EaseOff, 65.0, Seed, OldEase)
			|| !MeasureBehavior(*this, TEXT("NewEase65"), Current, ECatFishBehavior::EaseOff, 65.0, Seed, NewEase)) return false;
		TestTrue(TEXT("旧横切接近满速收线，复现缺乏阻力的基线"), OldLateral.ReelSpeed() > 75.0);
		TestTrue(TEXT("新横切靠实际力限制收线且仍能逐步回收"), NewLateral.ReelSpeed() > 10.0
			&& NewLateral.ReelSpeed() < 60.0 && NewLateral.ReelSpeed() < OldLateral.ReelSpeed() * 0.75);
		TestTrue(TEXT("新横切真实张力增加并守住猫的有限收线能力"), NewLateral.MeanTension() > OldLateral.MeanTension() + 5.0
			&& NewLateral.MaximumTensionNewtons <= 50.0 + 1e-4);
		TestTrue(TEXT("更高有效对抗实际扣除更多鱼体力"), NewLateral.FishStaminaDrain > OldLateral.FishStaminaDrain * 4.0);
		TestTrue(TEXT("停止主动向内游后缓游仍保留有效回收窗口"), OldEase.ReelSpeed() > 70.0
			&& NewEase.ReelSpeed() > 70.0 && NewEase.ReelSpeed() > NewLateral.ReelSpeed() + 15.0);
		TestTrue(TEXT("缓游的实际张力与鱼耗体低于横切对抗"), NewEase.MeanTension() < NewLateral.MeanTension()
			&& NewEase.FishStaminaDrain < NewLateral.FishStaminaDrain * 0.2);
		TestTrue(TEXT("六秒对抗没有把鱼体力快速清零"), NewLateral.RemainingFishStamina > 80.0
			&& NewEase.RemainingFishStamina > 80.0);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishWeakResistanceTest,
	"Catfishing.Unit.Fishing.Resistance.WeakerFishRemainsRetrievable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishWeakResistanceTest::RunTest(const FString& Parameters)
{
	using namespace CatFishResistanceTests;
	const FCatFishSteeringConfig Current;
	const auto Previous = PreviousSteeringConfig();
	// 用户日志中的 27.34 N 鱼应服从相同物理规则，不给弱鱼额外阻力或无来源的力。
	for (const int32 Seed : {11, 37, 101})
	{
		FMeasurements OldLateral, NewLateral;
		if (!MeasureBehavior(*this, TEXT("OldLateral27.34"), Previous, ECatFishBehavior::LateralArc, 27.34, Seed, OldLateral)
			|| !MeasureBehavior(*this, TEXT("NewLateral27.34"), Current, ECatFishBehavior::LateralArc, 27.34, Seed, NewLateral)) return false;
		TestTrue(TEXT("弱鱼新横切仍能完成至少95%的真实请求收线"),
			NewLateral.ReeledCentimeters > NewLateral.RequestedCentimeters * 0.95);
		TestTrue(TEXT("弱鱼不会因行为名称获得人为收线限速"),
			FMath::Abs(NewLateral.ReelSpeed() - OldLateral.ReelSpeed()) < 2.0);
		TestTrue(TEXT("弱鱼提高出力时张力和鱼费用仍按实际对抗增加"),
			NewLateral.MeanTension() > OldLateral.MeanTension()
			&& NewLateral.MaximumTensionNewtons < 40.0
			&& NewLateral.FishStaminaDrain > OldLateral.FishStaminaDrain);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
