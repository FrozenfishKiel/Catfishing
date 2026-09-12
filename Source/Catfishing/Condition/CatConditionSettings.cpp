#include "Condition/CatConditionSettings.h"

// 运行 gate 流程：只认显式总开关。倒地与解除现在是布尔事实（重毒鱼吃下即倒地、救援/休息/自愈即起身），
// 不再需要任何中毒阈值；未开启时 ConditionComponent 仍复制 Wet，但不裁决倒地与疲惫档。
bool UCatConditionSettings::IsRuntimeReady() const
{
	return bEnableConditionRuntime;
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

// 自愈就绪流程：只认有限正值。未配时不起自愈计时，倒地仍可由救援、休息和翻天自动救起解除。
bool UCatConditionSettings::HasDownedSelfRecovery() const
{
	return bEnableConditionRuntime && FMath::IsFinite(DownedSelfRecoverySeconds) && DownedSelfRecoverySeconds > 0.0;
}
