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

// 臭气就绪流程：只认有限正时长。未配＝没有「请勿靠近」，吃鱼的经验、黄条与重毒倒地都照常走，
// 不把整条进食链拖下水——名册或时长缺一项只让这一个副作用不发生。
bool UCatConditionSettings::HasStench() const
{
	return bEnableConditionRuntime && FMath::IsFinite(StenchSeconds) && StenchSeconds > 0.0;
}

// 发臭鱼名册流程：逐条比 FishDefinitionId，不做任何名字推断（「含 Stinky 就算」这种猜法会连上臭臭鱼的
// 皮肤、变体一起误判）。名册空＝这条规则当前没有对象，调用方记一次 Warning 即可。
bool UCatConditionSettings::IsStenchFish(const FName FishDefinitionId) const
{
	return !FishDefinitionId.IsNone() && StenchFishDefinitionIds.Contains(FishDefinitionId);
}
