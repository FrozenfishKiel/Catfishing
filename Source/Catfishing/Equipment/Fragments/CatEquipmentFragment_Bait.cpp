#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"

// 鱼饵配置校验流程：两个倍率都必须是有限正数；特殊饵标记不改变数值边界，查询不修改资产或运行实例。
bool UCatEquipmentFragment_Bait::IsRuntimeReady() const
{
	return FMath::IsFinite(BiteRateMultiplier)
		&& BiteRateMultiplier > 0.0
		&& FMath::IsFinite(MinimumBiteDelayMultiplier)
		&& MinimumBiteDelayMultiplier > 0.0;
}
