#include "Equipment/CatEquipmentSettings.h"

#include "Equipment/CatEquipmentDefinition.h"

// 定义查询流程：同步解析显式清单并只接受唯一完整 ID；重复定义返回空，防止客户端与服务器选到不同内容。
UCatEquipmentDefinition* UCatEquipmentSettings::FindRuntimeDefinition(const FName DefinitionId) const
{
	UCatEquipmentDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatEquipmentDefinition>& Ref : Definitions)
	{
		UCatEquipmentDefinition* Definition = Ref.LoadSynchronous();
		if (!Definition || !Definition->IsRuntimeDefinitionReady() || Definition->EquipmentDefinitionId != DefinitionId)
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = Definition;
	}
	return Match;
}
