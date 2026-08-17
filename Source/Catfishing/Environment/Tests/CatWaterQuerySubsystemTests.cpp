#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"

#if WITH_EDITOR
#include "Components/SplineComponent.h"
#include "Environment/CatWaterBoundarySplineActor.h"
#include "Misc/DataValidation.h"
#endif

#include <limits>

namespace CatWaterQueryTest
{
	static FCatWaterGeometryCache BuildRegion(FName Id, FVector Origin, TArray<FVector2D> Vertices = {})
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = Id;
		Input.PlaneToWorld = FTransform(FRotator::ZeroRotator, Origin);
		Input.WaterPointVerticalToleranceCm = 10;
		Input.BankHeightToleranceCm = 40;
		Input.BoundaryToleranceCm = 1;
		Input.MaxLandingCorrectionCm = 25;
		Input.MinimumWaterInsetCm = 5;
		Input.MaxSampleSegmentLengthCm = 100;
		Input.MaxChordErrorCm = 5;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = Vertices.IsEmpty()
			? TArray<FVector2D>{{0,0}, {200,0}, {200,200}, {0,200}}
			: MoveTemp(Vertices);
		return FCatWaterGeometry::Build(Input).Cache;
	}

	static ACatWaterRegion* SpawnDeferred(UWorld* World, const FCatWaterGeometryCache& Cache)
	{
		const FTransform Transform(FRotator::ZeroRotator, Cache.PlaneToWorld.GetLocation());
		ACatWaterRegion* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), Transform);
		if (!Region) return nullptr;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Cache);
		Region->FinishSpawning(Transform);
		return Region;
	}

#if WITH_EDITOR
	static ACatWaterRegion* BakeSquare(UWorld* World, FName Id, FVector Location)
	{
		ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
		Region->SetActorLocation(Location); Region->RegionId = Id; Region->WaterSurfaceZ = Location.Z;
		Region->WaterPointVerticalToleranceCm = 10; Region->BankHeightToleranceCm = 40;
		Region->MaxLandingCorrectionCm = 25; Region->MinimumWaterInsetCm = 5;
		ACatWaterBoundarySplineActor* Boundary = World->SpawnActor<ACatWaterBoundarySplineActor>();
		Boundary->BoundaryId = TEXT("Outer"); Boundary->OwningRegion = Region;
		USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
		Spline->ClearSplinePoints(false);
		for (const FVector Point : {FVector(0,0,Location.Z), FVector(200,0,Location.Z), FVector(200,200,Location.Z), FVector(0,200,Location.Z)})
			Spline->AddSplinePoint(Point + FVector(Location.X, Location.Y, 0), ESplineCoordinateSpace::World, false);
		for (int32 Index=0; Index<4; ++Index) Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
		Spline->SetClosedLoop(true, true); Region->BoundaryActors.Add(Boundary); Region->BakeGeometry();
		return Region;
	}
#endif
}

#define CAT_QUERY_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.WaterQuery." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_QUERY_TEST(FCatWaterRegistersTest, "RegistersAndUnregistersBakedRegions")
CAT_QUERY_TEST(FCatWaterStaleHandleTest, "RejectsStaleExpectedHandle")
CAT_QUERY_TEST(FCatWaterHeightAmbiguityTest, "HeightToleranceAndRegionAmbiguityFailClosed")
CAT_QUERY_TEST(FCatWaterExactAmbiguityTest, "ExactHandleCannotOverrideOverlappingRegionAmbiguity")
CAT_QUERY_TEST(FCatWaterRayTest, "ResolveRayUsesHorizontalWaterPlane")
CAT_QUERY_TEST(FCatWaterCandidateZTest, "CandidateIgnoresClientZAndProjectsToHorizontalPlane")
CAT_QUERY_TEST(FCatWaterBoundsCoarseTest, "BoundsAreOnlyACoarseFilter")
CAT_QUERY_TEST(FCatWaterOutsideBoundsTest, "ExactHandleStillReturnsOutsideShoreBeyondBounds")
CAT_QUERY_TEST(FCatWaterPreviewTieTest, "NearestShoreTieBreakIsStable")
#if WITH_EDITOR
CAT_QUERY_TEST(FCatWaterOverlapValidationTest, "DataValidationRejectsOverlappingSameHeightRegions")
#endif

bool FCatWaterRegistersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld();
	UWorld* World = WorldWrapper.GetTestWorld(); UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("LakeA"), FVector(0,0,100)));
	TestTrue(TEXT("fixture world has begun play"), World->HasBegunPlay());
	TestTrue(TEXT("deferred region has begun play"), Region && Region->HasActorBegunPlay());
	TestTrue(TEXT("deferred region retained injected baked geometry"), Region && Region->HasValidBakedGeometry());
	FCatWaterRegionHandle Found;
	TestEqual(TEXT("registered region is found"), Query->FindRegionById(TEXT("LakeA"), Found), ECatWaterQueryError::None);
	TestEqual(TEXT("registered handle exact"), Found, Region->GetWaterRegionHandle());
	Region->Destroy();
	TestEqual(TEXT("destroyed region unregisters"), Query->FindRegionById(TEXT("LakeA"), Found), ECatWaterQueryError::RegionNotFound);
	return !HasAnyErrors();
}

bool FCatWaterStaleHandleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("LakeA"), FVector(0,0,100)));
	FCatWaterRegionHandle Stale = Region->GetWaterRegionHandle(); ++Stale.GeometryRevision;
	TestEqual(TEXT("stale exact handle rejected"), Query->QueryWaterPoint(FVector(50,50,100), Stale).Error, ECatWaterQueryError::StaleGeometry);
	return !HasAnyErrors();
}

bool FCatWaterHeightAmbiguityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* A = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("A"), FVector(0,0,100)));
	TestEqual(TEXT("water tolerance rejects high point"), Query->QueryWaterPoint(FVector(50,50,111), A->GetWaterRegionHandle()).Error, ECatWaterQueryError::HeightOutOfTolerance);
	CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("B"), FVector(0,0,100)));
	TestEqual(TEXT("same height overlap is ambiguous"), Query->QueryWaterPoint(FVector(50,50,100), A->GetWaterRegionHandle()).Error, ECatWaterQueryError::AmbiguousRegion);
	return !HasAnyErrors();
}

bool FCatWaterExactAmbiguityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* A = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("A"), FVector(0,0,100)));
	CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("B"), FVector(0,0,100)));
	const FCatWaterSpatialResult Result = Query->QueryShoreRelation(FVector(100,100,100), A->GetWaterRegionHandle());
	TestFalse(TEXT("exact handle cannot select overlap"), Result.bSucceeded);
	TestEqual(TEXT("overlap error"), Result.Error, ECatWaterQueryError::AmbiguousRegion);
	return !HasAnyErrors();
}

bool FCatWaterRayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("Lake"), FVector(0,0,100)));
	const FCatWaterSpatialResult Hit = Query->ResolveRayToWater(FVector(50,50,300), FVector(0,0,-1), Region->GetWaterRegionHandle());
	TestTrue(TEXT("down ray hits"), Hit.bSucceeded);
	TestEqual(TEXT("ray resolves horizontal plane"), Hit.WaterSurfaceWorldPoint.Z, 100.0);
	TestEqual(TEXT("parallel ray rejected"), Query->ResolveRayToWater(FVector(50,50,300), FVector(1,0,0), Region->GetWaterRegionHandle()).Error, ECatWaterQueryError::InvalidDirection);
	return !HasAnyErrors();
}

bool FCatWaterCandidateZTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("Lake"), FVector(0,0,100)));
	const FCatWaterSpatialResult Result = Query->ResolveCandidatePointToWater(FVector(50,50,100000), Region->GetWaterRegionHandle());
	TestTrue(TEXT("finite client Z ignored"), Result.bSucceeded);
	TestEqual(TEXT("candidate projected to water Z"), Result.WaterSurfaceWorldPoint.Z, 100.0);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("nonfinite client input rejected"), Query->ResolveCandidatePointToWater(FVector(50,50,NaN), Region->GetWaterRegionHandle()).Error, ECatWaterQueryError::InvalidLocation);
	return !HasAnyErrors();
}

bool FCatWaterBoundsCoarseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	TArray<FVector2D> LShape{{0,0},{200,0},{200,50},{50,50},{50,200},{0,200}};
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("L"), FVector(0,0,100), MoveTemp(LShape)));
	const FCatWaterSpatialResult Result = Query->QueryWaterPoint(FVector(150,150,100), Region->GetWaterRegionHandle());
	TestTrue(TEXT("polygon query succeeds"), Result.bSucceeded);
	TestEqual(TEXT("AABB cannot promote concave cutout"), Result.Containment, ECatWaterContainment::Outside);
	return !HasAnyErrors();
}

bool FCatWaterOutsideBoundsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	ACatWaterRegion* Region = CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("Lake"), FVector(0,0,100)));
	const FCatWaterSpatialResult Result = Query->QueryShoreRelation(FVector(500,100,100), Region->GetWaterRegionHandle());
	TestTrue(TEXT("exact query still returns shore relation"), Result.bSucceeded);
	TestEqual(TEXT("far point outside"), Result.Containment, ECatWaterContainment::Outside);
	TestEqual(TEXT("nearest shore remains available"), Result.NearestShoreWorldPoint, FVector(200,100,100));
	return !HasAnyErrors();
}

bool FCatWaterPreviewTieTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game); WorldWrapper.BeginPlayInTestWorld(); UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* Query = World->GetSubsystem<UCatWaterQuerySubsystem>();
	CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("Zulu"), FVector(300,0,100)));
	CatWaterQueryTest::SpawnDeferred(World, CatWaterQueryTest::BuildRegion(TEXT("Alpha"), FVector(0,0,100)));
	const FCatWaterSpatialResult Result = Query->QueryNearestShoreForPreview(FVector(250,100,100));
	TestTrue(TEXT("preview finds shore"), Result.bSucceeded);
	TestEqual(TEXT("region id deterministically breaks equal tie"), Result.WaterRegion.RegionId, FName(TEXT("Alpha")));
	return !HasAnyErrors();
}

#if WITH_EDITOR
bool FCatWaterOverlapValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Editor); UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* A = CatWaterQueryTest::BakeSquare(World, TEXT("A"), FVector(0,0,100));
	CatWaterQueryTest::BakeSquare(World, TEXT("B"), FVector(50,50,100));
	FDataValidationContext Context;
	TestNotEqual(TEXT("same-height geometric overlap invalid"), A->IsDataValid(Context), EDataValidationResult::Valid);
	return !HasAnyErrors();
}
#endif

#undef CAT_QUERY_TEST

#endif
