#include "Equipment/Fragments/CatEquipmentFragment_Scoop.h"

// 配置校验流程：逐项检查本能力的有限数、范围与组合约束；返回结果，不修改资产或运行实例。
bool UCatEquipmentFragment_Scoop::IsRuntimeReady() const
{
	return FMath::IsFinite(ScoopReachCentimeters)
		&& ScoopReachCentimeters > 0.0;
}
