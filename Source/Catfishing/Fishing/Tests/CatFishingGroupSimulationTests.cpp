#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupSignedSupportSimulationTest,
	"Catfishing.Unit.Fishing.Group.SignedSupportUsesOneFishAndCarrierSolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupSignedSupportSimulationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightSimulationConfig Config;
	Config.FixedStepSeconds = 0.05;
	Config.PrimaryOperatorCatStrength = 100.0;
	Config.SecondCatStrength = 150.0;
	Config.PrimaryOperatorMassKilograms = 5.0;
	Config.HelperMassKilograms = 15.0;
	Config.FishMassKilograms = 3.0;
	Config.FishStrength = 40.0;
	Config.CatStaminaMaximum = 60.0;
	Config.ReelSpeedCentimetersPerSecond = 80.0;
	Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Config.MaximumLineLengthCentimeters = 1000.0;
	Config.RodDurability = 1000.0;
	FCatFightSimulationState State;
	State.CatStamina = 30.0;
	State.FishStamina = 100.0;
	State.LineLengthCentimeters = 500.0;
	State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.bGroupDriven = true;
	Constraint.CarrierTravelLimitCentimeters = 100.0;
	const auto Standing = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("站定四人使用同一鱼线求解器"), Standing.bSucceeded)) return false;
	TestTrue(TEXT("静止支撑不会自行把组推向岸内"), Standing.Trace.ConstraintRodEndWorldPosition.X >= -1e-9);
	Constraint.GroupDesiredVelocity = FVector(-300.0, 0.0, 0.0);
	const auto Cooperative = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("主动协作后退可参与同一约束求解"), Cooperative.bSucceeded)) return false;
	TestTrue(TEXT("有效主动后退可以穿过零速度推动端点"), Cooperative.Trace.ConstraintRodEndWorldPosition.X < 0.0);
	TestEqual(TEXT("后退没有在完整支撑外再添加另一份力量"), Cooperative.Trace.CatForceNewtons, 250.0);
	Constraint.CatSupportAlignment = 0.6;
	Constraint.GroupDesiredVelocity = FVector(-180.0, 0.0, 0.0);
	const auto Opposed = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("一人反向后仍使用同一求解器"), Opposed.bSucceeded)) return false;
	TestEqual(TEXT("相反方向使250预算只形成150沿线力"), Opposed.Trace.CatForceNewtons, 150.0);
	TestTrue(TEXT("反向协作降低实际预测后退量"), Opposed.Trace.ConstraintRodEndWorldPosition.X > Cooperative.Trace.ConstraintRodEndWorldPosition.X);
	Constraint.CatSupportAlignment = -1.0;
	Constraint.GroupDesiredVelocity = FVector(300.0, 0.0, 0.0);
	State.FishEffortRatio = 0.0;
	State.LineLengthCentimeters = 600.0;
	State.CatAction = ECatFightCatAction::Pull;
	const auto TowardFish = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("鱼暂时不出力时玩家也能主动向水边走"), TowardFish.bSucceeded)) return false;
	TestTrue(TEXT("向鱼方向的负支撑实际推动持竿组"), TowardFish.Trace.ConstraintRodEndWorldPosition.X > 0.0
		&& TowardFish.CarrierPullAccelerationCentimetersPerSecondSquared > 0.0);
	TestEqual(TEXT("向前施力没有同时获得收线能力"), TowardFish.RequestedReelDistanceCentimeters, 0.0);
	TestEqual(TEXT("负支撑不产生负卷线力上限"), TowardFish.Trace.ReelForceLimitNewtons, 0.0);
	TestEqual(TEXT("沿线推力保留正确符号"), TowardFish.Trace.CatForceNewtons, -250.0);
	auto Recomputed = TowardFish;
	TestTrue(TEXT("地形后重算保留同一有符号力"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Constraint, Recomputed));
	TestEqual(TEXT("最终牵引不把负力错误改回正支撑"), Recomputed.CarrierPullAccelerationCentimetersPerSecondSquared,
		TowardFish.CarrierPullAccelerationCentimetersPerSecondSquared, 1e-9);
	Constraint.CatSupportAlignment = -1.01;
	TestFalse(TEXT("越界方向比例拒绝输入"), FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ForwardVector).bSucceeded);
	Constraint.CatSupportAlignment = -1.0;
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.CatAction = ECatFightCatAction::None;
	const auto ExhaustedFish = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ZeroVector);
	TestTrue(TEXT("鱼力竭后猫仍能主动向前移动"), ExhaustedFish.bSucceeded && ExhaustedFish.bUseContinuousCarrierTraction
		&& ExhaustedFish.Trace.ConstraintRodEndWorldPosition.X > 0.0 && ExhaustedFish.CarrierPullAccelerationCentimetersPerSecondSquared > 0.0);
	TestEqual(TEXT("保留猫移动没有恢复死鱼推力"), ExhaustedFish.Trace.FishThrustNewtons, 0.0);
	TestEqual(TEXT("保留猫移动不改变收尾免耗体契约"), ExhaustedFish.CatStaminaDrain, 0.0);
	TestEqual(TEXT("保留猫移动不让死鱼继续耗体"), ExhaustedFish.FishStaminaDrain, 0.0);
	Constraint.CatSupportAlignment = 1.0;
	Constraint.GroupDesiredVelocity = FVector(-300.0, 0.0, 0.0);
	Constraint.CarrierBackwardTravelLimitCm = 0.0;
	const auto BlockedBack = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ZeroVector);
	TestTrue(TEXT("反向碰撞边界合法参与同一预测"), BlockedBack.bSucceeded);
	TestTrue(TEXT("向岸方向受阻不会预支穿墙后退距离"), BlockedBack.Trace.ConstraintRodEndWorldPosition.X >= -1e-9);
	Constraint.CarrierBackwardTravelLimitCm = -1.0;
	Constraint.CatSupportAlignment = 0.0;
	Constraint.GroupDesiredVelocity = FVector(0.0, 100.0, 0.0);
	Constraint.GroupLateralAcceleration = FVector(0.0, 100.0, 0.0);
	const auto Lateral = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ZeroVector);
	TestTrue(TEXT("零旧切向速度也能预测本步真实横向加速"), Lateral.bSucceeded && Lateral.Trace.ConstraintRodEndWorldPosition.Y > 0.0);
	Constraint.GroupLateralTravelLimitCm = 0.0;
	const auto BlockedLateral = FCatFishingFightSimulator::Step(Config, State, Constraint, FVector::ZeroVector);
	TestTrue(TEXT("横向碰撞边界合法参与同一预测"), BlockedLateral.bSucceeded);
	TestEqual(TEXT("横向受阻不预支穿墙切向位移"), BlockedLateral.Trace.ConstraintRodEndWorldPosition.Y, 0.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
