#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Environment/CatWaterTypes.h"

#include "CatWaterQuerySubsystem.generated.h"

class ACatWaterRegion;

UCLASS()
class CATFISHING_API UCatWaterQuerySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	FCatWaterSpatialResult QueryWaterPoint(const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult QueryShoreRelation(const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult QueryNearestShoreForPreview(const FVector& WorldPoint, FName OptionalRegionId = NAME_None) const;
	FCatWaterSpatialResult ResolveRayToWater(const FVector& RayOrigin, const FVector& RayDirection,
		const FCatWaterRegionHandle& ExpectedHandle) const;
	FCatWaterSpatialResult ResolveCandidatePointToWater(const FVector& CandidateWorldPoint,
		const FCatWaterRegionHandle& ExpectedHandle) const;
	ECatWaterQueryError FindRegionById(FName RegionId, FCatWaterRegionHandle& OutHandle) const;

	// Temporary compatibility wrapper for legacy callers.
	FCatWaterQueryResult QueryWaterRegion(const FCatWaterQuery& Query) const;

private:
	friend class ACatWaterRegion;

	void RegisterRegion(ACatWaterRegion* Region);
	void UnregisterRegion(const ACatWaterRegion* Region);
	void CompactRegistry() const;
	ECatWaterQueryError ResolveExactRegion(const FCatWaterRegionHandle& ExpectedHandle,
		const ACatWaterRegion*& OutRegion) const;
	bool HasOverlappingWaterResult(const ACatWaterRegion* Target, const FVector& WorldPoint,
		double VerticalToleranceCm, bool bUseBankTolerance) const;

	mutable TMap<FName, TArray<TWeakObjectPtr<ACatWaterRegion>>> RegionsById;
};
