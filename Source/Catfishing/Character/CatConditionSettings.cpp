#include "Character/CatConditionSettings.h"

// 阈值检查流程：要求总 gate 与两项有限正值；未裁时 ConditionComponent 仍复制 Wet，但不会从数值推导倒地。
bool UCatConditionSettings::HasDownedThresholds() const
{
	return bEnableConditionRuntime && FMath::IsFinite(PoisonDownedThreshold) && PoisonDownedThreshold > 0.0
		&& FMath::IsFinite(FatigueDownedThreshold) && FatigueDownedThreshold > 0.0;
}
