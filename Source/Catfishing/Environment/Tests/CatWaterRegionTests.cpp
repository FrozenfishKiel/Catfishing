#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"

#if WITH_EDITOR
#include "Components/SplineComponent.h"
#include "Environment/CatWaterBoundarySplineActor.h"
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"
#endif

namespace CatWaterRegionTest
{
	static FCatWaterGeometryBuildInput MakeSquare(FName Id = TEXT("LakeA"), FVector Origin = FVector(0, 0, 100))
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = Id;
		Input.PlaneToWorld = FTransform(FRotator::ZeroRotator, Origin);
		Input.WaterPointVerticalToleranceCm = 10;
		Input.BankHeightToleranceCm = 30;
		Input.BoundaryToleranceCm = 2;
		Input.MaxLandingCorrectionCm = 25;
		Input.MinimumWaterInsetCm = 5;
		Input.MaxSampleSegmentLengthCm = 100;
		Input.MaxChordErrorCm = 5;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = {{0,0}, {200,0}, {200,200}, {0,200}};
		return Input;
	}

	static FCatWaterGeometryCache BuildSquare(FName Id = TEXT("LakeA"), FVector Origin = FVector(0, 0, 100))
	{
		return FCatWaterGeometry::Build(MakeSquare(Id, Origin)).Cache;
	}

#if WITH_EDITOR
	static ACatWaterBoundarySplineActor* AddSquareBoundary(UWorld* World, ACatWaterRegion* Region, FName Id = TEXT("Outer"))
	{
		ACatWaterBoundarySplineActor* Boundary = World->SpawnActor<ACatWaterBoundarySplineActor>();
		Boundary->BoundaryId = Id;
		Boundary->OwningRegion = Region;
		USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
		Spline->ClearSplinePoints(false);
		for (const FVector Point : {FVector(0,0,0), FVector(200,0,0), FVector(200,200,0), FVector(0,200,0)})
		{
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
		for (int32 Index = 0; Index < 4; ++Index) Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
		Spline->SetClosedLoop(true, true);
		Region->BoundaryActors.Add(Boundary);
		return Boundary;
	}

	static ACatWaterRegion* AddBakedRegion(UWorld* World, FName Id = TEXT("LakeA"))
	{
		ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
		Region->RegionId = Id;
		Region->WaterSurfaceZ = 100;
		Region->WaterPointVerticalToleranceCm = 10;
		Region->BankHeightToleranceCm = 30;
		Region->MaxLandingCorrectionCm = 25;
		Region->MinimumWaterInsetCm = 5;
		AddSquareBoundary(World, Region);
		Region->BakeGeometry();
		return Region;
	}
#endif

	static FCatAggregationCommand MakeAggregation(int64 ExpectedRevision)
	{
		FCatAggregationCommand Command;
		Command.Context.StableNetId = TEXT("PlayerA");
		Command.Context.RequestId = FGuid::NewGuid();
		Command.RegionId = TEXT("LakeA");
		Command.ExpectedAggregationRevision = ExpectedRevision;
		Command.Contribution.Fishy = 1;
		return Command;
	}
}

#define CAT_REGION_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.WaterRegion." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_REGION_TEST(FCatWaterRegionDefaultTest, "DefaultRegionHasNoRuntimeGeometry")
CAT_REGION_TEST(FCatWaterRegionRuntimeCacheTest, "RuntimeUsesBakedCacheNotMutatedSpline")
CAT_REGION_TEST(FCatWaterRegionAggregationTest, "AggregationReplayRevisionAndBudgetContracts")

#if WITH_EDITOR
CAT_REGION_TEST(FCatWaterRegionBakeRejectTest, "BakeRejectsDuplicateIdsAndOwnershipMismatch")
CAT_REGION_TEST(FCatWaterRegionStableHandleTest, "BakeStoresStableNonZeroHandle")
CAT_REGION_TEST(FCatWaterRegionStaleValidationTest, "DataValidationRejectsStaleBake")
CAT_REGION_TEST(FCatWaterRegionMutationTest, "AuthoringMutationInvalidatesRuntimeHandle")
#endif

bool FCatWaterRegionDefaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ACatWaterRegion* Region = NewObject<ACatWaterRegion>();
	TestFalse(TEXT("default has no valid baked geometry"), Region->HasValidBakedGeometry());
	TestFalse(TEXT("default handle invalid"), Region->GetWaterRegionHandle().IsValid());
	TestFalse(TEXT("compatibility gate uses baked cache"), Region->IsRuntimeConfigured());
	return !HasAnyErrors();
}

bool FCatWaterRegionRuntimeCacheTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ACatWaterRegion* Region = NewObject<ACatWaterRegion>();
	const FCatWaterGeometryCache Cache = CatWaterRegionTest::BuildSquare();
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Cache);
	TestTrue(TEXT("injected baked cache is runtime valid"), Region->HasValidBakedGeometry());
	TestTrue(TEXT("inside comes from polygon cache"), Region->ContainsWorldPoint(FVector(100,100,100)));
	TestFalse(TEXT("point in AABB but outside polygon fails"), Region->ContainsWorldPoint(FVector(199,1999,100)));
	Region->BoundaryActors.Reset();
	TestTrue(TEXT("runtime remains independent of authoring actor array"), Region->ContainsWorldPoint(FVector(100,100,100)));
	return !HasAnyErrors();
}

bool FCatWaterRegionAggregationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game);
	ACatWaterRegion* Region = WorldWrapper.GetTestWorld()->SpawnActor<ACatWaterRegion>();
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, CatWaterRegionTest::BuildSquare());
	Region->bEnableAggregation = true; Region->AggregationBudget = 2;
	FCatAggregationCommand First = CatWaterRegionTest::MakeAggregation(1);
	const FCatAggregationResult Applied = Region->ContributeAggregation(First);
	TestTrue(TEXT("first contribution commits"), Applied.Command.bCommitted);
	TestEqual(TEXT("aggregation revision advances"), Applied.AggregationRevision, int64(2));
	const FCatAggregationResult Replay = Region->ContributeAggregation(First);
	TestEqual(TEXT("replay is terminal"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("aggregation does not mutate geometry handle"), Region->GetWaterRegionHandle(), CatWaterRegionTest::BuildSquare().Handle);
	return !HasAnyErrors();
}

#if WITH_EDITOR
bool FCatWaterRegionBakeRejectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Editor);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>(); Region->RegionId = TEXT("LakeA");
	ACatWaterBoundarySplineActor* First = CatWaterRegionTest::AddSquareBoundary(World, Region, TEXT("Same"));
	CatWaterRegionTest::AddSquareBoundary(World, Region, TEXT("Same"));
	Region->BakeGeometry();
	TestFalse(TEXT("duplicate boundary IDs clear cache"), Region->HasValidBakedGeometry());
	Region->BoundaryActors.SetNum(1); First->OwningRegion = nullptr;
	Region->BakeGeometry();
	TestFalse(TEXT("ownership mismatch clears cache"), Region->HasValidBakedGeometry());
	return !HasAnyErrors();
}

bool FCatWaterRegionStableHandleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Editor);
	ACatWaterRegion* Region = CatWaterRegionTest::AddBakedRegion(WorldWrapper.GetTestWorld());
	const FCatWaterRegionHandle First = Region->GetWaterRegionHandle();
	TestTrue(TEXT("bake creates nonzero handle"), First.IsValid());
	Region->BakeGeometry();
	TestEqual(TEXT("same authored geometry has stable handle"), Region->GetWaterRegionHandle(), First);
	TestTrue(TEXT("source digest stored"), FCatWaterRegionTestAccess::GetBakedSourceDigest(*Region) != 0);
	return !HasAnyErrors();
}

bool FCatWaterRegionStaleValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Editor);
	ACatWaterRegion* Region = CatWaterRegionTest::AddBakedRegion(WorldWrapper.GetTestWorld());
	ACatWaterBoundarySplineActor* Boundary = Region->BoundaryActors[0];
	const_cast<USplineComponent*>(Boundary->GetBoundarySpline())->SetLocationAtSplinePoint(
		1, FVector(250,0,0), ESplineCoordinateSpace::World, true);
	FDataValidationContext Context;
	TestNotEqual(TEXT("stale bake rejected by data validation"), Region->IsDataValid(Context), EDataValidationResult::Valid);
	TestFalse(TEXT("stale bake cannot serve runtime handle"), Region->HasValidBakedGeometry());
	return !HasAnyErrors();
}

bool FCatWaterRegionMutationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Editor);
	ACatWaterRegion* Region = CatWaterRegionTest::AddBakedRegion(WorldWrapper.GetTestWorld());
	TestTrue(TEXT("precondition baked"), Region->HasValidBakedGeometry());
	Region->WaterSurfaceZ += 10;
	FProperty* Property = FindFProperty<FProperty>(ACatWaterRegion::StaticClass(), GET_MEMBER_NAME_CHECKED(ACatWaterRegion, WaterSurfaceZ));
	FPropertyChangedEvent Event(Property);
	Region->PostEditChangeProperty(Event);
	TestFalse(TEXT("authoring property mutation invalidates handle"), Region->GetWaterRegionHandle().IsValid());
	return !HasAnyErrors();
}
#endif

#undef CAT_REGION_TEST

#endif
