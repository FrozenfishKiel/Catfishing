#include "Equipment/CatEquipmentDefinition.h"

// 定义检查流程：验证总 gate、身份、类别与功能路线；装配类要求槽位，Rod 要正耐久，特殊饵必须是消耗品，Chum 还必须给出服务器读取的三轴增量。
bool UCatEquipmentDefinition::IsRuntimeDefinitionReady() const
{
	if (!bEnableRuntimeDefinition || EquipmentDefinitionId.IsNone() || Kind == ECatEquipmentKind::Unknown
		|| FunctionalRouteId.IsNone())
	{
		return false;
	}
	const bool bLoadoutKind = Kind == ECatEquipmentKind::Rod || Kind == ECatEquipmentKind::Bait
		|| Kind == ECatEquipmentKind::Float || Kind == ECatEquipmentKind::ScoopNet;
	if (bLoadoutKind && LoadoutSlotId.IsNone())
	{
		return false;
	}
	if (Kind == ECatEquipmentKind::Rod)
	{
		return !bRunConsumable && !bSpecialBait && FMath::IsFinite(MaximumRodDurability) && MaximumRodDurability > 0.0;
	}
	if (Kind == ECatEquipmentKind::Bait)
	{
		return FMath::IsNearlyZero(MaximumRodDurability) && bRunConsumable == bSpecialBait;
	}
	if (Kind == ECatEquipmentKind::Chum)
	{
		return bRunConsumable && !bSpecialBait && FMath::IsNearlyZero(MaximumRodDurability)
			&& ChumContribution.IsValidContribution();
	}
	return FMath::IsNearlyZero(MaximumRodDurability) && !bSpecialBait;
}
