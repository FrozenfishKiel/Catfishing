#include "Equipment/Fragments/CatEquipmentFragment_Scoop.h"

// 配置校验流程：逐项检查本能力的有限数、范围与组合约束；返回结果，不修改资产或运行实例。
bool UCatEquipmentFragment_Scoop::IsRuntimeReady() const
{
	// 墓碑（2026-09-14，T15；钓鱼规则 §5.1）：单款抄网无长度属性；旧字段仅保留资产反序列化。
	return true;
}
