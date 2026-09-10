#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
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
		Value.FishFullEffortSpeedCentimetersPerSecond = 180.0;
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
	"Catfishing.Unit.Fishing.Simulation.ExhaustedCatLocksLineAndKeepsTowPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatRushTest::RunTest(const FString& Parameters)
{
	using namespace CatExhaustedEscapeTest;
	const auto Settings = Config();
	auto Current = State();
	auto Constraint = Rod();
	// 纯求解只验证锁线与拖水政策，实际三刚体被小鱼拖动由 PhysicalRod 的持续受力回归验证。
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const auto Step = FCatFishingFightSimulator::Step(Settings, Current, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("主控零体力的外冲步骤持续有效"), Step.bSucceeded && Step.bExhaustedCatEscape)) return false;
		TestEqual(TEXT("残留放线按键不能放长鱼线"), Step.LineLengthCentimeters, 500.0);
		TestEqual(TEXT("拖拽中不通过放线恢复猫体力"), Step.CatStaminaDrain, 0.0);
		TestEqual(TEXT("鱼不会在拖猫时自行耗尽"), Step.FishStaminaDrain, 0.0);
		TestEqual(TEXT("拖落水不被残余耐久磨尽抢先结束"), Step.RodWearDelta, 0.0);
		TestEqual(TEXT("持续外冲不由普通失败终局提前停止"), Step.Outcome, ECatFightStepOutcome::None);
		TestEqual(TEXT("持续使用快速游速而非平静休息速度"), Step.IntendedSwimSpeedCentimetersPerSecond, 360.0);
		TestTrue(TEXT("小鱼拖水仍有配置的辅助推力而非仅自身微小力量"),
			Step.Trace.FishThrustNewtons >= Settings.PrimaryOperatorMassKilograms
				* Settings.ExhaustedCatTowAccelerationCentimetersPerSecondSquared / 100.0);
		Current.FishVelocityCentimetersPerSecond = Step.ResolvedFishVelocityCentimetersPerSecond;
		Current.FishWorldPosition = Step.ProposedFishWorldPosition;
	}
	TestTrue(TEXT("鱼仍在同一锁定线长范围内"),
		FVector::Distance(Current.FishWorldPosition, Constraint.RodTipWorldPosition) <= Current.LineLengthCentimeters + 0.01);

	auto Faster = Settings;
	Faster.ExhaustedCatEscapeSpeedMultiplier = 3.0;
	const auto Tuned = FCatFishingFightSimulator::Step(Faster, State(), Rod(), FVector::ForwardVector);
	TestEqual(TEXT("外冲速度可以独立调参"), Tuned.IntendedSwimSpeedCentimetersPerSecond, 540.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatRescueTest,
	"Catfishing.Unit.Fishing.Simulation.ExhaustedCatRushEndsForRecoveryOrExhaustedFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatRescueTest::RunTest(const FString& Parameters)
{
	using namespace CatExhaustedEscapeTest;
	auto Settings = Config();
	auto Current = State();
	TestTrue(TEXT("主位力竭触发持续外冲"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
	Current.CatStamina = 0.001;
	Settings.PrimaryOperatorCatStrength = 50.0;
	TestFalse(TEXT("主控恢复正体力后停止强制拖拽"), FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Settings, Current, true));
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
	TestEqual(TEXT("力竭鱼不再主动推进拖水"), DeadFish.FishDriveAccelerationCentimetersPerSecondSquared, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatSteeringTest,
	"Catfishing.Unit.Fishing.Steering.ExhaustedCatRushOverridesEaseOffAndRespectsShore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingExhaustedCatSteeringTest::RunTest(const FString& Parameters)
{
	FCatFishSteeringConfig Settings;
	Settings.EaseOffInwardBias = 1.0;
	FRandomStream Random(123);
	FCatFishSteeringState Current;
	if (!TestTrue(TEXT("初始化低体力缓游命令"), FCatFishSteeringModel::Initialize(Settings,
		FVector::ForwardVector, ECatFishBehavior::EaseOff, 0.001, Random, Current))) return false;
	const int32 SeedBeforeEscape = Random.GetCurrentSeed();
	FVector Direction;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		if (!TestTrue(TEXT("最偏向回头的性格也可持续外冲"), FCatFishSteeringModel::Step(
			Settings, FVector::ForwardVector, 0.05, Random, Current, Direction, true))) return false;
		TestTrue(TEXT("阶段变化和低体力不能让鱼回头"), Direction.Equals(FVector::ForwardVector, 1e-6));
	}
	TestEqual(TEXT("强制外冲不消耗随机抽样"), Random.GetCurrentSeed(), SeedBeforeEscape);
	TestEqual(TEXT("强制外冲不切换普通策略"), Current.Behavior, ECatFishBehavior::EaseOff);
	TestEqual(TEXT("强制外冲执行不会另推进普通行为时长"), Current.BehaviorElapsedSeconds, 0.0);
	TestTrue(TEXT("真实岸线可把冲向陆地的鱼导回水里"), FCatFishSteeringModel::RedirectFromWaterBoundary(
		Settings, -FVector::ForwardVector, Random, Current));
	FCatFishSteeringModel::Step(Settings, FVector::ForwardVector, 0.05, Random, Current, Direction, true);
	TestTrue(TEXT("强制外冲不会立即覆盖岸线的安全方向"), FVector::DotProduct(Current.TargetDirection, -FVector::ForwardVector) > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingExhaustedCatWaterTest,
	"Catfishing.Unit.Fishing.Contract.ExhaustedTravelSamplesUsePhysicalFootWaterThreshold",
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
	UCatPhysicalBodyComponent* Physical = Character->GetPhysicalBodyComponent();
	if (!TestNotNull(TEXT("physical foot source"), Physical)) return false;
	Physical->TeleportBodyFromAuthority(FTransform(FVector(0.0, 0.0, Physical->GetStandRootHeightCm() - FootDepth)), TEXT("WaterContractStart"));
	const double SampleSeconds = .05;
	bool bEntered = false;
	double WetDuration = 0.0;
	// Contract fixture feeds sampled positions into the actual body/Condition query; real force/drag is covered separately.
	for (int32 Index = 0; Index < 200 && !bEntered; ++Index)
	{
		// 已采样的位置轨迹仅验证水深消费者，不重建被移除的 CMC 牵引积分。
		Physical->TeleportBodyFromAuthority(FTransform(Character->GetActorLocation()
			+ FVector(10.0, 0.0, 0.0)), TEXT("WaterContractSample"));
		double Depth = 0.0;
		const auto Exposure = Character->GetConditionComponent()->UpdateWaterExposureFromAuthority(
			Region->GetWaterRegionHandle(), SampleSeconds, Depth);
		if (!TestTrue(TEXT("拖拽路径可查询真实水深"), Exposure != ECatWaterExposureUpdate::Unavailable)) return false;
		if (Character->GetConditionComponent()->GetSnapshot().bWet) WetDuration += SampleSeconds;
		bEntered = Exposure == ECatWaterExposureUpdate::DangerousEntered;
		if (bEntered)
		{
			TestTrue(TEXT("满足35厘米深度后才落水"), Depth >= FootDepth - 1e-6);
			TestTrue(TEXT("满足连续确认时长后才落水"), WetDuration + 1e-6 >= WaterSettings->DangerousWaterConfirmationSeconds);
		}
	}
	TestTrue(TEXT("采样轨迹跨过真实边界后进入危险水域"), bEntered);
	return !HasAnyErrors();
}

#endif
