#include "Equipment/CatEquipmentDefinition.h"

// 定义检查流程：验证总 gate、身份、类别、功能路线和 Use 库存影响策略；装配类要求槽位，部署型要求 Actor 类，Rod 还要具备耐久和三组锚点，Chum 还必须给出服务器读取的三轴增量。
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
	const ECatEquipmentUseInventoryEffect ResolvedUseInventoryEffect = GetUseInventoryEffect();
	if (ResolvedUseInventoryEffect == ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse
		&& (UseActorClass.IsNull() || bRunConsumable))
	{
		return false;
	}
	if (ResolvedUseInventoryEffect == ECatEquipmentUseInventoryEffect::ConsumeQuantity && !bRunConsumable)
	{
		return false;
	}
	if (Kind == ECatEquipmentKind::Rod)
	{
		return !bRunConsumable && !bSpecialBait && !UseActorClass.IsNull()
			&& ResolvedUseInventoryEffect == ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse
			&& FMath::IsFinite(MaximumRodDurability) && MaximumRodDurability > 0.0
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
			&& ResolvedUseInventoryEffect == ECatEquipmentUseInventoryEffect::ConsumeQuantity
			&& ChumInfluence.IsRuntimeReady();
	}
	if (Kind == ECatEquipmentKind::Herb)
	{
		return bRunConsumable && !bSpecialBait && FMath::IsNearlyZero(MaximumRodDurability)
			&& ResolvedUseInventoryEffect == ECatEquipmentUseInventoryEffect::ConsumeQuantity
			&& ChumInfluence.IsUnconfigured();
	}
	return FMath::IsNearlyZero(MaximumRodDurability) && !bSpecialBait && ChumInfluence.IsUnconfigured();
}

ECatDomainCommandError UCatEquipmentDefinition::Use(const FCatRunInventorySlot& Item, const int32 Quantity) const
{
	// 物品定义侧 Use 裁决流程：
	// 1. 先确认调用方传入的是这份定义对应的真实库存实例，避免统一入口按 DefinitionId 误用同类物品。
	// 2. 未声明任何库存影响的物品返回 no-op 终态；调用方可以统一调用 Use，但不会移动背包格子。
	// 3. 部署型物品必须是一格一件的非数量物；数量消耗物必须是数量物，且不能超过当前实例堆栈。
	// 4. Rod 的断竿和耐久检查属于物品自身规则，放在定义对象里，避免 Equipment 事务入口继续写鱼竿特判。
	if (Item.DefinitionId.IsNone() || Item.Quantity <= 0 || !Item.ItemInstanceId.IsValid()
		|| Quantity <= 0 || Quantity > Item.Quantity
		|| (!EquipmentDefinitionId.IsNone() && Item.DefinitionId != EquipmentDefinitionId))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const ECatEquipmentUseInventoryEffect InventoryEffect = GetUseInventoryEffect();
	if (InventoryEffect == ECatEquipmentUseInventoryEffect::None)
	{
		return ECatDomainCommandError::AlreadyResolved;
	}
	if (InventoryEffect == ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse
		&& (UseActorClass.IsNull() || bRunConsumable || Item.Quantity != 1 || Quantity != 1))
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (InventoryEffect == ECatEquipmentUseInventoryEffect::ConsumeQuantity && !bRunConsumable)
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (Kind == ECatEquipmentKind::Rod
		&& (Item.bRodBroken || !FMath::IsFinite(Item.RodDurability) || Item.RodDurability <= 0.0))
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	return ECatDomainCommandError::None;
}

ECatDomainCommandError UCatEquipmentDefinition::UnUse(const FCatRunInventorySlot& Item) const
{
	// 物品定义侧 UnUse 裁决流程：
	// 1. 先确认活动记录仍然指向一份有效实例，防止收口时按定义重造或归还空物品。
	// 2. 再校验实例定义没有和当前定义资产错位；错位代表坏数据，必须让 Equipment 保持使用态供上层处理。
	// 3. 不再要求当前资产仍配置 UseActorClass，因为运行中热改数据时也要允许旧活动记录把实例安全放回背包。
	if (Item.DefinitionId.IsNone() || Item.Quantity <= 0 || !Item.ItemInstanceId.IsValid()
		|| (!EquipmentDefinitionId.IsNone() && Item.DefinitionId != EquipmentDefinitionId))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return ECatDomainCommandError::None;
}

bool UCatEquipmentDefinition::KeepsInventoryInstanceWhileUsed() const
{
	// 使用持有策略读取流程：只把“世界对象暂时代替背包实例”的模式交给活动记录，其余效果由自己的库存影响策略继续区分。
	return GetUseInventoryEffect() == ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
}

bool UCatEquipmentDefinition::ConsumesInventoryQuantityOnUse() const
{
	// 数量消耗策略读取流程：只有声明为 Use 后扣数量的定义返回 true；调用方必须先完成本物品自己的目标、距离和效果前置裁决。
	return GetUseInventoryEffect() == ECatEquipmentUseInventoryEffect::ConsumeQuantity;
}

ECatEquipmentUseInventoryEffect UCatEquipmentDefinition::GetUseInventoryEffect() const
{
	// 库存影响策略解析流程：
	// 1. 先尊重定义资产显式声明，后续新物品只改自己的数据或定义子类，不改 Equipment 入口。
	// 2. 旧部署型资产在新增字段前已经通过 UseActorClass 表达部署行为，因此这里保留兼容推导，避免一次数据迁移阻塞运行。
	// 3. 窝料和草药都在各自玩法 preflight 成功后通过 Use 扣库存数量；鱼饵仍由 Fishing 会话预算独立提交。
	if (UseInventoryEffect != ECatEquipmentUseInventoryEffect::Auto)
	{
		return UseInventoryEffect;
	}
	if ((Kind == ECatEquipmentKind::Chum || Kind == ECatEquipmentKind::Herb) && bRunConsumable)
	{
		return ECatEquipmentUseInventoryEffect::ConsumeQuantity;
	}
	return !UseActorClass.IsNull()
		? ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse
		: ECatEquipmentUseInventoryEffect::None;
}
