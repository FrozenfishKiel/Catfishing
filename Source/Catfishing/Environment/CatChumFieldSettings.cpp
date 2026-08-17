#include "Environment/CatChumFieldSettings.h"

bool UCatChumFieldSettings::IsRuntimeReady() const
{
	const int32 Channel = static_cast<int32>(PlacementLineOfSightChannel.GetValue());
	return bEnableChumFieldRuntime && MaxActiveFieldsPerRegion > 0
		&& FMath::IsFinite(MaxRawContributionPerRegion) && MaxRawContributionPerRegion > 0.0
		&& FMath::IsFinite(MaxPlacementRangeCentimeters) && MaxPlacementRangeCentimeters > 0.0
		&& FMath::IsFinite(MaxAimDeviationDegrees) && MaxAimDeviationDegrees > 0.0 && MaxAimDeviationDegrees <= 180.0
		&& Channel >= 0 && Channel < static_cast<int32>(ECC_MAX)
		&& FMath::IsFinite(ExpiredCleanupIntervalSeconds) && ExpiredCleanupIntervalSeconds > 0.0;
}
