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
#include "Items/CatWorldItemSettings.h"

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
	Geometry.BoundaryToleranceCm = 1.0;
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
		Runner->Config.RodStrength = 1000.0;
		Runner->Config.RodDurability = 1000.0;
		Runner->Config.MaximumLineLengthCentimeters = 1000.0;
		Runner->Config.CatStaminaMaximum = 100.0;
		Runner->Config.ReelSpeedCentimetersPerSecond = 160.0;
		Runner->Config.FishCalmSpeedCentimetersPerSecond = 25.0;
		Runner->Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
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
			const auto Motion = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface);
			if (!TestTrue(TEXT("real runtime surface solve continues through gap and slope"), Motion.bSucceeded)) return false;
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
		const auto Reentry = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface);
		TestTrue(TEXT("ground-to-water transition is recoverable"), Reentry.bSucceeded);
		TestFalse(TEXT("water reentry revokes dry-ground pickup eligibility"), Runner->bFishBeached);
		TestEqual(TEXT("reentry follows current water surface"), Reentry.FishWorldPosition.Z, 0.0, 0.01);

		// 实际岸面也可能早于烘焙轮廓；残余收线约束不能被 ActualReelDistance=0 卡住。
		Runner->State.bFishExhausted = false;
		Runner->State.FishStamina = 50.0;
		Runner->State.FishWorldPosition = FVector(158.0, 0.0, 0.0);
		Step.ProposedFishWorldPosition = FVector(150.0, 0.0, 0.0);
		Step.CombinedCatStrength = 50.0;
		Step.FishConstraintCorrectionCentimeters = 8.0;
		Step.bLineTaut = true;
		const auto EarlyGround = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface);
		TestTrue(TEXT("real dry ground inside baked outline still lands"), EarlyGround.bSucceeded && bJustBeached);
		TestEqual(TEXT("early ground is confirmed by collision"), Surface, static_cast<AActor*>(InsideGround));
		TestEqual(TEXT("actual dry height retained inside water outline"), EarlyGround.FishWorldPosition.Z, 40.0, 0.05);

		Runner->bFishBeached = false;
		Runner->State.FishWorldPosition = FVector(-90.0, 0.0, 0.0);
		Runner->State.LineLengthCentimeters = 500.0;
		TestTrue(TEXT("initialize real shore steering"), FCatFishSteeringModel::Initialize(Runner->SteeringConfig,
			FVector::ForwardVector, ECatFishMotionIntent::CalmOrInward, 1.0, Runner->SteeringRandom, Runner->SteeringState));
		Step = FCatFightStepResult{};
		Step.bSucceeded = true;
		Step.bLineTaut = true;
		Step.LineLengthCentimeters = 500.0;
		Step.CombinedCatStrength = 50.0;
		Step.ActualReelDistanceCentimeters = 2.0;
		Step.FishConstraintCorrectionCentimeters = 8.0;
		Step.ProposedFishWorldPosition = FVector(-120.0, 0.0, 0.0);
		Rod.RodTipVelocityCentimetersPerSecond = FVector(-400.0, 0.0, 0.0);
		const auto Swing = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface);
		TestTrue(TEXT("live-fish swing shore contact stays valid"), Swing.bSucceeded);
		TestFalse(TEXT("dominant rod swing does not instantly exhaust the live fish"), bJustBeached);
		TestEqual(TEXT("rod swing does not force stamina drain"), Step.FishStaminaDrain, 0.0);
		Runner->State.bFishExhausted = true;
		Runner->State.FishStamina = 0.0;
		Runner->State.CatAction = ECatFightCatAction::None;
		const auto DeadTow = Runner->ResolveFishSurfaceFromAuthority(Step, Rod, Water, bJustBeached, Normal, Surface);
		TestTrue(TEXT("already exhausted fish follows physical line endpoint movement onto shore"), DeadTow.bSucceeded && bJustBeached);
	}
	return !HasAnyErrors();
}

#endif
