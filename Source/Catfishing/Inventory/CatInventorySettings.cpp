#include "Inventory/CatInventorySettings.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentDefinition.h"

// 目录项校验流程：稳定 ID 和定义资产必须互相对齐，避免无效 ID 被错误映射到另一种物品。
bool FCatInventoryCatalogDefinition::IsRuntimeReady() const
{
	UCatInventoryItemDefinition* Definition = ItemDefinition.LoadSynchronous();
	if (DefinitionId.IsNone() || Definition == nullptr || !Definition->IsInventoryRuntimeDefinitionReady())
	{
		return false;
	}

	return Definition->GetInventoryDefinitionId() == DefinitionId;
}

// 定义资产查找流程：只接受唯一且可运行的目录项；重复 ID 直接失败，防止不同机器解析出不同物品。
UCatInventoryItemDefinition* UCatInventorySettings::FindRuntimeDefinition(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return nullptr;
	}

	UCatInventoryItemDefinition* Match = nullptr;
	for (const FCatInventoryCatalogDefinition& Definition : Definitions)
	{
		if (Definition.DefinitionId != DefinitionId || !Definition.IsRuntimeReady())
		{
			continue;
		}

		if (Match != nullptr)
		{
			return nullptr;
		}

		Match = Definition.ItemDefinition.LoadSynchronous();
	}

	if (Match != nullptr)
	{
		return Match;
	}

	const UCatFishCatalogSettings* FishCatalog = GetDefault<UCatFishCatalogSettings>();
	return FishCatalog != nullptr ? FishCatalog->FindRuntimeDefinition(DefinitionId) : nullptr;
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
	const UCatEquipmentDefinition* Equipment = Cast<UCatEquipmentDefinition>(&ItemDefinition);
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
