#include "Inventory/CatInventorySettings.h"

#include "Equipment/CatEquipmentItemDefinition.h"
#include "Logging/CatLog.h"

// 全表读取流程：先验证表类型，再检查每行的编号、行名、定义和唯一性；全部成立后才发布按编号排序的结果。
// 失败时清空输出，避免坏行让不同系统只看到各自能解析的部分物品。
bool UCatInventorySettings::GetItemDefinitions(TArray<UCatInventoryItemDefinition*>& OutDefinitions, FString& OutError) const
{
	OutDefinitions.Reset();
	OutError.Reset();
	const UDataTable* Table = ItemCatalog.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FCatItemCatalogRow::StaticStruct() || Table->GetRowMap().IsEmpty())
	{
		OutError = TEXT("Item catalog is missing, empty, or has the wrong row type.");
		return false;
	}
	TSet<int32> SeenIds;
	TSet<FSoftObjectPath> SeenDefinitions;
	TArray<UCatInventoryItemDefinition*> Candidates;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCatItemCatalogRow& Row = *reinterpret_cast<const FCatItemCatalogRow*>(Pair.Value);
		UCatInventoryItemDefinition* Definition = Row.ItemDefinition.LoadSynchronous();
		if (Row.ItemId <= 0 || Pair.Key.ToString() != FString::FromInt(Row.ItemId)
			|| SeenIds.Contains(Row.ItemId) || SeenDefinitions.Contains(Row.ItemDefinition.ToSoftObjectPath())
			|| !Definition || Definition->ItemId != Row.ItemId)
		{
			OutError = FString::Printf(TEXT("Invalid item catalog row: %s"), *Pair.Key.ToString());
			return false;
		}
		SeenIds.Add(Row.ItemId);
		SeenDefinitions.Add(Row.ItemDefinition.ToSoftObjectPath());
		Candidates.Add(Definition);
	}
	Candidates.Sort([](const UCatInventoryItemDefinition& A, const UCatInventoryItemDefinition& B)
	{
		return A.ItemId < B.ItemId;
	});
	OutDefinitions = MoveTemp(Candidates);
	return true;
}

// 数字查询流程：拒绝无效编号，取得完整有效目录后只返回身份匹配且可运行的定义；没有任何旧目录回退。
UCatInventoryItemDefinition* UCatInventorySettings::FindRuntimeDefinition(const int32 ItemId) const
{
	if (ItemId <= 0) return nullptr;
	TArray<UCatInventoryItemDefinition*> Items;
	FString Error;
	if (!GetItemDefinitions(Items, Error))
	{
		UE_LOG(LogCatfishing, Error, TEXT("Event=item_catalog_rejected ItemId=%d Reason=%s"), ItemId, *Error);
		return nullptr;
	}
	for (UCatInventoryItemDefinition* Definition : Items)
	{
		if (Definition->ItemId == ItemId)
			return Definition->IsInventoryRuntimeDefinitionReady() ? Definition : nullptr;
	}
	return nullptr;
}

// 玩家随身容量读取流程：只把配置值夹到非负；InventorySettings 是随身库存容量的唯一配置源。
int32 UCatInventorySettings::GetPlayerInventorySlotCapacity() const
{
	return FMath::Max(0, PlayerInventorySlotCapacity);
}

// 默认堆叠配置读取流程：保留 0 作为“不设硬上限”的原始配置语义，调用方需要有效容量时走 GetDefaultQuantityStackLimit。
int32 UCatInventorySettings::GetDefaultQuantityStackCapacity() const
{
	return FMath::Max(0, DefaultQuantityStackCapacity);
}

// 默认堆叠上限读取流程：正数直接作为单格上限，0 统一提升为 MAX_int32，避免各调用方重复解释 0 的含义。
int32 UCatInventorySettings::GetDefaultQuantityStackLimit() const
{
	const int32 ConfiguredCapacity = GetDefaultQuantityStackCapacity();
	return ConfiguredCapacity > 0 ? ConfiguredCapacity : MAX_int32;
}

// 饵携带上限读取流程：与堆叠上限同一口径，0 表示这条规则没配、按 MAX_int32 处理，不会因为漏配就把饵全挡在背包外。
int32 UCatInventorySettings::GetBaitCarryLimit() const
{
	return BaitCarryLimit > 0 ? BaitCarryLimit : MAX_int32;
}

// 窝料携带上限读取流程：同上，两档各自独立，一个漏配不影响另一个。
int32 UCatInventorySettings::GetChumCarryLimit() const
{
	return ChumCarryLimit > 0 ? ChumCarryLimit : MAX_int32;
}

// 分类判定流程：只问装备定义已有的两个能力判定；不是装备定义、或两个能力都不成立时不受任何随身总量约束。
ECatInventoryCarryCategory UCatInventorySettings::ResolveCarryCategory(const UCatInventoryItemDefinition& ItemDefinition)
{
	const UCatEquipmentItemDefinition* Equipment = Cast<UCatEquipmentItemDefinition>(&ItemDefinition);
	if (Equipment == nullptr)
	{
		return ECatInventoryCarryCategory::None;
	}
	if (Equipment->CanServeFishingBait())
	{
		return ECatInventoryCarryCategory::Bait;
	}
	if (Equipment->CanServeChumPlacement())
	{
		return ECatInventoryCarryCategory::Chum;
	}
	return ECatInventoryCarryCategory::None;
}

// 分类上限读取流程：两档各读各的配置，None 分类恒为不限；返回值已经是「可直接参与比较的有效上限」。
int32 UCatInventorySettings::GetCarryLimitForCategory(const ECatInventoryCarryCategory Category) const
{
	switch (Category)
	{
	case ECatInventoryCarryCategory::Bait:
		return GetBaitCarryLimit();
	case ECatInventoryCarryCategory::Chum:
		return GetChumCarryLimit();
	default:
		return MAX_int32;
	}
}
