#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "Equipment/CatEquippedDefinition.h"
#include "AbilitySystem/Config/CatAbilitySet.h"

// 关联校验流程：检查装备资产、每个能力集及显式世界类能否加载；无世界实体的装备允许空类，不生成 Actor 或授予能力。
bool UCatEquippableItemFragment::IsRuntimeReady() const
{
	if (!IsValid(EquipmentDefinition)) return false;
	for (const auto& SetReference : EquipmentDefinition->AbilitySetsToGrant)
	{
		const auto* Set = SetReference.LoadSynchronous();
		if (!Set || !Set->IsRuntimeReady()) return false;
	}
	if (!EquipmentDefinition->ActorClass.IsNull())
	{
		const auto* ActorClass = EquipmentDefinition->ActorClass.LoadSynchronous();
		if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) return false;
	}
	return true;
}
