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
