#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

#include "Environment/CatWaterGeometry.h"

namespace CatWaterGeometryTest
{
	static FCatWaterPolygonBuildInput MakeBoundary(
		const FName BoundaryId,
		const ECatWaterBoundaryOperation Operation,
		std::initializer_list<FVector2D> Vertices)
	{
		FCatWaterPolygonBuildInput Boundary;
		Boundary.BoundaryId = BoundaryId;
		Boundary.Operation = Operation;
		for (const FVector2D& Vertex : Vertices)
		{
			Boundary.Vertices.Add(Vertex);
		}
		return Boundary;
	}

	static FCatWaterGeometryBuildInput MakeSquareWithIsland()
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = TEXT("LakeA");
		Input.PlaneToWorld = FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 100.0));
		Input.WaterPointVerticalToleranceCm = 10.0;
		Input.BankHeightToleranceCm = 25.0;
		Input.BoundaryToleranceCm = 0.5;
		Input.MaxLandingCorrectionCm = 10.0;
		Input.MinimumWaterInsetCm = 2.0;
		Input.Boundaries.Add(MakeBoundary(TEXT("Outer"), ECatWaterBoundaryOperation::Include,
			{{0.0, 0.0}, {1000.0, 0.0}, {1000.0, 1000.0}, {0.0, 1000.0}}));
		Input.Boundaries.Add(MakeBoundary(TEXT("Island"), ECatWaterBoundaryOperation::Exclude,
			{{400.0, 400.0}, {600.0, 400.0}, {600.0, 600.0}, {400.0, 600.0}}));
		return Input;
	}

	static bool ContainsErrorFragment(const FCatWaterGeometryBuildResult& Result, const TCHAR* Fragment)
	{
		for (const FString& Error : Result.Errors)
		{
			if (Error.Contains(Fragment, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterGeometryDefaultsFailClosedTest,
	"Catfishing.Unit.Environment.WaterGeometry.DefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterGeometryClassificationTest,
	"Catfishing.Unit.Environment.WaterGeometry.ConvexConcaveAndHolesClassifyCorrectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterGeometryTopologyTest,
	"Catfishing.Unit.Environment.WaterGeometry.RejectsSelfIntersectionAndInvalidTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterGeometryShoreTest,
	"Catfishing.Unit.Environment.WaterGeometry.ShoreDistanceDirectionAndTieBreakAreDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterGeometryRevisionTest,
	"Catfishing.Unit.Environment.WaterGeometry.RevisionIsStableAndConfigurationSensitive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatWaterGeometryDefaultsFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatWaterRegionHandle DefaultHandle;
	TestFalse(TEXT("Default handle is invalid"), DefaultHandle.IsValid());
	const FCatWaterGeometryCache DefaultCache{};
	TestFalse(TEXT("Default cache is not runtime ready"), DefaultCache.IsRuntimeReady());

	const FCatWaterGeometryBuildResult DefaultBuild = FCatWaterGeometry::Build(FCatWaterGeometryBuildInput());
	TestFalse(TEXT("Default build fails"), DefaultBuild.bSucceeded);
	TestTrue(TEXT("Default build explains invalid geometry"), DefaultBuild.Errors.Num() > 0);

	const FCatWaterSpatialResult Query = FCatWaterGeometry::QueryPoint(DefaultCache, FVector::ZeroVector, 10.0);
	TestFalse(TEXT("Default cache query fails"), Query.bSucceeded);
	TestEqual(TEXT("Default cache reports invalid geometry"), Query.Error, ECatWaterQueryError::InvalidGeometry);
	TestEqual(TEXT("Default query containment stays outside"), Query.Containment, ECatWaterContainment::Outside);
	TestEqual(TEXT("Default query shore kind stays none"), Query.NearestShoreKind, ECatWaterShoreKind::None);

	const FCatWaterSpatialResult NonFinite = FCatWaterGeometry::QueryPoint(
		DefaultCache, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), 10.0);
	TestEqual(TEXT("Non-finite location is rejected first"), NonFinite.Error, ECatWaterQueryError::InvalidLocation);
	return !HasAnyErrors();
}

bool FCatWaterGeometryClassificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatWaterGeometryBuildResult SquareBuild = FCatWaterGeometry::Build(
		CatWaterGeometryTest::MakeSquareWithIsland());
	TestTrue(TEXT("Convex water with an island builds"), SquareBuild.bSucceeded);
	if (!SquareBuild.bSucceeded)
	{
		return false;
	}

	const FCatWaterGeometryCache& Cache = SquareBuild.Cache;
	const FCatWaterSpatialResult Water = FCatWaterGeometry::QueryPoint(Cache, FVector(100.0, 100.0, 100.0), 10.0);
	TestTrue(TEXT("Water query succeeds"), Water.bSucceeded);
	TestEqual(TEXT("Convex interior is water"), Water.Containment, ECatWaterContainment::Inside);
	TestEqual(TEXT("Water surface is projected onto the plane"), Water.WaterSurfaceWorldPoint, FVector(100.0, 100.0, 100.0));
	TestEqual(TEXT("Horizontal plane normal points up"), Water.WaterSurfaceNormal, FVector::UpVector);

	const FCatWaterSpatialResult Island = FCatWaterGeometry::QueryPoint(Cache, FVector(500, 500, 100), 10.0);
	TestEqual(TEXT("Hole is outside water"), Island.Containment, ECatWaterContainment::Outside);
	TestEqual(TEXT("Hole shore kind"), Island.NearestShoreKind, ECatWaterShoreKind::ExcludedBoundary);
	TestTrue(TEXT("Waterward direction is normalized"), Island.WaterwardDirection.IsNormalized());
	const FCatWaterSpatialResult Moved = FCatWaterGeometry::QueryPoint(
		Cache, Island.NearestShoreWorldPoint + Island.WaterwardDirection * 10.0, 10.0);
	TestEqual(TEXT("Direction points out of the hole into valid water"),
		Moved.Containment, ECatWaterContainment::Inside);

	const FCatWaterSpatialResult TooHigh = FCatWaterGeometry::QueryPoint(Cache, FVector(100.0, 100.0, 111.0), 10.0);
	TestFalse(TEXT("Point above vertical tolerance fails"), TooHigh.bSucceeded);
	TestEqual(TEXT("Height rejection is explicit"), TooHigh.Error, ECatWaterQueryError::HeightOutOfTolerance);
	TestEqual(TEXT("Vertical delta is signed and reported"), TooHigh.VerticalDeltaCm, 11.0);

	FCatWaterGeometryBuildInput ConcaveInput = CatWaterGeometryTest::MakeSquareWithIsland();
	ConcaveInput.RegionId = TEXT("Concave");
	ConcaveInput.Boundaries.Reset();
	ConcaveInput.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("ConcaveOuter"), ECatWaterBoundaryOperation::Include,
		{{0.0, 0.0}, {600.0, 0.0}, {600.0, 200.0}, {200.0, 200.0}, {200.0, 600.0}, {0.0, 600.0}}));
	const FCatWaterGeometryBuildResult ConcaveBuild = FCatWaterGeometry::Build(ConcaveInput);
	TestTrue(TEXT("Concave water builds"), ConcaveBuild.bSucceeded);
	if (ConcaveBuild.bSucceeded)
	{
		TestEqual(TEXT("Concave arm is inside"),
			FCatWaterGeometry::QueryPoint(ConcaveBuild.Cache, FVector(100.0, 500.0, 100.0), 10.0).Containment,
			ECatWaterContainment::Inside);
		TestEqual(TEXT("Concave cutout is outside despite lying in bounds"),
			FCatWaterGeometry::QueryPoint(ConcaveBuild.Cache, FVector(500.0, 500.0, 100.0), 10.0).Containment,
			ECatWaterContainment::Outside);
	}
	return !HasAnyErrors();
}

bool FCatWaterGeometryTopologyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterGeometryBuildInput SelfIntersecting = CatWaterGeometryTest::MakeSquareWithIsland();
	SelfIntersecting.Boundaries.Reset();
	SelfIntersecting.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("BowTie"), ECatWaterBoundaryOperation::Include,
		{{0.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}, {100.0, 0.0}}));
	const FCatWaterGeometryBuildResult BowTie = FCatWaterGeometry::Build(SelfIntersecting);
	TestFalse(TEXT("Self-intersection is rejected"), BowTie.bSucceeded);
	TestTrue(TEXT("Self-intersection has a useful error"),
		CatWaterGeometryTest::ContainsErrorFragment(BowTie, TEXT("intersect")));

	FCatWaterGeometryBuildInput Touching = CatWaterGeometryTest::MakeSquareWithIsland();
	Touching.Boundaries.Reset();
	Touching.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("Outer"), ECatWaterBoundaryOperation::Include,
		{{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}}));
	Touching.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("Touching"), ECatWaterBoundaryOperation::Include,
		{{100.0, 100.0}, {200.0, 100.0}, {200.0, 200.0}, {100.0, 200.0}}));
	TestFalse(TEXT("Includes touching at an endpoint are rejected"), FCatWaterGeometry::Build(Touching).bSucceeded);

	FCatWaterGeometryBuildInput Overlapping = Touching;
	Overlapping.Boundaries[1] = CatWaterGeometryTest::MakeBoundary(
		TEXT("Overlap"), ECatWaterBoundaryOperation::Include,
		{{50.0, 50.0}, {150.0, 50.0}, {150.0, 150.0}, {50.0, 150.0}});
	TestFalse(TEXT("Overlapping includes are rejected"), FCatWaterGeometry::Build(Overlapping).bSucceeded);

	FCatWaterGeometryBuildInput InvalidHole = CatWaterGeometryTest::MakeSquareWithIsland();
	InvalidHole.Boundaries[1] = CatWaterGeometryTest::MakeBoundary(
		TEXT("OutsideHole"), ECatWaterBoundaryOperation::Exclude,
		{{900.0, 400.0}, {1100.0, 400.0}, {1100.0, 600.0}, {900.0, 600.0}});
	TestFalse(TEXT("Exclude crossing its include is rejected"), FCatWaterGeometry::Build(InvalidHole).bSucceeded);

	FCatWaterGeometryBuildInput NestedHoles = CatWaterGeometryTest::MakeSquareWithIsland();
	NestedHoles.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("NestedIsland"), ECatWaterBoundaryOperation::Exclude,
		{{450.0, 450.0}, {550.0, 450.0}, {550.0, 550.0}, {450.0, 550.0}}));
	TestFalse(TEXT("Nested excludes are rejected"), FCatWaterGeometry::Build(NestedHoles).bSucceeded);

	FCatWaterGeometryBuildInput CollinearOverlap = CatWaterGeometryTest::MakeSquareWithIsland();
	CollinearOverlap.Boundaries.Reset();
	CollinearOverlap.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("Collinear"), ECatWaterBoundaryOperation::Include,
		{{0.0, 0.0}, {100.0, 0.0}, {50.0, 0.0}, {50.0, 100.0}, {0.0, 100.0}}));
	TestFalse(TEXT("Self-overlapping collinear edges are rejected"), FCatWaterGeometry::Build(CollinearOverlap).bSucceeded);

	FCatWaterGeometryBuildInput EmptyExclude = CatWaterGeometryTest::MakeSquareWithIsland();
	EmptyExclude.Boundaries[1].Vertices.Reset();
	const FCatWaterGeometryBuildResult EmptyExcludeResult = FCatWaterGeometry::Build(EmptyExclude);
	TestFalse(TEXT("Empty exclude fails closed without entering topology analysis"), EmptyExcludeResult.bSucceeded);
	TestTrue(TEXT("Empty exclude reports its canonical vertex failure"),
		CatWaterGeometryTest::ContainsErrorFragment(EmptyExcludeResult, TEXT("three canonical vertices")));
	return !HasAnyErrors();
}

bool FCatWaterGeometryShoreTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterGeometryBuildInput Input = CatWaterGeometryTest::MakeSquareWithIsland();
	Input.RegionId = TEXT("TieBreak");
	Input.Boundaries.Reset();
	Input.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("Zulu"), ECatWaterBoundaryOperation::Include,
		{{200.0, 0.0}, {300.0, 0.0}, {300.0, 100.0}, {200.0, 100.0}}));
	Input.Boundaries.Add(CatWaterGeometryTest::MakeBoundary(
		TEXT("Alpha"), ECatWaterBoundaryOperation::Include,
		{{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}}));
	const FCatWaterGeometryBuildResult Build = FCatWaterGeometry::Build(Input);
	TestTrue(TEXT("Disjoint includes build"), Build.bSucceeded);
	if (!Build.bSucceeded)
	{
		return false;
	}

	const FCatWaterSpatialResult Between = FCatWaterGeometry::QueryPoint(
		Build.Cache, FVector(150.0, 50.0, 100.0), 10.0);
	TestEqual(TEXT("Point between regions is outside"), Between.Containment, ECatWaterContainment::Outside);
	TestEqual(TEXT("Outside distance is negative"), Between.SignedDistanceToShoreCm, -50.0);
	TestEqual(TEXT("BoundaryId breaks equal-distance ties"), Between.NearestShoreWorldPoint, FVector(100.0, 50.0, 100.0));
	TestEqual(TEXT("Chosen shore direction points into Alpha"), Between.WaterwardDirection, FVector(-1.0, 0.0, 0.0));

	const FCatWaterSpatialResult AlphaCenter = FCatWaterGeometry::QueryPoint(
		Build.Cache, FVector(50.0, 50.0, 100.0), 10.0);
	TestEqual(TEXT("Inside distance is positive"), AlphaCenter.SignedDistanceToShoreCm, 50.0);
	TestEqual(TEXT("Segment index breaks ties within a boundary"), AlphaCenter.NearestShoreWorldPoint, FVector(50.0, 0.0, 100.0));
	TestEqual(TEXT("First segment waterward points inward"), AlphaCenter.WaterwardDirection, FVector(0.0, 1.0, 0.0));

	const FCatWaterSpatialResult OuterCorner = FCatWaterGeometry::QueryPoint(
		Build.Cache, FVector(-10.0, -10.0, 100.0), 10.0);
	const FCatWaterSpatialResult CornerInset = FCatWaterGeometry::QueryPoint(
		Build.Cache, OuterCorner.NearestShoreWorldPoint + OuterCorner.WaterwardDirection * 2.0, 10.0);
	TestEqual(TEXT("Waterward direction at a tied outer corner enters valid water"),
		CornerInset.Containment, ECatWaterContainment::Inside);

	const FCatWaterSpatialResult NearBoundary = FCatWaterGeometry::QueryPoint(
		Build.Cache, FVector(50.0, -0.25, 100.0), 10.0);
	TestEqual(TEXT("Boundary tolerance classifies boundary"), NearBoundary.Containment, ECatWaterContainment::Boundary);
	TestEqual(TEXT("Boundary signed distance is zero"), NearBoundary.SignedDistanceToShoreCm, 0.0);

	const FCatWaterSpatialResult Corrected = FCatWaterGeometry::ResolveCandidatePoint(
		Build.Cache, FVector(-5.0, 50.0, 100.0));
	TestTrue(TEXT("Near outer candidate is corrected"), Corrected.bSucceeded);
	TestEqual(TEXT("Corrected candidate is inside"), Corrected.Containment, ECatWaterContainment::Inside);
	TestEqual(TEXT("Correction lands at configured inset"), Corrected.WaterSurfaceWorldPoint, FVector(2.0, 50.0, 100.0));

	const FCatWaterSpatialResult TooFar = FCatWaterGeometry::ResolveCandidatePoint(
		Build.Cache, FVector(-11.0, 50.0, 100.0));
	TestFalse(TEXT("Candidate beyond correction limit is rejected"), TooFar.bSucceeded);

	const FCatWaterGeometryBuildResult IslandBuild = FCatWaterGeometry::Build(
		CatWaterGeometryTest::MakeSquareWithIsland());
	const FCatWaterSpatialResult InHole = FCatWaterGeometry::ResolveCandidatePoint(
		IslandBuild.Cache, FVector(405.0, 500.0, 100.0));
	TestFalse(TEXT("Excluded candidate is never corrected"), InHole.bSucceeded);
	TestEqual(TEXT("Excluded rejection identifies excluded shore"), InHole.NearestShoreKind, ECatWaterShoreKind::ExcludedBoundary);
	return !HasAnyErrors();
}

bool FCatWaterGeometryRevisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterGeometryBuildInput Input = CatWaterGeometryTest::MakeSquareWithIsland();
	Input.RegionId = TEXT("StableLake");
	const FCatWaterGeometryBuildResult First = FCatWaterGeometry::Build(Input);
	TestTrue(TEXT("Revision fixture builds"), First.bSucceeded);
	TestTrue(TEXT("Successful build has a non-zero positive revision"), First.Cache.Handle.GeometryRevision > 0);

	FCatWaterGeometryBuildInput Equivalent = Input;
	Algo::Reverse(Equivalent.Boundaries[0].Vertices);
	const FVector2D RepeatedVertex = Equivalent.Boundaries[0].Vertices[0];
	Equivalent.Boundaries[0].Vertices.Insert(RepeatedVertex, 1);
	Equivalent.Boundaries[0].Vertices.Add(RepeatedVertex);
	Equivalent.Boundaries[0].Vertices[2].X += 0.04;
	Swap(Equivalent.Boundaries[0], Equivalent.Boundaries[1]);
	const FCatWaterGeometryBuildResult Second = FCatWaterGeometry::Build(Equivalent);
	TestTrue(TEXT("Equivalent canonical geometry builds"), Second.bSucceeded);
	TestEqual(TEXT("Winding, order, duplicates, and sub-quantum noise do not change revision"),
		Second.Cache.Handle.GeometryRevision, First.Cache.Handle.GeometryRevision);

	TArray<FCatWaterGeometryBuildInput> ChangedInputs;
	ChangedInputs.Add(Input); ChangedInputs.Last().RegionId = TEXT("OtherLake");
	ChangedInputs.Add(Input); ChangedInputs.Last().PlaneToWorld.SetLocation(FVector(0.1, 0.0, 100.0));
	ChangedInputs.Add(Input); ChangedInputs.Last().PlaneToWorld.SetRotation(FRotator(0.0, 0.1, 0.0).Quaternion());
	ChangedInputs.Add(Input); ChangedInputs.Last().WaterPointVerticalToleranceCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().BankHeightToleranceCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().BoundaryToleranceCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().MaxLandingCorrectionCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().MinimumWaterInsetCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().MaxSampleSegmentLengthCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().MaxChordErrorCm += 0.1;
	ChangedInputs.Add(Input); ChangedInputs.Last().Boundaries[0].BoundaryId = TEXT("OtherOuter");
	ChangedInputs.Add(Input); ChangedInputs.Last().Boundaries[0].Vertices[1].X += 0.1;

	for (int32 Index = 0; Index < ChangedInputs.Num(); ++Index)
	{
		const int64 ChangedRevision = FCatWaterGeometry::ComputeRevision(ChangedInputs[Index]);
		TestNotEqual(*FString::Printf(TEXT("Configuration mutation %d changes revision"), Index),
			ChangedRevision, First.Cache.Handle.GeometryRevision);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
