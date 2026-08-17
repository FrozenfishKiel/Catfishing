#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Components/SplineComponent.h"
#include "Environment/CatWaterBoundarySplineActor.h"

#include <limits>

namespace CatWaterBoundaryTest
{
	static ACatWaterBoundarySplineActor* SpawnBoundary(UWorld* World)
	{
		return World ? World->SpawnActor<ACatWaterBoundarySplineActor>() : nullptr;
	}

	static void SetClosedPoints(USplineComponent* Spline, std::initializer_list<FVector> Points)
	{
		Spline->ClearSplinePoints(false);
		for (const FVector& Point : Points)
		{
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::Local, false);
		}
		Spline->SetClosedLoop(true, true);
	}

	static bool Build(ACatWaterBoundarySplineActor* Boundary, FCatWaterPolygonBuildInput& Out, FString& Error,
		double MaxLength = 100.0, double MaxError = 5.0, const FTransform& WorldToPlane = FTransform::Identity)
	{
		return Boundary && Boundary->BuildPolygonInput(WorldToPlane, MaxLength, MaxError, Out, Error);
	}
}

#define CAT_BOUNDARY_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.WaterBoundary." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_BOUNDARY_TEST(FCatWaterBoundaryDefaultTest, "DefaultBoundaryFailsClosed")
CAT_BOUNDARY_TEST(FCatWaterBoundaryValidityTest, "RequiresClosedFiniteUniqueVertices")
CAT_BOUNDARY_TEST(FCatWaterBoundarySamplingTest, "SamplingRespectsLengthAndChordError")
CAT_BOUNDARY_TEST(FCatWaterBoundarySCurveTest, "SCurveCannotHideChordErrorAtMidpoint")
CAT_BOUNDARY_TEST(FCatWaterBoundaryProjectionTest, "SamplingProjectsIntoRegionPlane")

bool FCatWaterBoundaryDefaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	ACatWaterBoundarySplineActor* Boundary = CatWaterBoundaryTest::SpawnBoundary(WorldWrapper.GetTestWorld());
	TestNotNull(TEXT("boundary"), Boundary);
	if (!Boundary) return false;
	TestFalse(TEXT("does not tick"), Boundary->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("does not replicate"), Boundary->GetIsReplicated());
	TestTrue(TEXT("spline defaults closed"), Boundary->GetBoundarySpline()->IsClosedLoop());
#if WITH_EDITOR
	TestTrue(TEXT("actor is BlueprintType"), ACatWaterBoundarySplineActor::StaticClass()->HasMetaData(TEXT("BlueprintType")));
#endif
	FCatWaterPolygonBuildInput Input; FString Error;
	TestFalse(TEXT("default spline fails closed"), CatWaterBoundaryTest::Build(Boundary, Input, Error));
	TestTrue(TEXT("failure has diagnostic"), !Error.IsEmpty());
	return !HasAnyErrors();
}

bool FCatWaterBoundaryValidityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game);
	ACatWaterBoundarySplineActor* Boundary = CatWaterBoundaryTest::SpawnBoundary(WorldWrapper.GetTestWorld());
	USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
	Boundary->BoundaryId = TEXT("Outer");
	CatWaterBoundaryTest::SetClosedPoints(Spline, {{0,0,0}, {100,0,0}, {0,100,0}});
	Spline->SetClosedLoop(false, true);
	FCatWaterPolygonBuildInput Input; FString Error;
	TestFalse(TEXT("open spline rejected"), CatWaterBoundaryTest::Build(Boundary, Input, Error));
	Spline->SetClosedLoop(true, true);
	Spline->SetLocationAtSplinePoint(1, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), ESplineCoordinateSpace::Local, true);
	TestFalse(TEXT("non-finite vertex rejected"), CatWaterBoundaryTest::Build(Boundary, Input, Error));
	CatWaterBoundaryTest::SetClosedPoints(Spline, {{0,0,0}, {0,0,0}, {100,0,0}});
	TestFalse(TEXT("fewer than three unique vertices rejected"), CatWaterBoundaryTest::Build(Boundary, Input, Error));
	return !HasAnyErrors();
}

bool FCatWaterBoundarySamplingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game);
	ACatWaterBoundarySplineActor* Boundary = CatWaterBoundaryTest::SpawnBoundary(WorldWrapper.GetTestWorld());
	Boundary->BoundaryId = TEXT("Outer");
	USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
	CatWaterBoundaryTest::SetClosedPoints(Spline, {{0,0,0}, {300,0,0}, {300,300,0}, {0,300,0}});
	for (int32 Index = 0; Index < 4; ++Index) Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	Spline->UpdateSpline();
	FCatWaterPolygonBuildInput Input; FString Error;
	TestTrue(TEXT("square samples"), CatWaterBoundaryTest::Build(Boundary, Input, Error, 75.0, 1.0));
	TestTrue(TEXT("long segments subdivide"), Input.Vertices.Num() >= 16);
	for (int32 Index = 0; Index < Input.Vertices.Num(); ++Index)
	{
		const double Length = FVector2D::Distance(Input.Vertices[Index], Input.Vertices[(Index + 1) % Input.Vertices.Num()]);
		TestTrue(TEXT("each sampled chord obeys max length"), Length <= 75.001);
	}
	TestNotEqual(TEXT("duplicate closing point omitted"), Input.Vertices[0], Input.Vertices.Last());
	return !HasAnyErrors();
}

bool FCatWaterBoundarySCurveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game);
	ACatWaterBoundarySplineActor* Boundary = CatWaterBoundaryTest::SpawnBoundary(WorldWrapper.GetTestWorld());
	Boundary->BoundaryId = TEXT("SCurve");
	USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
	CatWaterBoundaryTest::SetClosedPoints(Spline, {{0,0,0}, {100,0,0}, {100,100,0}, {0,100,0}});
	Spline->SetTangentsAtSplinePoint(0, FVector(100, 600, 0), FVector(100, 600, 0), ESplineCoordinateSpace::Local, false);
	Spline->SetTangentsAtSplinePoint(1, FVector(100, 600, 0), FVector(100, 600, 0), ESplineCoordinateSpace::Local, true);
	FCatWaterPolygonBuildInput Input; FString Error;
	TestTrue(TEXT("S curve samples"), CatWaterBoundaryTest::Build(Boundary, Input, Error, 1000.0, 5.0));
	TestTrue(TEXT("control hull error forces subdivision even if midpoint returns to chord"), Input.Vertices.Num() > 4);
	return !HasAnyErrors();
}

bool FCatWaterBoundaryProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper; WorldWrapper.CreateTestWorld(EWorldType::Game);
	ACatWaterBoundarySplineActor* Boundary = CatWaterBoundaryTest::SpawnBoundary(WorldWrapper.GetTestWorld());
	Boundary->BoundaryId = TEXT("Projected");
	Boundary->SetActorLocation(FVector(1000, 2000, 300));
	USplineComponent* Spline = const_cast<USplineComponent*>(Boundary->GetBoundarySpline());
	CatWaterBoundaryTest::SetClosedPoints(Spline, {{0,0,50}, {100,0,-50}, {0,100,25}});
	for (int32 Index = 0; Index < 3; ++Index) Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	Spline->UpdateSpline();
	FCatWaterPolygonBuildInput Input; FString Error;
	const FTransform WorldToPlane(FRotator::ZeroRotator, FVector(-1000, -2000, -300));
	TestTrue(TEXT("projection succeeds"), CatWaterBoundaryTest::Build(Boundary, Input, Error, 1000.0, 1.0, WorldToPlane));
	TestEqual(TEXT("world X projected"), Input.Vertices[0].X, 0.0);
	TestEqual(TEXT("world Y projected"), Input.Vertices[0].Y, 0.0);
	return !HasAnyErrors();
}

#undef CAT_BOUNDARY_TEST

#endif
