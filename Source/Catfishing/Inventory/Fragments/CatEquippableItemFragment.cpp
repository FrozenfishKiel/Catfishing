#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "Equipment/CatEquippedDefinition.h"

// 关联校验流程：片段只确认装备资产存在，具体能力与表现由装备定义和对应行为校验，不执行运行副作用。
bool UCatEquippableItemFragment::IsRuntimeReady() const
{
	return IsValid(EquipmentDefinition);
}
