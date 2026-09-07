#include "Inventory/CatInventorySettings.h"

#include "Inventory/CatInventoryItemInstance.h"

// 目录项校验流程：稳定 ID、定义类、实例类和定义默认对象必须互相对齐，避免旧 ID 被错误映射到另一种物品。
bool FCatInventoryCatalogDefinition::IsRuntimeReady() const
{
	if (DefinitionId.IsNone() || ItemDefinitionClass == nullptr
		|| UCatInventoryItemDefinition::ResolveItemInstanceClass(ItemDefinitionClass) == nullptr)
	{
		return false;
	}

	const UCatInventoryItemDefinition* ItemDefinition =
		GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass);
	return ItemDefinition != nullptr && ItemDefinition->ItemDefinitionId == DefinitionId;
}

// 定义类查找流程：只接受唯一且可运行的目录项；重复 ID 直接失败，防止不同机器解析出不同物品。
TSubclassOf<UCatInventoryItemDefinition> UCatInventorySettings::FindRuntimeDefinitionClass(
	const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return nullptr;
	}

	TSubclassOf<UCatInventoryItemDefinition> Match = nullptr;
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

		Match = Definition.ItemDefinitionClass;
	}

	return Match;
}

// 目录返回类默认对象作为静态配置；复用唯一类查找，避免重复 ID 在不同调用点解析出不同物品。
const UCatInventoryItemDefinition* UCatInventorySettings::FindRuntimeDefinition(
	const FName DefinitionId) const
{
	const TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass =
		FindRuntimeDefinitionClass(DefinitionId);
	return ItemDefinitionClass != nullptr
		? GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass) : nullptr;
}
