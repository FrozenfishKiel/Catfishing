#include "Condition/CatConditionSettings.h"

// 水域阈值检查流程：要求总 gate、有限水深和进入/退出滞回关系同时成立；不满足时 Condition 不发布湿身或危险水域变化。
bool UCatConditionSettings::HasWaterExposureThresholds() const
{
	return bEnableConditionRuntime
		&& FMath::IsFinite(WetWaterDepthCentimeters) && WetWaterDepthCentimeters >= 0.0
		&& FMath::IsFinite(DangerousWaterDepthCentimeters) && DangerousWaterDepthCentimeters > 0.0
		&& FMath::IsFinite(DangerousWaterExitDepthCentimeters) && DangerousWaterExitDepthCentimeters >= 0.0
		&& DangerousWaterExitDepthCentimeters < DangerousWaterDepthCentimeters
		&& FMath::IsFinite(DangerousWaterConfirmationSeconds) && DangerousWaterConfirmationSeconds >= 0.0;
}

// 配置查询只返回显式开关，不恢复已经删除的治疗和食用效果条件。
bool UCatConditionSettings::IsRuntimeReady() const
{
	return bEnableConditionRuntime;
}
