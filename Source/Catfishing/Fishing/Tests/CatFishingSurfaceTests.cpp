#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "FishContainers/CatFishPickupSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSurfaceTraversalTest,
	"Catfishing.Unit.Fishing.Runtime.LiveAndExhaustedFishTraverseRealShoreGapAndSlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSurfaceTraversalTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("create surface traversal world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Region || !Session || !Cube) return false;
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	FCatWaterGeometryBuildInput Geometry;
	Geometry.RegionId = TEXT("SurfaceTraversalWater");
	Geometry.WaterPointVerticalToleranceCm = 10.0;
	Geometry.BankHeightToleranceCm = 20.0;
	Geometry.BoundaryToleranceCm = 2.0;
	Geometry.MaxLandingCorrectionCm = 20.0;
	Geometry.MinimumWaterInsetCm = 5.0;
	FCatWaterPolygonBuildInput& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
	Boundary.BoundaryId = TEXT("Outer");
	Boundary.Vertices = {FVector2D(0.0, -1000.0), FVector2D(1000.0, -1000.0),
		FVector2D(1000.0, 1000.0), FVector2D(0.0, 1000.0)};
	const auto Baked = FCatWaterGeometry::Build(Geometry);
	if (!TestTrue(TEXT("bake real water query geometry"), Baked.bSucceeded)) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
	const auto SpawnGround = [&](const FVector& Location, const FVector& Scale, const FRotator& Rotation)
	{
		AStaticMeshActor* Ground = World->SpawnActor<AStaticMeshActor>();
		Ground->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Ground->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Ground->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		Ground->SetActorTransform(FTransform(Rotation, Location, Scale));
		return Ground;
	};
	// 水域边界 X=0，实际岸坡从约 X=-100 开始，间隙大于抛竿 MaxLandingCorrection。
	AStaticMeshActor* Slope = SpawnGround(FVector(-300.0, 0.0, 100.0), FVector(4.0, 4.0, 0.2), FRotator(10.0, 0.0, 0.0));
	AStaticMeshActor* InsideGround = SpawnGround(FVector(150.0, 0.0, 30.0), FVector(1.0, 1.0, 0.2), FRotator::ZeroRotator);
	WorldWrapper.BeginPlayInTestWorld();
	if (!TestTrue(TEXT("water registers for runtime queries"), World->GetSubsystem<UCatWaterQuerySubsystem>()
		->QueryShoreRelation(FVector(40.0, 0.0, 0.0), Region->GetWaterRegionHandle()).bSucceeded)) return false;

	for (const bool bInitiallyExhausted : {false, true})
	{
		UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
		Runner->Session = Session;
		Runner->WaterRegion = Region->GetWaterRegionHandle();
		Runner->Config.FixedStepSeconds = 0.05;
		Runner->Config.PrimaryOperatorCatStrength = 50.0;
		Runner->Config.PrimaryOperatorMassKilograms = 5.0;
		Runner->Config.FishMassKilograms = 0.1;
		Runner->Config.FishStrength = 1.0;
		Runner->Config.RodDurability = 1000.0;
		Runner->Config.MaximumLineLengthCentimeters = 1000.0;
		Runner->Config.CatStaminaMaximum = 100.0;
		Runner->Config.ReelSpeedCentimetersPerSecond = 160.0;
		Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		if (!TestTrue(TEXT("surface test uses valid simulation parameters"), Runner->Config.IsValid())) return false;
		Runner->State.CatStamina = 100.0;
		Runner->State.FishStamina = bInitiallyExhausted ? 0.0 : 1000.0;
		Runner->State.bFishExhausted = bInitiallyExhausted;
		Runner->State.CatAction = ECatFightCatAction::Pull;
		Runner->State.MotionIntent = bInitiallyExhausted ? ECatFishMotionIntent::AutoHauling : ECatFishMotionIntent::CalmOrInward;
		Runner->State.FishWorldPosition = FVector(40.0, 0.0, 0.0);
		FCatFightRodConstraintInput Rod;
		Rod.RodTipWorldPosition = FVector(-450.0, 0.0, 150.0);
		Rod.RodForwardWorld = FVector::ForwardVector;
		Rod.bRodHeld = true;
		Runner->State.LineLengthCentimeters = FVector::Distance(Rod.RodTipWorldPosition, Runner->State.FishWorldPosition);
		bool bCrossedGap = false;
		bool bLanded = false;
		int32 GroundedSteps = 0;
		for (int32 Index = 0; Index < 100; ++Index)
		{
			FCatFightStepResult Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Rod,
				Runner->State.bFishExhausted ? FVector::ZeroVector : -FVector::ForwardVector);
			if (!TestTrue(TEXT("real fight simulator continues"), Step.bSucceeded)) return false;
			FCatWaterSpatialResult Water;
			bool bJustBeached = false;
			FVector Normal;
			AActor* Surface = nullptr;
			FCatFishingRodResistanceResult RotationResistance;
			const auto Motion = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
			if (!TestTrue(TEXT("real runtime surface solve continues through gap and slope"), Motion.bSucceeded)) return false;
			TestTrue(TEXT("resolved surface also provides a valid rod load"), RotationResistance.bSucceeded);
			if (!Step.bLineTaut || Step.Outcome == ECatFightStepOutcome::FishExhausted || Runner->State.bFishExhausted)
			{
				TestEqual(TEXT("released or exhausted fish has no stale rod torque"), RotationResistance.MaximumFishTorqueStrengthMeters, 0.0);
			}
			TestTrue(TEXT("haul never bounces toward the water inset"), Motion.FishWorldPosition.X <= Runner->State.FishWorldPosition.X + 0.01);
			if (Motion.FishWorldPosition.X < -30.0 && Motion.FishWorldPosition.X > -90.0)
			{
				bCrossedGap = true;
				TestFalse(TEXT("gap is not falsely classified as pickup-ready ground"), Runner->bFishBeached);
				TestEqual(TEXT("gap tow stays on water surface"), Motion.FishWorldPosition.Z, 0.0, 0.01);
			}
			if (bJustBeached)
			{
				bLanded = true;
				TestTrue(TEXT("first landing keeps ground height instead of overwriting it with water"), Motion.FishWorldPosition.Z > 70.0);
				TestEqual(TEXT("landing uses the real collision slope"), Surface, static_cast<AActor*>(Slope));
				if (!bInitiallyExhausted)
				{
					TestEqual(TEXT("live fish transitions through the exhausted lifecycle"), Step.Outcome, ECatFightStepOutcome::FishExhausted);
					TestEqual(TEXT("landing drains remaining fish stamina exactly once"), Step.FishStaminaDrain, Runner->State.FishStamina);
				}
			}
			if (Runner->bFishBeached)
			{
				++GroundedSteps;
				TestTrue(TEXT("grounded drag follows high slope above bank tolerance"), Motion.FishWorldPosition.Z > 70.0);
				TestTrue(TEXT("contact normal is normalized"), FMath::IsNearlyEqual(Normal.Size(), 1.0, 0.01));
				// 倾斜薄盒的边缘几步会命中侧面；离开边缘后才应命中完整上坡面。
				if (Motion.FishWorldPosition.X < -130.0)
				{
					TestTrue(FString::Printf(TEXT("slope top normal at X=%.2f is %s"),
						Motion.FishWorldPosition.X, *Normal.ToCompactString()), Normal.Z > 0.9 && FMath::Abs(Normal.X) > 0.1);
				}
			}
			Runner->State.FishWorldPosition = Motion.FishWorldPosition;
			Runner->State.LineLengthCentimeters = Step.LineLengthCentimeters;
			Runner->State.FishStamina = FMath::Max(0.0, Runner->State.FishStamina - Step.FishStaminaDrain);
			if (Step.Outcome == ECatFightStepOutcome::FishExhausted) Runner->State.bFishExhausted = true;
		}
		TestTrue(TEXT("fish crossed a gap larger than cast correction distance"), bCrossedGap);
		TestTrue(TEXT("fish reached physical dry ground"), bLanded);
		TestTrue(TEXT("fish kept dragging on the slope for multiple steps"), GroundedSteps > 10);

		// 地面暂时缺失/重新入水是空间转换，不应把整个会话判为 Invalidated。
		FCatFightStepResult Step;
		Step.bSucceeded = true;
		Step.LineLengthCentimeters = Runner->State.LineLengthCentimeters;
		Step.ProposedFishWorldPosition = FVector(-80.0, 0.0, Runner->State.FishWorldPosition.Z);
		FCatWaterSpatialResult Water;
		bool bJustBeached;
		FVector Normal;
		AActor* Surface = nullptr;
		FCatFishingRodResistanceResult RotationResistance;
		const auto Reentry = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
		TestTrue(TEXT("ground-to-water transition is recoverable"), Reentry.bSucceeded);
		TestFalse(TEXT("water reentry revokes dry-ground pickup eligibility"), Runner->bFishBeached);
		TestEqual(TEXT("reentry follows current water surface"), Reentry.FishWorldPosition.Z, 0.0, 0.01);

		// 实际岸面也可能早于烘焙轮廓；残余收线约束不能被 ActualReelDistance=0 卡住。
		Runner->State.bFishExhausted = false;
		Runner->State.FishStamina = 50.0;
		Runner->State.FishWorldPosition = FVector(158.0, 0.0, 0.0);
		Step.ProposedFishWorldPosition = FVector(150.0, 0.0, 0.0);
		Step.OperatorCatStrength = 50.0;
		Step.FishConstraintCorrectionCentimeters = 8.0;
		Step.bLineTaut = true;
		Step.NormalizedTension = 1.0;
		Step.NormalizedLineLoad = 1.0;
		const auto EarlyGround = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
		TestTrue(TEXT("real dry ground inside baked outline still lands"), EarlyGround.bSucceeded && bJustBeached);
		TestEqual(TEXT("early ground is confirmed by collision"), Surface, static_cast<AActor*>(InsideGround));
		TestEqual(TEXT("actual dry height retained inside water outline"), EarlyGround.FishWorldPosition.Z, 40.0, 0.05);
		TestEqual(TEXT("landing removes fish torque before publishing the same step"), RotationResistance.MaximumFishTorqueStrengthMeters, 0.0);

		Runner->bFishBeached = false;
		Runner->State.FishWorldPosition = FVector(-90.0, 0.0, 0.0);
		Runner->State.LineLengthCentimeters = 500.0;
		TestTrue(TEXT("initialize real shore steering"), FCatFishSteeringModel::Initialize(Runner->SteeringConfig,
			FVector::ForwardVector, ECatFishBehavior::EaseOff, 1.0, Runner->SteeringRandom, Runner->SteeringState));
		Step = FCatFightStepResult{};
		Step.bSucceeded = true;
		Step.bLineTaut = true;
		Step.LineLengthCentimeters = 500.0;
		Step.OperatorCatStrength = 50.0;
		Step.ActualReelDistanceCentimeters = 2.0;
		Step.FishConstraintCorrectionCentimeters = 8.0;
		Step.ProposedFishWorldPosition = FVector(-120.0, 0.0, 0.0);
		Step.FishEffortDirection = FVector::ForwardVector;
		Step.IntendedSwimSpeedCentimetersPerSecond = 60.0;
		Step.NormalizedTension = 1.0;
		Step.NormalizedLineLoad = 1.0;
		Rod.RodTipVelocityCentimetersPerSecond = FVector(-400.0, 0.0, 0.0);
		const auto Swing = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
		TestTrue(TEXT("live-fish swing shore contact stays valid"), Swing.bSucceeded);
		TestFalse(TEXT("dominant rod swing does not instantly exhaust the live fish"), bJustBeached);
		TestTrue(TEXT("real shore resolution retains unfulfilled active swim intent even after unloading"),
			Step.FishUnfulfilledDistanceCentimeters > 0.0 && Step.FishStaminaDrain > 0.0
			&& Step.FishStaminaDrain < Runner->State.FishStamina);
		TestFalse(TEXT("real shoreline correction leaves the line slack"), Step.bLineTaut);
		TestEqual(TEXT("slack shoreline result cannot publish the old loaded torque"), RotationResistance.MaximumFishTorqueStrengthMeters, 0.0);
		Runner->State.bFishExhausted = true;
		Runner->State.FishStamina = 0.0;
		Runner->State.CatAction = ECatFightCatAction::None;
		// 上一次解析已经把 Step 替换为真实水面结果；新收尾步骤重新提交同一个岸上候选。
		Step.ProposedFishWorldPosition = FVector(-120.0, 0.0, 0.0);
		const auto DeadTow = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
		TestTrue(TEXT("already exhausted fish follows physical line endpoint movement onto shore"), DeadTow.bSucceeded && bJustBeached);
	}

	// 猫端本步正背离鱼移动：鱼的新位置和旧竿尖会呈现松线，但联合端点仍在承载。
	{
		auto* Runner = NewObject<UCatFishingFightRunner>(Session);
		Runner->Session = Session;
		Runner->WaterRegion = Region->GetWaterRegionHandle();
		Runner->Config.FixedStepSeconds = 0.05;
		Runner->Config.PrimaryOperatorCatStrength = 50.0;
		Runner->Config.PrimaryOperatorMassKilograms = 5.0;
		Runner->Config.FishMassKilograms = 3.0;
		Runner->Config.FishStrength = 75.0;
		Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
		Runner->Config.RodDurability = 1000.0;
		Runner->Config.MaximumLineLengthCentimeters = 1000.0;
		Runner->Config.CatStaminaMaximum = 100.0;
		Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		Runner->State.CatStamina = Runner->State.FishStamina = 100.0;
		Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		Runner->State.FishWorldPosition = FVector(500, 500, 0);
		FCatFightRodConstraintInput Rod;
		Rod.bRodHeld = true;
		Rod.RodTipWorldPosition = FVector(0, 500, 150);
		Rod.CarrierVelocityCentimetersPerSecond = Rod.RodTipVelocityCentimetersPerSecond = FVector(-100, 0, 0);
		Runner->State.LineLengthCentimeters = FVector::Distance(Rod.RodTipWorldPosition, Runner->State.FishWorldPosition);
		if (!TestTrue(TEXT("moving endpoint surface fixture is configured"), Runner->Config.IsValid())) return false;
		auto Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Rod, FVector::ForwardVector);
		if (!TestTrue(TEXT("observed endpoint produces the authoritative line load"), Step.bSucceeded
			&& Step.LineTensionNewtons > 0.0)) return false;
		const auto Predicted = Step;
		FCatWaterSpatialResult Water;
		bool bJustBeached = false;
		FVector Normal;
		AActor* Surface = nullptr;
		FCatFishingRodResistanceResult RotationResistance;
		const auto Motion = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, RotationResistance);
		TestTrue(TEXT("unchanged water candidate delivers shared load to the rod"), Motion.bSucceeded
			&& Motion.FishWorldPosition.Equals(Predicted.ProposedFishWorldPosition, 0.001)
			&& Step.bLineTaut && RotationResistance.MaximumFishTorqueStrengthMeters > 0.0);
		TestEqual(TEXT("surface consumer preserves joint tension"), Step.LineTensionNewtons, Predicted.LineTensionNewtons);
		TestTrue(TEXT("surface consumer preserves physical velocity without position-derived inertia"),
			Step.ResolvedFishVelocityCentimetersPerSecond.Equals(Predicted.ResolvedFishVelocityCentimetersPerSecond, 1e-6));
	}

	// 满线右键和普通锁线经过真实岸线解析后仍交付同一张力、转矩和费用。
	// 此处故意保留原始 Slack，覆盖 Runner 在本步放尽时尚未刷新有效操作的边界。
	FCatFightStepResult LockedAtLimit;
	FCatFishingRodResistanceResult LockedAtLimitResistance;
	for (const bool bAlreadyAtLimit : {true, false})
	for (const auto Action : {ECatFightCatAction::None, ECatFightCatAction::Slack})
	{
		if (!bAlreadyAtLimit && Action == ECatFightCatAction::None) continue;
		auto* Runner = NewObject<UCatFishingFightRunner>(Session);
		Runner->Session = Session;
		Runner->WaterRegion = Region->GetWaterRegionHandle();
		Runner->Config.FixedStepSeconds = 0.05;
		Runner->Config.PrimaryOperatorCatStrength = 50.0;
		Runner->Config.PrimaryOperatorMassKilograms = 5.0;
		Runner->Config.FishMassKilograms = 3.0;
		Runner->Config.FishStrength = 75.0;
		Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
		Runner->Config.RodDurability = 1000.0;
		Runner->Config.CatStaminaMaximum = 100.0;
		Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
		Runner->State.CatStamina = Runner->State.FishStamina = 50.0;
		Runner->State.CatAction = Action;
		Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		Runner->State.FishWorldPosition = FVector(999.9, 500, 0);
		FCatFightRodConstraintInput Rod;
		Rod.bRodHeld = true;
		Rod.RodTipWorldPosition = FVector(499.9, 500, 150);
		Runner->State.LineLengthCentimeters = FVector::Distance(Rod.RodTipWorldPosition, Runner->State.FishWorldPosition);
		Runner->Config.MaximumLineLengthCentimeters = Runner->State.LineLengthCentimeters + (bAlreadyAtLimit ? 0.0 : 0.05);
		Runner->SteeringRandom.Initialize(1459);
		if (!FCatFishSteeringModel::Initialize(Runner->SteeringConfig, FVector::ForwardVector,
			ECatFishBehavior::OutwardRush, 1.0, Runner->SteeringRandom, Runner->SteeringState)) return false;
		auto Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Rod, FVector::ForwardVector);
		const double CandidateLineLength = Step.LineLengthCentimeters;
		FCatWaterSpatialResult Water;
		bool bJustBeached = false;
		FVector Normal;
		AActor* Surface = nullptr;
		FCatFishingRodResistanceResult Resistance;
		const auto Motion = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface, Resistance);
		if (!TestTrue(TEXT("满线角力通过真实水域及岸线消费者"), Motion.bSucceeded && Resistance.bSucceeded)) return false;
		TestTrue(TEXT("测试确实在岸线发生水域修正"), Runner->bLastShoreContactDiagnosticActive);
		if (!bAlreadyAtLimit)
		{
			TestEqual(TEXT("本步候选出线确实曾达到上限"), CandidateLineLength, Runner->Config.MaximumLineLengthCentimeters);
			TestEqual(TEXT("真实岸线挡回后不提交未实际放出的线"), Step.LineLengthCentimeters, Runner->State.LineLengthCentimeters);
			TestTrue(TEXT("最终未实际满线时保留正常回体"), Step.bSlackRecoveryActive && Step.CatStaminaDrain < 0.0);
			continue;
		}
		TestEqual(TEXT("岸线不重新放出超过线杯容量的线"), Step.LineLengthCentimeters, Runner->Config.MaximumLineLengthCentimeters);
		TestFalse(TEXT("岸线后满线仍不回体"), Step.bSlackRecoveryActive);
		if (Action == ECatFightCatAction::None)
		{
			LockedAtLimit = Step;
			LockedAtLimitResistance = Resistance;
		}
		else
		{
			TestTrue(TEXT("满线右键交付与锁线相同的岸线位置"), Step.ProposedFishWorldPosition.Equals(LockedAtLimit.ProposedFishWorldPosition, 1e-6));
			TestEqual(TEXT("满线右键交付相同张力"), Step.LineTensionNewtons, LockedAtLimit.LineTensionNewtons);
			TestEqual(TEXT("满线右键交付相同杆阻力"), Resistance.MaximumFishTorqueStrengthMeters, LockedAtLimitResistance.MaximumFishTorqueStrengthMeters);
			TestEqual(TEXT("岸线后猫体力只按锁线结算一次"), Step.CatStaminaDrain, LockedAtLimit.CatStaminaDrain);
			TestEqual(TEXT("岸线后鱼体力只按锁线结算一次"), Step.FishStaminaDrain, LockedAtLimit.FishStaminaDrain);
			TestEqual(TEXT("岸线后鱼竿磨损只按锁线结算一次"), Step.RodWearDelta, LockedAtLimit.RodWearDelta);
		}
	}

	// 先用真实收线把活鱼拖进烘焙轮廓与碰撞岸坡的间隙，再放线连续游回湖内。
	// 单次最近岸点查询成功不能证明可恢复：每步仍须保留鱼自己朝水里的小位移。
	UCatFishingFightRunner* RecoveryRunner = NewObject<UCatFishingFightRunner>(Session);
	RecoveryRunner->Session = Session;
	RecoveryRunner->WaterRegion = Region->GetWaterRegionHandle();
	RecoveryRunner->Config.FixedStepSeconds = 0.05;
	RecoveryRunner->Config.PrimaryOperatorCatStrength = 50.0;
	RecoveryRunner->Config.PrimaryOperatorMassKilograms = 5.0;
	RecoveryRunner->Config.FishMassKilograms = 0.1;
	RecoveryRunner->Config.FishStrength = 1.0;
	RecoveryRunner->Config.RodDurability = 1000.0;
	RecoveryRunner->Config.MaximumLineLengthCentimeters = 1000.0;
	RecoveryRunner->Config.CatStaminaMaximum = 100.0;
	RecoveryRunner->Config.ReelSpeedCentimetersPerSecond = 160.0;
	RecoveryRunner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	if (!TestTrue(TEXT("gap recovery simulation parameters are valid"), RecoveryRunner->Config.IsValid())) return false;
	RecoveryRunner->State.CatStamina = 100.0;
	RecoveryRunner->State.FishStamina = 1000.0;
	RecoveryRunner->State.CatAction = ECatFightCatAction::Pull;
	RecoveryRunner->State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
	RecoveryRunner->State.FishWorldPosition = FVector(40.0, 0.0, 0.0);
	FCatFightRodConstraintInput RecoveryRod;
	RecoveryRod.RodTipWorldPosition = FVector(-450.0, 0.0, 150.0);
	RecoveryRod.RodForwardWorld = FVector::ForwardVector;
	RecoveryRod.bRodHeld = true;
	RecoveryRunner->State.LineLengthCentimeters = FVector::Distance(
		RecoveryRod.RodTipWorldPosition, RecoveryRunner->State.FishWorldPosition);
	RecoveryRunner->SteeringRandom.Initialize(1459);
	if (!TestTrue(TEXT("gap recovery has initialized runtime shore steering"),
		FCatFishSteeringModel::Initialize(RecoveryRunner->SteeringConfig, FVector::ForwardVector,
			ECatFishBehavior::EaseOff, 1.0, RecoveryRunner->SteeringRandom,
			RecoveryRunner->SteeringState))) return false;

	bool bHasStartedWaterwardRecovery = false;
	const auto AdvanceRecoveryStep = [&](const FVector& DesiredDirection, const bool bReturningToWater)
	{
		const FVector PreviousPosition = RecoveryRunner->State.FishWorldPosition;
		const double PreviousLineLength = RecoveryRunner->State.LineLengthCentimeters;
		FCatFightStepResult Step = FCatFishingFightSimulator::Step(RecoveryRunner->Config,
			RecoveryRunner->State, RecoveryRod, DesiredDirection);
		if (!TestTrue(TEXT("real simulator advances the live gap recovery"), Step.bSucceeded)) return false;
		const FVector CandidateBeforeSurface = Step.ProposedFishWorldPosition;
		FCatWaterSpatialResult Water;
		bool bJustBeached = false;
		FVector Normal;
		AActor* Surface = nullptr;
		FCatFishingRodResistanceResult RotationResistance;
		const auto Motion = RecoveryRunner->ResolveFishSurfaceFromAuthority(
			Step, RecoveryRod, Water, bJustBeached, Normal, Surface, RotationResistance);
		if (!TestTrue(TEXT("real water query and runner resolve the gap recovery"), Motion.bSucceeded)) return false;
		TestFalse(TEXT("gap recovery never grants dry-ground pickup eligibility"), RecoveryRunner->bFishBeached || bJustBeached);
		TestTrue(TEXT("gap recovery does not force the live fish to exhaust"), Step.Outcome != ECatFightStepOutcome::FishExhausted);
		TestEqual(TEXT("gap recovery remains at the actual water surface"), Motion.FishWorldPosition.Z, 0.0, 0.01);
		if (bReturningToWater)
		{
			if (CandidateBeforeSurface.X > PreviousPosition.X + 0.01)
			{
				TestTrue(TEXT("each waterward candidate makes actual progress back into the lake"),
					Motion.FishWorldPosition.X > PreviousPosition.X + 0.01);
				bHasStartedWaterwardRecovery = true;
			}
			else
			{
				// 释放收线时可能仍有被拖向岸的负速度。岸线先刹住越界惯性，不能伪造瞬间反向位移。
				TestTrue(TEXT("initial landward inertia cannot move the released fish farther outside"),
					Motion.FishWorldPosition.X >= PreviousPosition.X - 0.01);
				TestFalse(TEXT("once waterward recovery starts it cannot stall again"), bHasStartedWaterwardRecovery);
			}
			TestTrue(TEXT("runtime recovery never snaps from the gap to the shoreline"),
				FVector::Dist2D(PreviousPosition, Motion.FishWorldPosition)
					<= RecoveryRunner->Config.FishFullEffortSpeedCentimetersPerSecond * RecoveryRunner->State.FishEffortRatio
						* RecoveryRunner->Config.FixedStepSeconds + 0.01);
			TestEqual(TEXT("runtime free spool settles line length against the actual recovered fish position"),
				Step.LineLengthCentimeters, FMath::Max(PreviousLineLength,
					FVector::Distance(RecoveryRod.RodTipWorldPosition, Motion.FishWorldPosition)), 1e-6);
		}
		RecoveryRunner->State.FishWorldPosition = Motion.FishWorldPosition;
		// 与生产 Runner 一样跨步持有地形修正后的速度。旧夹具漏掉此写回，
		// 固定水阻接入后每步重复起步，120步只能走60cm，首次暴露其假停滞。
		RecoveryRunner->State.FishVelocityCentimetersPerSecond = RecoveryRunner->State.bFishExhausted
			|| Step.Outcome != ECatFightStepOutcome::None ? FVector::ZeroVector : Step.ResolvedFishVelocityCentimetersPerSecond;
		RecoveryRunner->State.LineLengthCentimeters = Step.LineLengthCentimeters;
		RecoveryRunner->State.FishStamina = FMath::Max(0.0, RecoveryRunner->State.FishStamina - Step.FishStaminaDrain);
		RecoveryRunner->State.CatStamina = FMath::Clamp(RecoveryRunner->State.CatStamina - Step.CatStaminaDrain,
			0.0, RecoveryRunner->Config.CatStaminaMaximum);
		RecoveryRunner->State.AbsoluteRodWear = Step.AbsoluteRodWear;
		RecoveryRunner->State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
		return true;
	};
	for (int32 Index = 0; Index < 40 && RecoveryRunner->State.FishWorldPosition.X > -60.0; ++Index)
	{
		if (!AdvanceRecoveryStep(-FVector::ForwardVector, false)) return false;
	}
	if (!TestTrue(TEXT("actual reeling reaches the outline-to-ground gap before release"),
		RecoveryRunner->State.FishWorldPosition.X <= -60.0
			&& RecoveryRunner->State.FishWorldPosition.X > -90.0)) return false;
	RecoveryRunner->State.CatAction = ECatFightCatAction::Slack;
	RecoveryRunner->State.FishEffortRatio = 25.0 / RecoveryRunner->Config.FishFullEffortSpeedCentimetersPerSecond;
	const double ReleasePositionX = RecoveryRunner->State.FishWorldPosition.X;
	int32 RecoverySteps = 0;
	for (; RecoverySteps < 120 && RecoveryRunner->State.FishWorldPosition.X < 30.0; ++RecoverySteps)
	{
		if (!AdvanceRecoveryStep(FVector::ForwardVector, true)) return false;
	}
	AddInfo(FString::Printf(TEXT("SurfaceRecovery ReleaseX=%.6f FinalX=%.6f Steps=%d Seconds=%.3f VelocityCmS=%s"),
		ReleasePositionX, RecoveryRunner->State.FishWorldPosition.X, RecoverySteps,
		RecoverySteps * RecoveryRunner->Config.FixedStepSeconds,
		*RecoveryRunner->State.FishVelocityCentimetersPerSecond.ToCompactString()));
	TestTrue(TEXT("live fish returns beyond the shoreline band over multiple simulation steps"),
		RecoveryRunner->State.FishWorldPosition.X >= 30.0 && RecoverySteps > 20);
	const FCatWaterSpatialResult RecoveredWater = World->GetSubsystem<UCatWaterQuerySubsystem>()
		->QueryShoreRelation(RecoveryRunner->State.FishWorldPosition, Region->GetWaterRegionHandle());
	TestTrue(TEXT("the real water query confirms the recovered fish is inside the lake"),
		RecoveredWater.bSucceeded && RecoveredWater.Containment == ECatWaterContainment::Inside);

	// 岸线容差带会连续返回最近岸点。低速鱼必须累积小于旧 1 cm 阈值的游动，才能走出 2 cm 容差带。
	RecoveryRunner->State.FishWorldPosition = FVector::ZeroVector;
	RecoveryRunner->State.LineLengthCentimeters = 600.0;
	RecoveryRunner->State.FishEffortRatio = 10.0 / RecoveryRunner->Config.FishFullEffortSpeedCentimetersPerSecond;
	// 该用例专门验证岸线容差穿越，使用已经达到目标的稳态速度；起步惯性另有回归。
	RecoveryRunner->State.FishVelocityCentimetersPerSecond = FVector(10.0, 0.0, 0.0);
	const UCatWaterQuerySubsystem* RecoveryWater = World->GetSubsystem<UCatWaterQuerySubsystem>();
	const auto InitialBoundary = RecoveryWater->QueryShoreRelation(
		RecoveryRunner->State.FishWorldPosition, Region->GetWaterRegionHandle());
	if (!TestTrue(TEXT("slow recovery starts on the real two-centimeter shoreline band"),
		InitialBoundary.bSucceeded && InitialBoundary.Containment == ECatWaterContainment::Boundary)) return false;
	int32 StepsWithinBoundaryBand = 0;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		if (!AdvanceRecoveryStep(FVector::ForwardVector, true)) return false;
		TestEqual(TEXT("each slow runtime step preserves exactly half a centimeter of waterward swim"),
			RecoveryRunner->State.FishWorldPosition.X, (Index + 1) * 0.5, 1e-6);
		const auto SlowWater = RecoveryWater->QueryShoreRelation(
			RecoveryRunner->State.FishWorldPosition, Region->GetWaterRegionHandle());
		if (!TestTrue(TEXT("real water query remains valid during slow shore recovery"), SlowWater.bSucceeded)) return false;
		if (SlowWater.Containment == ECatWaterContainment::Boundary) ++StepsWithinBoundaryBand;
	}
	TestEqual(TEXT("slow fish advances through four consecutive steps inside the shoreline tolerance"), StepsWithinBoundaryBand, 4);
	const auto ClearedBoundary = RecoveryWater->QueryShoreRelation(
		RecoveryRunner->State.FishWorldPosition, Region->GetWaterRegionHandle());
	TestTrue(TEXT("half-centimeter swims accumulate until the real query classifies the fish inside water"),
		ClearedBoundary.bSucceeded && ClearedBoundary.Containment == ECatWaterContainment::Inside);
	return !HasAnyErrors();
}

#endif
