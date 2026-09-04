#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"

namespace CatExhaustedEscapeTest
{
	FCatFightSimulationConfig Config()
	{
		FCatFightSimulationConfig Value;
		Value.FixedStepSeconds = 0.05;
		Value.PrimaryOperatorMassKilograms = 5.0;
		Value.FishMassKilograms = 0.04;
		Value.FishStrength = 0.4;
		Value.CatStaminaMaximum = 60.0;
		Value.RodDurability = 1.0;
		Value.ReelSpeedCentimetersPerSecond = 80.0;
		Value.FishCalmSpeedCentimetersPerSecond = 95.0;
		Value.FishStruggleSpeedCentimetersPerSecond = 180.0;
		Value.MaximumLineLengthCentimeters = 1500.0;
		return Value;
	}
	FCatFightSimulationState State()
	{
		FCatFightSimulationState Value;
		Value.CatStamina = 0.0;
		Value.FishStamina = 0.1;
		Value.FishWorldPosition = FVector(250.0, 0.0, 0.0);
		Value.LineLengthCentimeters = 500.0;
		Value.CatAction = ECatFightCatAction::Slack;
		Value.MotionIntent = ECatFishMotionIntent::CalmOrInward;
		Value.AbsoluteRodWear = 0.99;
		return Value;
	}
	FCatFightRodConstraintInput Rod()
	{
		FCatFightRodConstraintInput Value;
		Value.bRodHeld = true;
		Value.RodForwardWorld = FVector::ForwardVector;
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatRushTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedCatLocksLineAndIsDraggedContinuously",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatRushTest::RunTest(const FString& Parameters)
{
	using namespace CatExhaustedEscapeTest;
	const auto Settings = Config();
	auto Current = State();
	auto Constraint = Rod();
	double FastestPull = 0.0;
	// 足够长的持续外冲：鱼超过旧最大线长世界距离后，也不能凭自耗/坏竿/逃脱提前收尾。
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const auto Step = FCatFishingFightSimulator::Step(Settings, Current, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("零体力、无助手的外冲步骤持续有效"), Step.bSucceeded && Step.bExhaustedCatEscape)) return false;
		TestEqual(TEXT("残留放线按键不能放长鱼线"), Step.LineLengthCentimeters, 500.0);
		TestEqual(TEXT("拖拽中不通过放线恢复猫体力"), Step.CatStaminaDrain, 0.0);
		TestEqual(TEXT("鱼不会在拖猫时自行耗尽"), Step.FishStaminaDrain, 0.0);
		TestEqual(TEXT("拖落水不被残余耐久磨尽抢先结束"), Step.RodWearDelta, 0.0);
		TestEqual(TEXT("持续外冲不由普通失败终局提前停止"), Step.Outcome, ECatFightStepOutcome::None);
		TestEqual(TEXT("持续使用快速游速而非平静休息速度"), Step.IntendedSwimSpeedCentimetersPerSecond, 360.0);
		FastestPull = FMath::Max(FastestPull, Step.CarrierTargetPullSpeedCentimetersPerSecond);
		Constraint.RodTipWorldPosition.X += Step.CarrierTargetPullSpeedCentimetersPerSecond * Settings.FixedStepSeconds;
		Current.FishWorldPosition = Step.ProposedFishWorldPosition;
	}
	TestTrue(TEXT("即使小鱼也能快速拖动无力的猫"), FastestPull >= 350.0);
	TestTrue(TEXT("猫持续被拉向远处而非原地僵持"), Constraint.RodTipWorldPosition.X > 2500.0);
	TestTrue(TEXT("鱼仍在同一锁定线长范围内"),
		FVector::Distance(Current.FishWorldPosition, Constraint.RodTipWorldPosition) <= Current.LineLengthCentimeters + 0.01);

	auto Faster = Settings;
	Faster.ExhaustedCatEscapeSpeedMultiplier = 3.0;
	const auto Tuned = FCatFishingFightSimulator::Step(Faster, State(), Rod(), FVector::ForwardVector);
	TestEqual(TEXT("外冲速度可以独立调参"), Tuned.IntendedSwimSpeedCentimetersPerSecond, 540.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatRescueTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedCatRushEndsForRescueOrExhaustedFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatRescueTest::RunTest(const FString& Parameters)
{
	using namespace CatExhaustedEscapeTest;
	auto Settings = Config();
	auto Current = State();
	TestTrue(TEXT("主位力竭触发持续外冲"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
	Settings.SecondCatStrength = 30.0;
	TestFalse(TEXT("助手实际出力时交回正常对抗"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
	Settings.SecondCatStrength = 0.0;
	Current.CatStamina = 0.001;
	Settings.PrimaryOperatorCatStrength = 50.0;
	TestFalse(TEXT("有力气的接力者不会被强制拖拽"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
	Current = State();
	Settings = Config();
	Current.bOperatorPresent = false;
	TestFalse(TEXT("离竿后不继续强制拖旧猫"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
	Current.bOperatorPresent = true;
	TestFalse(TEXT("未持竿时不强制拖猫"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, false));
	Current.bFishExhausted = true;
	Current.FishStamina = 0.0;
	Current.CatAction = ECatFightCatAction::Pull;
	const auto DeadFish = FCatFishingFightSimulator::Step(Settings, Current, Rod(), FVector::ZeroVector);
	TestTrue(TEXT("鱼已力竭时保留零体力收线"), DeadFish.bSucceeded && !DeadFish.bExhaustedCatEscape
		&& DeadFish.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("力竭鱼不会反过来拖猫"), DeadFish.CarrierTargetPullSpeedCentimetersPerSecond, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatSteeringTest,
	"Catfishing.Unit.Fishing.Steering.ExhaustedCatRushOverridesRestFeintsAndRespectsShore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatSteeringTest::RunTest(const FString& Parameters)
{
	FCatFishSteeringConfig Settings;
	Settings.FullStaminaInwardProbability = 1.0;
	Settings.ExhaustedInwardProbability = 1.0;
	Settings.FeintProbability = 1.0;
	FRandomStream Random(123);
	FCatFishSteeringState Current;
	FVector Direction;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const auto Intent = Index % 2 ? ECatFishMotionIntent::CalmOrInward : ECatFishMotionIntent::StrugglingOutward;
		if (!TestTrue(TEXT("最偏向回头的性格也可持续外冲"), FCatFishSteeringModel::Step(
			Settings, FVector::ForwardVector, Intent, 0.001, 0.05, Random, Current, Direction, true))) return false;
		TestTrue(TEXT("阶段变化和低体力不能让鱼回头"), Direction.Equals(FVector::ForwardVector, 1e-6));
	}
	TestEqual(TEXT("强制外冲不消耗随机抽样"), Random.GetCurrentSeed(), 123);
	TestTrue(TEXT("真实岸线可把冲向陆地的鱼导回水里"), FCatFishSteeringModel::RedirectFromWaterBoundary(
		Settings, -FVector::ForwardVector, Random, Current));
	FCatFishSteeringModel::Step(Settings, FVector::ForwardVector, ECatFishMotionIntent::StrugglingOutward,
		0.001, 0.05, Random, Current, Direction, true);
	TestTrue(TEXT("强制外冲不会立即覆盖岸线的安全方向"), FVector::DotProduct(Current.TargetDirection, -FVector::ForwardVector) > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatWaterTest,
	"Catfishing.Unit.Fishing.Runtime.ExhaustedDragReachesRealDangerousWaterThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatWaterTest::RunTest(const FString& Parameters)
{
	using namespace CatExhaustedEscapeTest;
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game)) return false;
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	if (!Region || !Character) return false;
	FCatWaterGeometryBuildInput Geometry;
	Geometry.RegionId = TEXT("ExhaustedCatEscapeWater");
	Geometry.WaterPointVerticalToleranceCm = 100.0;
	Geometry.BankHeightToleranceCm = 100.0;
	Geometry.BoundaryToleranceCm = 2.0;
	Geometry.MaxLandingCorrectionCm = 20.0;
	Geometry.MinimumWaterInsetCm = 5.0;
	auto& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
	Boundary.BoundaryId = TEXT("Outer");
	Boundary.Vertices = {FVector2D(100.0, -1000.0), FVector2D(5000.0, -1000.0),
		FVector2D(5000.0, 1000.0), FVector2D(100.0, 1000.0)};
	const auto Baked = FCatWaterGeometry::Build(Geometry);
	if (!TestTrue(TEXT("构建真实水域空间查询"), Baked.bSucceeded)) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
	WorldWrapper.BeginPlayInTestWorld();
	const UCatConditionSettings* WaterSettings = GetDefault<UCatConditionSettings>();
	const double FootDepth = WaterSettings->DangerousWaterDepthCentimeters;
	Character->SetActorLocation(FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - FootDepth));
	auto Settings = Config();
	auto Current = State();
	auto Constraint = Rod();
	bool bEntered = false;
	double WetDuration = 0.0;
	// 本测试把纯模型的牵引位移送入真实角色/水域/Condition；不替代 CharacterMovement 或动画画面验收。
	for (int32 Index = 0; Index < 200 && !bEntered; ++Index)
	{
		const auto Step = FCatFishingFightSimulator::Step(Settings, Current, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("进入水域前持续有有效拖拽步骤"), Step.bSucceeded)) return false;
		Character->AddActorWorldOffset(FVector(Step.CarrierTargetPullSpeedCentimetersPerSecond * Settings.FixedStepSeconds, 0.0, 0.0));
		Constraint.RodTipWorldPosition.X = Character->GetActorLocation().X;
		Current.FishWorldPosition = Step.ProposedFishWorldPosition;
		double Depth = 0.0;
		const auto Exposure = Character->GetConditionComponent()->UpdateWaterExposureFromAuthority(
			Region->GetWaterRegionHandle(), Settings.FixedStepSeconds, Depth);
		if (!TestTrue(TEXT("拖拽路径可查询真实水深"), Exposure != ECatWaterExposureUpdate::Unavailable)) return false;
		if (Character->GetConditionComponent()->GetSnapshot().bWet) WetDuration += Settings.FixedStepSeconds;
		bEntered = Exposure == ECatWaterExposureUpdate::DangerousEntered;
		if (bEntered)
		{
			TestTrue(TEXT("满足35厘米深度后才落水"), Depth >= FootDepth - 1e-6);
			TestTrue(TEXT("满足连续确认时长后才落水"), WetDuration + 1e-6 >= WaterSettings->DangerousWaterConfirmationSeconds);
		}
	}
	TestTrue(TEXT("持续外冲把猫从岸外拖进危险水域"), bEntered);
	return !HasAnyErrors();
}

#endif
