#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"

// 窝料校验流程：复用水域领域的完整规格检查；查询只返回就绪结果，不创建影响或扣除库存。
bool UCatEquipmentFragment_Chum::IsRuntimeReady() const
{
	return ChumInfluence.IsRuntimeReady();
}
