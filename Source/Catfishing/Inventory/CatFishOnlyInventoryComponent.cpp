#include "Inventory/CatFishOnlyInventoryComponent.h"

#include "Data/CatFishDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"

// 构造流程：鱼护/鱼缸仍是普通正式库存，只把接收规则收窄到鱼定义。
UCatFishOnlyInventoryComponent::UCatFishOnlyInventoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 实例接收流程：先沿用正式库存的槽位边界，再确认 incoming 实例绑定的是鱼定义。
bool UCatFishOnlyInventoryComponent::CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry,
	const int32 TargetSlotIndex) const
{
	const UCatInventoryItemDefinition* Definition =
		IncomingEntry.Instance != nullptr ? IncomingEntry.Instance->GetItemDefinition() : nullptr;
	return Super::CanAcceptInventoryEntryAtSlot(IncomingEntry, TargetSlotIndex)
		&& Cast<const UCatFishDefinition>(Definition) != nullptr;
}

// 定义接收流程：容量预演没有实例对象，因此直接检查定义类型并复用父类槽位边界。
bool UCatFishOnlyInventoryComponent::CanAcceptInventoryDefinitionAtSlot(
	const UCatInventoryItemDefinition& IncomingDefinition, const int32 TargetSlotIndex) const
{
	return Super::CanAcceptInventoryDefinitionAtSlot(IncomingDefinition, TargetSlotIndex)
		&& Cast<const UCatFishDefinition>(&IncomingDefinition) != nullptr;
}
