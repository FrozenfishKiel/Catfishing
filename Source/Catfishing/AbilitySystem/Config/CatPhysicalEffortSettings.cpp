#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"

bool UCatPhysicalEffortSettings::IsValid() const
{
	return FMath::IsFinite(StaminaPerUnfulfilledMeter) && StaminaPerUnfulfilledMeter >= 0
		&& FMath::IsFinite(SupportReferenceSpeedCmS) && SupportReferenceSpeedCmS >= 0
		&& FMath::IsFinite(RecoveryPerSecond) && RecoveryPerSecond >= 0
		&& FMath::IsFinite(FishingForwardMoveStaminaPerSecond) && FishingForwardMoveStaminaPerSecond >= 0
		&& FMath::IsFinite(FishingBackwardMoveStaminaPerSecond) && FishingBackwardMoveStaminaPerSecond >= 0;
}
