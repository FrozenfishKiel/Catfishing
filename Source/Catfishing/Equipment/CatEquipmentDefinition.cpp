#include "Equipment/CatEquipmentDefinition.h"

// 定义检查流程：验证总 gate、身份、类别与功能路线；装配类要求槽位，Rod 还要声明 UseActorClass，所有 Bait 都必须是局内耗材，Chum 还必须给出服务器读取的三轴增量。
bool UCatEquipmentDefinition::IsRuntimeDefinitionReady() const
{
	const auto IsFiniteTransform = [](const FTransform& Transform)
	{
		return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized()
			&& !Transform.GetScale3D().IsNearlyZero();
	};
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
		return !bRunConsumable && !bSpecialBait && !UseActorClass.IsNull()
			&& FMath::IsFinite(MaximumRodDurability) && MaximumRodDurability > 0.0
			&& FMath::IsFinite(FishingStrength) && FishingStrength > 0.0
			&& FMath::IsFinite(MaximumLineLengthCentimeters) && MaximumLineLengthCentimeters > 0.0
			&& FMath::IsFinite(RodPhysicsLengthCentimeters) && RodPhysicsLengthCentimeters > 0.0
			&& FMath::IsFinite(BaseDurabilityWearPerSecond) && BaseDurabilityWearPerSecond >= 0.0
			&& FMath::IsFinite(HighTensionWearMultiplier) && HighTensionWearMultiplier >= 1.0
			&& IsFiniteTransform(RodTipLocalTransform) && IsFiniteTransform(StandLocalTransform)
			&& IsFiniteTransform(GripLocalTransform) && ChumInfluence.IsUnconfigured();
	}
	if (Kind == ECatEquipmentKind::Bait)
	{
		return FMath::IsNearlyZero(MaximumRodDurability) && bRunConsumable
			&& FMath::IsFinite(BiteRateMultiplier) && BiteRateMultiplier > 0.0
			&& FMath::IsFinite(MinimumBiteDelayMultiplier) && MinimumBiteDelayMultiplier > 0.0
			&& ChumInfluence.IsUnconfigured();
	}
	if (Kind == ECatEquipmentKind::Float)
	{
		return FMath::IsNearlyZero(MaximumRodDurability) && !bRunConsumable && !bSpecialBait
			&& FMath::IsFinite(MaximumCastDistanceCentimeters) && MaximumCastDistanceCentimeters > 0.0
			&& FMath::IsFinite(CastErrorStandardDeviationCentimeters) && CastErrorStandardDeviationCentimeters >= 0.0
			&& FMath::IsFinite(MaximumCastErrorRadiusCentimeters) && MaximumCastErrorRadiusCentimeters >= 0.0
			&& CastErrorStandardDeviationCentimeters <= MaximumCastErrorRadiusCentimeters
			&& FMath::IsFinite(BiteSignalStability) && BiteSignalStability >= 0.0 && BiteSignalStability <= 1.0
			&& ChumInfluence.IsUnconfigured();
	}
	if (Kind == ECatEquipmentKind::ScoopNet)
	{
		return FMath::IsNearlyZero(MaximumRodDurability) && !bRunConsumable && !bSpecialBait
			&& FMath::IsFinite(ScoopReachCentimeters) && ScoopReachCentimeters > 0.0
			&& ChumInfluence.IsUnconfigured();
	}
	if (Kind == ECatEquipmentKind::Chum)
	{
		return bRunConsumable && !bSpecialBait && FMath::IsNearlyZero(MaximumRodDurability)
			&& ChumInfluence.IsRuntimeReady();
	}
	return FMath::IsNearlyZero(MaximumRodDurability) && !bSpecialBait && ChumInfluence.IsUnconfigured();
}
