#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"

// 配置校验流程：逐项检查本能力的有限数、范围与组合约束；返回结果，不修改资产或运行实例。
bool UCatEquipmentFragment_Rod::IsRuntimeReady() const
{
	// 锚点不得含 NaN、非单位旋转或零缩放，否则世界竿与鱼线计算会失去一致空间。
	const auto IsAnchorReady = [](const FTransform& Transform)
	{
		return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized()
			&& !Transform.GetScale3D().IsNearlyZero();
	};
	// 竿强度只校验结构合法（有限、非负）：0 是"未配置"这一有意义的内容态，由强度检查在使用点 fail-closed，
	// 与 UCatFishDefinition::ScoopTargetRadiusCentimeters 同一套口径；在这里硬性要求正值会让尚未补值的正式鱼竿整根不可用。
	return FMath::IsFinite(FishingStrength)
		&& FishingStrength >= 0.0
		&& FMath::IsFinite(MaximumRodDurability)
		&& MaximumRodDurability > 0.0
		&& FMath::IsFinite(MaximumLineLengthCentimeters)
		&& MaximumLineLengthCentimeters > 0.0
		&& FMath::IsFinite(RodPhysicsLengthCentimeters)
		&& RodPhysicsLengthCentimeters > 0.0
		&& FMath::IsFinite(BaseDurabilityWearPerSecond)
		&& BaseDurabilityWearPerSecond >= 0.0
		&& FMath::IsFinite(HighTensionWearMultiplier)
		&& HighTensionWearMultiplier >= 1.0
		&& IsAnchorReady(RodTipLocalTransform)
		&& IsAnchorReady(StandLocalTransform)
		&& IsAnchorReady(GripLocalTransform);
}
