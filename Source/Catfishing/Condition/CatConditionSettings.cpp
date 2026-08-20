#include "Condition/CatConditionSettings.h"

// 阈值检查流程：要求总 gate 与 Poison 有限正值；未裁时 ConditionComponent 仍复制 Wet，但不会从数值推导倒地，三条恢复
// 路径也会因此拒绝。倒地来源仅中毒。
bool UCatConditionSettings::HasDownedThresholds() const
{
	return bEnableConditionRuntime && FMath::IsFinite(PoisonDownedThreshold) && PoisonDownedThreshold > 0.0;
}
