#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"

struct FCatWaterRegionTestAccess
{
	static void InjectBakedGeometry(ACatWaterRegion& Region, const FCatWaterGeometryCache& Cache, int64 SourceDigest = 0)
	{
		Region.RegionId = Cache.Handle.RegionId;
		Region.WaterSurfaceZ = Cache.WaterSurfaceZ;
		Region.WaterPointVerticalToleranceCm = Cache.WaterPointVerticalToleranceCm;
		Region.BankHeightToleranceCm = Cache.BankHeightToleranceCm;
		Region.BoundaryToleranceCm = Cache.BoundaryToleranceCm;
		Region.MaxLandingCorrectionCm = Cache.MaxLandingCorrectionCm;
		Region.MinimumWaterInsetCm = Cache.MinimumWaterInsetCm;
		Region.MaxSampleSegmentLengthCm = Cache.MaxSampleSegmentLengthCm;
		Region.MaxChordErrorCm = Cache.MaxChordErrorCm;
		Region.BakedGeometry = Cache;
		Region.GeometryRevision = Cache.Handle.GeometryRevision;
		Region.BakedSourceDigest = SourceDigest != 0 ? SourceDigest : Cache.Handle.GeometryRevision;
		Region.bTrustInjectedGeometryForTests = true;
	}

	static const FCatWaterGeometryCache& GetBakedGeometry(const ACatWaterRegion& Region)
	{
		return Region.BakedGeometry;
	}

	static int64 GetBakedSourceDigest(const ACatWaterRegion& Region)
	{
		return Region.BakedSourceDigest;
	}
};

#endif
