#include "Environment/CatChumFieldSettings.h"

// 打窝功能是否具备运行期开启条件；任一策划参数缺失/非法都会让整个功能保持关闭，而不是用非法值继续跑
bool UCatChumFieldSettings::IsRuntimeReady() const
{
	// 视线检测用的碰撞通道以整数存取，先转换出来供下面的范围校验使用
	const int32 Channel = static_cast<int32>(PlacementLineOfSightChannel.GetValue());
	return bEnableChumFieldRuntime && MaxActiveFieldsPerRegion > 0 // 总开关开启，且每个水域允许的活跃窝料场数量合法
		&& FMath::IsFinite(MaxRawContributionPerRegion) && MaxRawContributionPerRegion > 0.0 // 每水域窝料浓度预算合法
		&& FMath::IsFinite(MaxPlacementRangeCentimeters) && MaxPlacementRangeCentimeters > 0.0 // 最大投放距离合法
		&& FMath::IsFinite(MaxAimDeviationDegrees) && MaxAimDeviationDegrees > 0.0 && MaxAimDeviationDegrees <= 180.0 // 瞄准容差角度在合理范围内
		&& Channel >= 0 && Channel < static_cast<int32>(ECC_MAX) // 视线碰撞通道必须是引擎已知的合法枚举值
		&& FMath::IsFinite(ExpiredCleanupIntervalSeconds) && ExpiredCleanupIntervalSeconds > 0.0; // 过期清理定时器间隔合法
}
