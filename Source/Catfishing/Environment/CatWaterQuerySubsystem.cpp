#include "Environment/CatWaterQuerySubsystem.h"

#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"

namespace CatWaterQueryPrivate
{
	static bool IsFinite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	static FCatWaterSpatialResult MakeError(ECatWaterQueryError Error)
	{
		FCatWaterSpatialResult Result;
		Result.Error = Error;
		return Result;
	}

	static bool IsWater(const FCatWaterSpatialResult& Result)
	{
		return Result.bSucceeded && Result.Containment != ECatWaterContainment::Outside;
	}

	static bool BoundsContainWorldXY(const FCatWaterGeometryCache& Cache, const FVector& WorldPoint)
	{
		const FVector Plane = Cache.WorldToPlane.TransformPosition(WorldPoint);
		return Cache.Bounds2D.IsInsideOrOn(FVector2D(Plane.X, Plane.Y));
	}
}

void UCatWaterQuerySubsystem::RegisterRegion(ACatWaterRegion* Region)
{
	if (!IsValid(Region) || !Region->HasValidBakedGeometry()) return;
	RegionsById.FindOrAdd(Region->RegionId).AddUnique(Region);
}

void UCatWaterQuerySubsystem::UnregisterRegion(const ACatWaterRegion* Region)
{
	for (auto It = RegionsById.CreateIterator(); It; ++It)
	{
		It.Value().RemoveAll([Region](const TWeakObjectPtr<ACatWaterRegion>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Region;
		});
		if (It.Value().IsEmpty()) It.RemoveCurrent();
	}
}

void UCatWaterQuerySubsystem::CompactRegistry() const
{
	for (auto It = RegionsById.CreateIterator(); It; ++It)
	{
		It.Value().RemoveAll([](const TWeakObjectPtr<ACatWaterRegion>& Entry)
		{
			return !Entry.IsValid() || !Entry->HasValidBakedGeometry();
		});
		if (It.Value().IsEmpty()) It.RemoveCurrent();
	}
}

ECatWaterQueryError UCatWaterQuerySubsystem::FindRegionById(
	const FName RegionId, FCatWaterRegionHandle& OutHandle) const
{
	OutHandle = FCatWaterRegionHandle();
	CompactRegistry();
	if (RegionId.IsNone()) return ECatWaterQueryError::RegionNotFound;
	const TArray<TWeakObjectPtr<ACatWaterRegion>>* Entries = RegionsById.Find(RegionId);
	if (!Entries || Entries->IsEmpty()) return ECatWaterQueryError::RegionNotFound;
	if (Entries->Num() != 1) return ECatWaterQueryError::AmbiguousRegion;
	OutHandle = (*Entries)[0]->GetWaterRegionHandle();
	return OutHandle.IsValid() ? ECatWaterQueryError::None : ECatWaterQueryError::InvalidGeometry;
}

ECatWaterQueryError UCatWaterQuerySubsystem::ResolveExactRegion(
	const FCatWaterRegionHandle& ExpectedHandle, const ACatWaterRegion*& OutRegion) const
{
	OutRegion = nullptr;
	if (!ExpectedHandle.IsValid()) return ECatWaterQueryError::StaleGeometry;
	CompactRegistry();
	const TArray<TWeakObjectPtr<ACatWaterRegion>>* Entries = RegionsById.Find(ExpectedHandle.RegionId);
	if (!Entries || Entries->IsEmpty()) return ECatWaterQueryError::RegionNotFound;
	if (Entries->Num() != 1) return ECatWaterQueryError::AmbiguousRegion;
	const ACatWaterRegion* Region = (*Entries)[0].Get();
	if (!Region || Region->GetWaterRegionHandle() != ExpectedHandle) return ECatWaterQueryError::StaleGeometry;
	OutRegion = Region;
	return ECatWaterQueryError::None;
}

bool UCatWaterQuerySubsystem::HasOverlappingWaterResult(
	const ACatWaterRegion* Target, const FVector& WorldPoint, const double VerticalToleranceCm,
	const bool bUseBankTolerance) const
{
	for (const TPair<FName, TArray<TWeakObjectPtr<ACatWaterRegion>>>& Pair : RegionsById)
	{
		for (const TWeakObjectPtr<ACatWaterRegion>& Entry : Pair.Value)
		{
			const ACatWaterRegion* Other = Entry.Get();
			if (!Other || Other == Target || !Other->HasValidBakedGeometry()) continue;
			const FCatWaterGeometryCache& Cache = Other->BakedGeometry;
			const double OtherTolerance = bUseBankTolerance
				? Cache.BankHeightToleranceCm : Cache.WaterPointVerticalToleranceCm;
			const double Tolerance = FMath::Min(VerticalToleranceCm, OtherTolerance);
			if (FMath::Abs(WorldPoint.Z - Cache.WaterSurfaceZ) > Tolerance
				|| !CatWaterQueryPrivate::BoundsContainWorldXY(Cache, WorldPoint)) continue;
			if (CatWaterQueryPrivate::IsWater(FCatWaterGeometry::QueryPoint(Cache, WorldPoint, Tolerance))) return true;
		}
	}
	return false;
}

FCatWaterSpatialResult UCatWaterQuerySubsystem::QueryWaterPoint(
	const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const
{
	if (!CatWaterQueryPrivate::IsFinite(WorldPoint)) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	const ACatWaterRegion* Region = nullptr;
	if (const ECatWaterQueryError Error = ResolveExactRegion(ExpectedHandle, Region); Error != ECatWaterQueryError::None)
		return CatWaterQueryPrivate::MakeError(Error);
	FCatWaterSpatialResult Result = FCatWaterGeometry::QueryPoint(
		Region->BakedGeometry, WorldPoint, Region->BakedGeometry.WaterPointVerticalToleranceCm);
	if (CatWaterQueryPrivate::IsWater(Result)
		&& HasOverlappingWaterResult(Region, WorldPoint,
			Region->BakedGeometry.WaterPointVerticalToleranceCm, false))
		return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::AmbiguousRegion);
	return Result;
}

FCatWaterSpatialResult UCatWaterQuerySubsystem::QueryShoreRelation(
	const FVector& WorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const
{
	if (!CatWaterQueryPrivate::IsFinite(WorldPoint)) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	const ACatWaterRegion* Region = nullptr;
	if (const ECatWaterQueryError Error = ResolveExactRegion(ExpectedHandle, Region); Error != ECatWaterQueryError::None)
		return CatWaterQueryPrivate::MakeError(Error);
	FCatWaterSpatialResult Result = FCatWaterGeometry::QueryPoint(
		Region->BakedGeometry, WorldPoint, Region->BakedGeometry.BankHeightToleranceCm);
	if (CatWaterQueryPrivate::IsWater(Result)
		&& HasOverlappingWaterResult(Region, WorldPoint, Region->BakedGeometry.BankHeightToleranceCm, true))
		return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::AmbiguousRegion);
	return Result;
}

FCatWaterSpatialResult UCatWaterQuerySubsystem::ResolveCandidatePointToWater(
	const FVector& CandidateWorldPoint, const FCatWaterRegionHandle& ExpectedHandle) const
{
	if (!CatWaterQueryPrivate::IsFinite(CandidateWorldPoint)) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	const ACatWaterRegion* Region = nullptr;
	if (const ECatWaterQueryError Error = ResolveExactRegion(ExpectedHandle, Region); Error != ECatWaterQueryError::None)
		return CatWaterQueryPrivate::MakeError(Error);
	const FCatWaterGeometryCache& Cache = Region->BakedGeometry;
	FVector PlanePoint = Cache.WorldToPlane.TransformPosition(CandidateWorldPoint);
	PlanePoint.Z = 0.0;
	const FVector Projected = Cache.PlaneToWorld.TransformPosition(PlanePoint);
	FCatWaterSpatialResult Result = FCatWaterGeometry::ResolveCandidatePoint(Cache, Projected);
	if (CatWaterQueryPrivate::IsWater(Result)
		&& HasOverlappingWaterResult(Region, Result.WaterSurfaceWorldPoint,
			Cache.WaterPointVerticalToleranceCm, false))
		return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::AmbiguousRegion);
	return Result;
}

FCatWaterSpatialResult UCatWaterQuerySubsystem::ResolveRayToWater(
	const FVector& RayOrigin, const FVector& RayDirection, const FCatWaterRegionHandle& ExpectedHandle) const
{
	if (!CatWaterQueryPrivate::IsFinite(RayOrigin)) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	if (!CatWaterQueryPrivate::IsFinite(RayDirection) || !RayDirection.IsNormalized() || FMath::IsNearlyZero(RayDirection.Z))
		return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidDirection);
	const ACatWaterRegion* Region = nullptr;
	if (const ECatWaterQueryError Error = ResolveExactRegion(ExpectedHandle, Region); Error != ECatWaterQueryError::None)
		return CatWaterQueryPrivate::MakeError(Error);
	const double Time = (Region->BakedGeometry.WaterSurfaceZ - RayOrigin.Z) / RayDirection.Z;
	if (!FMath::IsFinite(Time) || Time < 0.0) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	return ResolveCandidatePointToWater(RayOrigin + RayDirection * Time, ExpectedHandle);
}

FCatWaterSpatialResult UCatWaterQuerySubsystem::QueryNearestShoreForPreview(
	const FVector& WorldPoint, const FName OptionalRegionId) const
{
	if (!CatWaterQueryPrivate::IsFinite(WorldPoint)) return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::InvalidLocation);
	CompactRegistry();
	FCatWaterSpatialResult Best;
	double BestDistance = TNumericLimits<double>::Max();
	FString BestId;
	for (const TPair<FName, TArray<TWeakObjectPtr<ACatWaterRegion>>>& Pair : RegionsById)
	{
		if (!OptionalRegionId.IsNone() && Pair.Key != OptionalRegionId) continue;
		if (Pair.Value.Num() != 1)
		{
			return CatWaterQueryPrivate::MakeError(ECatWaterQueryError::AmbiguousRegion);
		}
		const ACatWaterRegion* Region = Pair.Value[0].Get();
		if (!Region) continue;
		FCatWaterSpatialResult Candidate = FCatWaterGeometry::QueryPoint(
			Region->BakedGeometry, WorldPoint, Region->BakedGeometry.BankHeightToleranceCm);
		if (!Candidate.bSucceeded) continue;
		const double Distance = FMath::Abs(Candidate.SignedDistanceToShoreCm);
		const FString CandidateId = Candidate.WaterRegion.RegionId.ToString().ToLower();
		if (!Best.bSucceeded || Distance < BestDistance - UE_DOUBLE_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(Distance, BestDistance) && CandidateId < BestId))
		{
			Best = MoveTemp(Candidate); BestDistance = Distance; BestId = CandidateId;
		}
	}
	return Best.bSucceeded ? Best : CatWaterQueryPrivate::MakeError(ECatWaterQueryError::RegionNotFound);
}

FCatWaterQueryResult UCatWaterQuerySubsystem::QueryWaterRegion(const FCatWaterQuery& Query) const
{
	FCatWaterQueryResult Result; Result.RunRevision = Query.RunRevision;
	if (Query.RunRevision <= 0) { Result.Error = ECatWaterQueryError::RevisionConflict; return Result; }
	if (Query.RunPhase.Phase != ECatRunPhase::DayActive || !Query.RunPhase.bFishingAllowed)
	{
		Result.Error = ECatWaterQueryError::FishingClosed; return Result;
	}
	if (!CatWaterQueryPrivate::IsFinite(Query.WorldLocation))
	{
		Result.Error = ECatWaterQueryError::InvalidLocation; return Result;
	}
	CompactRegistry();
	const ACatWaterRegion* Match = nullptr;
	for (const TPair<FName, TArray<TWeakObjectPtr<ACatWaterRegion>>>& Pair : RegionsById)
	{
		for (const TWeakObjectPtr<ACatWaterRegion>& Entry : Pair.Value)
		{
			const ACatWaterRegion* Region = Entry.Get();
			if (!Region || !CatWaterQueryPrivate::BoundsContainWorldXY(Region->BakedGeometry, Query.WorldLocation)) continue;
			const FCatWaterSpatialResult Spatial = FCatWaterGeometry::QueryPoint(
				Region->BakedGeometry, Query.WorldLocation, Region->BakedGeometry.WaterPointVerticalToleranceCm);
			if (!CatWaterQueryPrivate::IsWater(Spatial)) continue;
			if (Match) { Result.Error = ECatWaterQueryError::AmbiguousRegion; return Result; }
			Match = Region;
		}
	}
	if (!Match) { Result.Error = ECatWaterQueryError::RegionNotFound; return Result; }
	Result.bSucceeded = true; Result.Error = ECatWaterQueryError::None; Result.Region = Match->MakeSnapshot();
	return Result;
}
