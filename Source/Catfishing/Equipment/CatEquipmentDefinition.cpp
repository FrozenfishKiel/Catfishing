#include "Equipment/CatEquipmentDefinition.h"

// 定义检查流程：验证总 gate、身份、类别与功能路线；装配类要求槽位，Rod 要正耐久、正强度、正放线上限且不是消耗品（强度
// 与 L_max 是遛鱼判定的必需输入，缺一根竿就没法进搏斗），Float 要正射程与正精准度偏移半径（这两项决定浮漂落在哪儿，
// 缺一项就算不出落点和 D₀），Bait 不论普通/特殊都必须是一局消耗品（飞书 §3.4：咬钩后无论结局都扣 1 份饵），
// Chum 还必须给出服务器读取的三轴增量；非 Rod 的三个竿字段与非 Float 的两个漂字段都必须是 0；
// 旧 Herb/Driftwood 保留反射值但不进入运行目录。
bool UCatEquipmentDefinition::IsRuntimeDefinitionReady() const
{
	if (!bEnableRuntimeDefinition || EquipmentDefinitionId.IsNone() || Kind == ECatEquipmentKind::Unknown
		|| FunctionalRouteId.IsNone())
	{
		return false;
	}
	if (Kind == ECatEquipmentKind::Herb || Kind == ECatEquipmentKind::Driftwood)
	{
		return false;
	}
	const bool bLoadoutKind = Kind == ECatEquipmentKind::Rod || Kind == ECatEquipmentKind::Bait
		|| Kind == ECatEquipmentKind::Float || Kind == ECatEquipmentKind::ScoopNet;
	if (bLoadoutKind && LoadoutSlotId.IsNone())
	{
		return false;
	}
	const bool bFloatFieldsZero = FMath::IsNearlyZero(FloatCastRangeMeters)
		&& FMath::IsNearlyZero(FloatAccuracyOffsetRadiusMeters);
	if (Kind == ECatEquipmentKind::Rod)
	{
		return !bRunConsumable && !bSpecialBait && bFloatFieldsZero
			&& FMath::IsFinite(MaximumRodDurability) && MaximumRodDurability > 0.0
			&& FMath::IsFinite(RodStrength) && RodStrength > 0.0
			&& FMath::IsFinite(MaximumLineLengthMeters) && MaximumLineLengthMeters > 0.0;
	}
	const bool bRodFieldsZero = FMath::IsNearlyZero(MaximumRodDurability) && FMath::IsNearlyZero(RodStrength)
		&& FMath::IsNearlyZero(MaximumLineLengthMeters);
	if (Kind == ECatEquipmentKind::Float)
	{
		return !bRunConsumable && !bSpecialBait && bRodFieldsZero
			&& FMath::IsFinite(FloatCastRangeMeters) && FloatCastRangeMeters > 0.0
			&& FMath::IsFinite(FloatAccuracyOffsetRadiusMeters) && FloatAccuracyOffsetRadiusMeters > 0.0;
	}
	if (Kind == ECatEquipmentKind::Bait)
	{
		return bRodFieldsZero && bFloatFieldsZero && bRunConsumable;
	}
	if (Kind == ECatEquipmentKind::Chum)
	{
		return bRunConsumable && !bSpecialBait && bRodFieldsZero && bFloatFieldsZero
			&& ChumContribution.IsValidContribution();
	}
	return bRodFieldsZero && bFloatFieldsZero && !bSpecialBait;
}
