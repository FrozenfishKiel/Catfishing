#include "Condition/CatConditionSettings.h"

// 阈值检查流程：要求总 gate 与 Poison 有限正值；未裁时 ConditionComponent 仍复制 Wet，但不会从数值推导倒地。
bool UCatConditionSettings::HasDownedThresholds() const
{
	return bEnableConditionRuntime && FMath::IsFinite(PoisonDownedThreshold) && PoisonDownedThreshold > 0.0;
}

bool UCatConditionSettings::HasWaterExposureThresholds() const
{
	return bEnableConditionRuntime
		&& FMath::IsFinite(WetWaterDepthCentimeters) && WetWaterDepthCentimeters >= 0.0
		&& FMath::IsFinite(DangerousWaterDepthCentimeters) && DangerousWaterDepthCentimeters > 0.0
		&& FMath::IsFinite(DangerousWaterExitDepthCentimeters) && DangerousWaterExitDepthCentimeters >= 0.0
		&& DangerousWaterExitDepthCentimeters < DangerousWaterDepthCentimeters
		&& FMath::IsFinite(DangerousWaterConfirmationSeconds) && DangerousWaterConfirmationSeconds >= 0.0;
}
