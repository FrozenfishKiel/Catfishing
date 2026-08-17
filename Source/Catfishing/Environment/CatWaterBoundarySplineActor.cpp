#include "Environment/CatWaterBoundarySplineActor.h"

#include "Components/SplineComponent.h"
#include "Environment/CatWaterRegion.h"

namespace CatWaterBoundaryPrivate
{
	constexpr int32 MaxSubdivisionDepth = 20;
	constexpr double VertexEpsilonSquared = 1.0e-12;

	static bool IsFinite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	static double DistanceToSegment(const FVector& Point, const FVector& Start, const FVector& End)
	{
		const FVector Segment = End - Start;
		const double LengthSquared = Segment.SizeSquared();
		if (LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector::Distance(Point, Start);
		}
		const double Alpha = FMath::Clamp(FVector::DotProduct(Point - Start, Segment) / LengthSquared, 0.0, 1.0);
		return FVector::Distance(Point, Start + Segment * Alpha);
	}

	static bool SampleBezier(
		const FVector& P0,
		const FVector& C1,
		const FVector& C2,
		const FVector& P1,
		double MaxLength,
		double MaxError,
		int32 Depth,
		TArray<FVector>& OutPoints,
		FString& OutError)
	{
		if (!IsFinite(P0) || !IsFinite(C1) || !IsFinite(C2) || !IsFinite(P1))
		{
			OutError = TEXT("Spline contains non-finite position or tangent data.");
			return false;
		}
		const bool bLengthSatisfied = FVector::Distance(P0, P1) <= MaxLength;
		const bool bErrorSatisfied = FMath::Max(
			DistanceToSegment(C1, P0, P1), DistanceToSegment(C2, P0, P1)) <= MaxError;
		if (bLengthSatisfied && bErrorSatisfied)
		{
			OutPoints.Add(P1);
			return true;
		}
		if (Depth >= MaxSubdivisionDepth)
		{
			OutError = TEXT("Spline adaptive sampling exceeded maximum subdivision depth.");
			return false;
		}

		const FVector P01 = (P0 + C1) * 0.5;
		const FVector P12 = (C1 + C2) * 0.5;
		const FVector P23 = (C2 + P1) * 0.5;
		const FVector P012 = (P01 + P12) * 0.5;
		const FVector P123 = (P12 + P23) * 0.5;
		const FVector Mid = (P012 + P123) * 0.5;
		return SampleBezier(P0, P01, P012, Mid, MaxLength, MaxError, Depth + 1, OutPoints, OutError)
			&& SampleBezier(Mid, P123, P23, P1, MaxLength, MaxError, Depth + 1, OutPoints, OutError);
	}
}

ACatWaterBoundarySplineActor::ACatWaterBoundarySplineActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	BoundarySpline = CreateDefaultSubobject<USplineComponent>(TEXT("BoundarySpline"));
	SetRootComponent(BoundarySpline);
	BoundarySpline->SetClosedLoop(true, false);
	BoundarySpline->SetCanEverAffectNavigation(false);
}

const USplineComponent* ACatWaterBoundarySplineActor::GetBoundarySpline() const
{
	return BoundarySpline;
}

bool ACatWaterBoundarySplineActor::BuildPolygonInput(
	const FTransform& WorldToPlane,
	const double MaxSampleSegmentLengthCm,
	const double MaxChordErrorCm,
	FCatWaterPolygonBuildInput& OutInput,
	FString& OutError) const
{
	using namespace CatWaterBoundaryPrivate;
	OutInput = FCatWaterPolygonBuildInput();
	OutError.Reset();
	if (!BoundarySpline || BoundaryId.IsNone())
	{
		OutError = TEXT("BoundaryId and spline are required.");
		return false;
	}
	if (!BoundarySpline->IsClosedLoop() || BoundarySpline->GetNumberOfSplinePoints() < 3)
	{
		OutError = TEXT("Boundary spline must be closed and have at least three points.");
		return false;
	}
	if (!FMath::IsFinite(MaxSampleSegmentLengthCm) || MaxSampleSegmentLengthCm <= 0.0
		|| !FMath::IsFinite(MaxChordErrorCm) || MaxChordErrorCm <= 0.0
		|| WorldToPlane.ContainsNaN())
	{
		OutError = TEXT("Sampling limits and WorldToPlane must be finite and positive.");
		return false;
	}

	TArray<FVector> WorldSamples;
	const int32 NumPoints = BoundarySpline->GetNumberOfSplinePoints();
	TArray<FVector2D> UniqueControlVertices;
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const FVector PlaneControl = WorldToPlane.TransformPosition(
			BoundarySpline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World));
		if (!IsFinite(PlaneControl))
		{
			OutError = TEXT("Spline contains a non-finite control vertex.");
			return false;
		}
		const FVector2D Vertex(PlaneControl.X, PlaneControl.Y);
		if (!UniqueControlVertices.ContainsByPredicate([&](const FVector2D& Existing)
		{
			return FVector2D::DistSquared(Existing, Vertex) <= VertexEpsilonSquared;
		}))
		{
			UniqueControlVertices.Add(Vertex);
		}
	}
	if (UniqueControlVertices.Num() < 3)
	{
		OutError = TEXT("Boundary spline must have at least three finite unique control vertices.");
		return false;
	}
	WorldSamples.Add(BoundarySpline->GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World));
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const float StartKey = static_cast<float>(Index);
		const float EndKey = static_cast<float>(Index + 1);
		const FVector P0 = BoundarySpline->GetLocationAtSplineInputKey(StartKey, ESplineCoordinateSpace::World);
		const FVector P1 = BoundarySpline->GetLocationAtSplineInputKey(EndKey, ESplineCoordinateSpace::World);
		const FVector D0 = BoundarySpline->GetTangentAtSplineInputKey(StartKey, ESplineCoordinateSpace::World);
		const FVector D1 = BoundarySpline->GetTangentAtSplineInputKey(EndKey, ESplineCoordinateSpace::World);
		const double KeySpan = static_cast<double>(EndKey - StartKey);
		if (!SampleBezier(P0, P0 + D0 * (KeySpan / 3.0), P1 - D1 * (KeySpan / 3.0), P1,
			MaxSampleSegmentLengthCm, MaxChordErrorCm, 0, WorldSamples, OutError))
		{
			return false;
		}
	}
	if (WorldSamples.Num() > 1 && FVector::DistSquared(WorldSamples[0], WorldSamples.Last()) <= VertexEpsilonSquared)
	{
		WorldSamples.Pop(EAllowShrinking::No);
	}

	OutInput.BoundaryId = BoundaryId;
	OutInput.Operation = Operation;
	for (const FVector& WorldPoint : WorldSamples)
	{
		const FVector PlanePoint = WorldToPlane.TransformPosition(WorldPoint);
		if (!IsFinite(PlanePoint))
		{
			OutInput = FCatWaterPolygonBuildInput();
			OutError = TEXT("Projected spline point is non-finite.");
			return false;
		}
		const FVector2D Vertex(PlanePoint.X, PlanePoint.Y);
		if (OutInput.Vertices.IsEmpty() || FVector2D::DistSquared(OutInput.Vertices.Last(), Vertex) > VertexEpsilonSquared)
		{
			OutInput.Vertices.Add(Vertex);
		}
	}
	if (OutInput.Vertices.Num() > 1
		&& FVector2D::DistSquared(OutInput.Vertices[0], OutInput.Vertices.Last()) <= VertexEpsilonSquared)
	{
		OutInput.Vertices.Pop(EAllowShrinking::No);
	}
	TArray<FVector2D> UniqueVertices;
	for (const FVector2D& Vertex : OutInput.Vertices)
	{
		if (!UniqueVertices.ContainsByPredicate([&](const FVector2D& Existing)
		{
			return FVector2D::DistSquared(Existing, Vertex) <= VertexEpsilonSquared;
		}))
		{
			UniqueVertices.Add(Vertex);
		}
	}
	if (UniqueVertices.Num() < 3)
	{
		OutInput = FCatWaterPolygonBuildInput();
		OutError = TEXT("Boundary spline must produce at least three finite unique vertices.");
		return false;
	}
	return true;
}

#if WITH_EDITOR
void ACatWaterBoundarySplineActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (OwningRegion)
	{
		OwningRegion->InvalidateBakedGeometry();
	}
}

void ACatWaterBoundarySplineActor::PostEditMove(const bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished && OwningRegion)
	{
		OwningRegion->InvalidateBakedGeometry();
	}
}
#endif
