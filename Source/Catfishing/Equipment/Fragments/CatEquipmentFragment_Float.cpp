#include "Equipment/Fragments/CatEquipmentFragment_Float.h"

// 配置校验流程：逐项检查本能力的有限数、范围与组合约束；返回结果，不修改资产或运行实例。
bool UCatEquipmentFragment_Float::IsRuntimeReady() const
{
	return FMath::IsFinite(MaximumCastDistanceCentimeters)
		&& MaximumCastDistanceCentimeters > 0.0
		&& FMath::IsFinite(CastErrorStandardDeviationCentimeters)
		&& CastErrorStandardDeviationCentimeters >= 0.0
		&& FMath::IsFinite(MaximumCastErrorRadiusCentimeters)
		&& MaximumCastErrorRadiusCentimeters >= 0.0
		&& CastErrorStandardDeviationCentimeters <= MaximumCastErrorRadiusCentimeters
		&& FMath::IsFinite(BiteSignalStability)
		&& BiteSignalStability >= 0.0
		&& BiteSignalStability <= 1.0;
}
