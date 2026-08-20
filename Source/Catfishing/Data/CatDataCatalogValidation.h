#pragma once

#include "CoreMinimal.h"
#include "Data/CatDataCatalogTypes.h"

class UCatEquipmentSettings;
class UCatFishCatalogSettings;

/** WORK-01 正式内容包校验入口；它把 Fish 与 Equipment 目录作为一个可运行数据包审查，而不是让调用方各查各的。 */
class CATFISHING_API FCatDataCatalogValidator
{
public:
	/** 同时校验运行时必需的 Fish 与 Equipment 目录；返回合并问题、单目录结果和内容包摘要，供 Editor/CI/Automation 使用。 */
	static FCatDataCatalogValidationReport ValidateRuntimeCatalogs(
		const UCatFishCatalogSettings* FishCatalog,
		const UCatEquipmentSettings* EquipmentCatalog);
};
