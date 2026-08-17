#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"

#include "Environment/CatWaterTypes.h"

#include "CatWaterGeometry.generated.h"

struct FCatWaterPolygonBuildInput
{
	FName BoundaryId;
	ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;
	TArray<FVector2D> Vertices;
};

struct FCatWaterGeometryBuildInput
{
	FName RegionId;
	FTransform PlaneToWorld = FTransform::Identity;
	double WaterPointVerticalToleranceCm = 0.0;
	double BankHeightToleranceCm = 0.0;
	double BoundaryToleranceCm = 0.0;
	double MaxLandingCorrectionCm = 0.0;
	double MinimumWaterInsetCm = 0.0;
	double MaxSampleSegmentLengthCm = 100.0;
	double MaxChordErrorCm = 5.0;
	TArray<FCatWaterPolygonBuildInput> Boundaries;
};

USTRUCT()
struct FCatWaterBakedPolygon
{
	GENERATED_BODY()

	UPROPERTY()
	FName BoundaryId;

	UPROPERTY()
	TArray<FVector2D> Vertices;

	UPROPERTY()
	FBox2D Bounds = FBox2D(ForceInit);
};

USTRUCT()
struct FCatWaterGeometryCache
{
	GENERATED_BODY()

	UPROPERTY()
	FCatWaterRegionHandle Handle;

	UPROPERTY()
	FTransform WorldToPlane = FTransform::Identity;

	UPROPERTY()
	FTransform PlaneToWorld = FTransform::Identity;

	UPROPERTY()
	TArray<FCatWaterBakedPolygon> IncludePolygons;

	UPROPERTY()
	TArray<FCatWaterBakedPolygon> ExcludePolygons;

	UPROPERTY()
	FBox2D Bounds2D = FBox2D(ForceInit);

	UPROPERTY()
	double WaterSurfaceZ = 0.0;

	UPROPERTY()
	double WaterPointVerticalToleranceCm = 0.0;

	UPROPERTY()
	double BankHeightToleranceCm = 0.0;

	UPROPERTY()
	double BoundaryToleranceCm = 0.0;

	UPROPERTY()
	double MaxLandingCorrectionCm = 0.0;

	UPROPERTY()
	double MinimumWaterInsetCm = 0.0;

	UPROPERTY()
	double MaxSampleSegmentLengthCm = 0.0;

	UPROPERTY()
	double MaxChordErrorCm = 0.0;

	bool IsRuntimeReady() const;
};

struct FCatWaterGeometryBuildResult
{
	bool bSucceeded = false;
	FCatWaterGeometryCache Cache;
	TArray<FString> Errors;
};

class FCatWaterGeometry
{
public:
	static FCatWaterGeometryBuildResult Build(const FCatWaterGeometryBuildInput& Input);
	static FCatWaterSpatialResult QueryPoint(
		const FCatWaterGeometryCache& Cache,
		const FVector& WorldPoint,
		double VerticalToleranceCm);
	static FCatWaterSpatialResult ResolveCandidatePoint(
		const FCatWaterGeometryCache& Cache,
		const FVector& CandidateWorldPoint);
	static int64 ComputeRevision(const FCatWaterGeometryBuildInput& CanonicalInput);
};
