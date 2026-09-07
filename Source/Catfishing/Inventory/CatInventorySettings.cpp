#include "Inventory/CatInventorySettings.h"

// 目录项校验流程：稳定 ID 和定义资产必须互相对齐，避免旧 ID 被错误映射到另一种物品。
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

	return Match;
}

// 玩家随身容量读取流程：只把配置值夹到非负；旧 Equipment 覆盖逻辑留在迁移适配层，库存设置本身不反向依赖 Equipment。
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
