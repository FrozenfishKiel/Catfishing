#include "Data/CatFishDataStatics.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"

// 鱼定义查询流程：直接转发目录的唯一查询入口；目录本身已对重复 ID 与未启用资产 fail-closed。
UCatFishDefinition* UCatFishDataStatics::FindFishDefinition(const FName FishDefinitionId)
{
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	return Catalog && !FishDefinitionId.IsNone() ? Catalog->FindRuntimeDefinition(FishDefinitionId) : nullptr;
}

// 表现定义查询流程：先解析鱼种，再沿鱼定义唯一的表现引用取资产；不按 ID 维护第二张表现表。
UCatFishPresentationDefinition* UCatFishDataStatics::FindFishPresentationDefinition(const FName FishDefinitionId)
{
	const UCatFishDefinition* Fish = FindFishDefinition(FishDefinitionId);
	return Fish ? Fish->LoadRuntimePresentationDefinition() : nullptr;
}
