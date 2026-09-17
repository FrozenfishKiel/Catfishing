#include "Online/Voice/CatVoiceSettings.h"

bool UCatVoiceSettings::IsRangeValid() const
{
	return FMath::IsFinite(FullVolumeDistanceCm) && FMath::IsFinite(SilentDistanceCm)
		&& FullVolumeDistanceCm >= 0.0f && SilentDistanceCm > FullVolumeDistanceCm;
}

float UCatVoiceSettings::GetGainForDistance(const double DistanceCm) const
{
	if (!IsRangeValid() || !FMath::IsFinite(DistanceCm) || DistanceCm < 0.0) return 0.0f;
	return static_cast<float>(FMath::Clamp((SilentDistanceCm - DistanceCm)
		/ (SilentDistanceCm - FullVolumeDistanceCm), 0.0, 1.0));
}
