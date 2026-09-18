#include "Inventory/CatFishOnlyInventoryComponent.h"

#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"

// 构造流程：鱼护和鱼缸沿用库存状态，仅按可组合的鱼类标签限制接收，不要求真实鱼子类。
UCatFishOnlyInventoryComponent::UCatFishOnlyInventoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 实例接收流程：先检查库存槽位边界，再从实例定义读取鱼类标签；假鱼可用普通实例入库，不伪造捕获数据。
bool UCatFishOnlyInventoryComponent::CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry,
	const int32 TargetSlotIndex) const
{
	const UCatInventoryItemDefinition* Definition =
		IncomingEntry.Instance != nullptr ? IncomingEntry.Instance->GetItemDefinition() : nullptr;
	return Super::CanAcceptInventoryEntryAtSlot(IncomingEntry, TargetSlotIndex)
		&& Definition && Definition->HasSemanticTag(CatItemTags::Fish);
}

// 容量预演和实际收货共用 Fish 分类契约；假鱼可以占格，但可售卖、可食用等能力仍由各自片段决定。
bool UCatFishOnlyInventoryComponent::CanAcceptInventoryDefinitionAtSlot(
	const UCatInventoryItemDefinition& IncomingDefinition, const int32 TargetSlotIndex) const
{
	return Super::CanAcceptInventoryDefinitionAtSlot(IncomingDefinition, TargetSlotIndex)
		&& IncomingDefinition.HasSemanticTag(CatItemTags::Fish);
}
