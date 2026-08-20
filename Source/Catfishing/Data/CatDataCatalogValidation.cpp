#include "Data/CatDataCatalogValidation.h"

#include "Data/CatFishCatalogSettings.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Hash/Blake3.h"

namespace CatDataCatalogValidation
{
	// 文本编码流程：合并报告只组合各目录已经声明的内容摘要，不重新扫描资产或外部资料。
	static void AppendHashLine(FString& Text, const FString& Line)
	{
		Text += Line;
		Text += TEXT("\n");
	}

	// 缺失目录记录流程：把缺少 Settings 指针也表达成普通 catalog issue，方便 CI 用同一套问题列表显示。
	static void AddMissingCatalogIssue(FCatDataCatalogValidationReport& Report, const FName CatalogName)
	{
		FCatDataCatalogIssue& Issue = Report.Issues.AddDefaulted_GetRef();
		Issue.CatalogName = CatalogName;
		Issue.FieldName = TEXT("Settings");
		Issue.Code = ECatDataCatalogIssueCode::MissingDefinition;
		Issue.Message = FString::Printf(TEXT("%s settings are required for runtime catalog validation."), *CatalogName.ToString());
	}

	// 单目录合并流程：保留原始目录结果，同时把 issues 提升到报告顶层，避免调用方漏看某个目录。
	static void AddCatalogResult(FCatDataCatalogValidationReport& Report, const FCatDataCatalogValidationResult& Result)
	{
		Report.Catalogs.Add(Result);
		Report.Issues.Append(Result.Issues);
	}

	// 报告摘要流程：按固定目录顺序写入各目录 ContentHash；它描述内容包身份，不把校验错误消息写进摘要。
	static FString ComputeReportContentHashHex(const FCatDataCatalogValidationReport& Report)
	{
		FString Canonical;
		AppendHashLine(Canonical, TEXT("catalog-report:work01"));
		for (const FCatDataCatalogValidationResult& Catalog : Report.Catalogs)
		{
			AppendHashLine(Canonical, FString::Printf(TEXT("schema:%d"), Catalog.SchemaVersion));
			AppendHashLine(Canonical, FString::Printf(TEXT("revision:%lld"), static_cast<long long>(Catalog.DataRevision)));
			AppendHashLine(Canonical, FString::Printf(TEXT("hash:%s"), *Catalog.ContentHashHex));
		}
		FTCHARToUTF8 Utf8(*Canonical);
		return LexToString(FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())));
	}
}

// 合并校验流程：先校验 Equipment，再用同一 Equipment 目录校验 Fish 的特殊饵引用；任一目录缺失或非法都让报告 fail-closed。
FCatDataCatalogValidationReport FCatDataCatalogValidator::ValidateRuntimeCatalogs(
	const UCatFishCatalogSettings* FishCatalog,
	const UCatEquipmentSettings* EquipmentCatalog)
{
	FCatDataCatalogValidationReport Report;
	if (EquipmentCatalog)
	{
		CatDataCatalogValidation::AddCatalogResult(Report, EquipmentCatalog->ValidateRuntimeCatalog());
	}
	else
	{
		CatDataCatalogValidation::AddMissingCatalogIssue(Report, TEXT("EquipmentCatalog"));
	}

	if (FishCatalog)
	{
		CatDataCatalogValidation::AddCatalogResult(Report, FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog));
	}
	else
	{
		CatDataCatalogValidation::AddMissingCatalogIssue(Report, TEXT("FishCatalog"));
	}

	Report.bValid = Report.Issues.IsEmpty() && Report.Catalogs.Num() == 2;
	Report.ContentHashHex = CatDataCatalogValidation::ComputeReportContentHashHex(Report);
	return Report;
}
