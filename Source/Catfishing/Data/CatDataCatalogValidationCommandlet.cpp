#include "Data/CatDataCatalogValidationCommandlet.h"

#include "Data/CatDataCatalogValidation.h"
#include "Data/CatFishCatalogSettings.h"
#include "Equipment/CatEquipmentSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatDataCatalogValidationCommandlet, Log, All);

namespace CatDataCatalogValidationCommandlet
{
	// Issue 编码流程：优先使用枚举注册名输出稳定诊断；反射不可用时保留整数值，避免日志因显示名缺失而丢失机器码。
	static FString IssueCodeToLogString(const ECatDataCatalogIssueCode Code)
	{
		if (const UEnum* Enum = StaticEnum<ECatDataCatalogIssueCode>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Code));
		}
		return FString::Printf(TEXT("%d"), static_cast<int32>(Code));
	}

	// Issue 日志流程：逐条暴露目录、稳定 ID、字段和机器码；Message 只辅助人工定位，不参与任何运行分支。
	static void LogIssue(const FCatDataCatalogIssue& Issue)
	{
		UE_LOG(LogCatDataCatalogValidationCommandlet, Warning,
			TEXT("Event=data_catalog_validation_issue Catalog=%s StableId=%s Field=%s Code=%s Message=\"%s\""),
			*Issue.CatalogName.ToString(),
			*Issue.StableId.ToString(),
			*Issue.FieldName.ToString(),
			*IssueCodeToLogString(Issue.Code),
			*Issue.Message);
	}
}

// 构造流程：命令入口不需要 client/server world，只要求 Editor 上下文加载默认配置；退出码直接交给调用方，避免 CI 只能靠文本 grep 判断结果。
UCatDataCatalogValidationCommandlet::UCatDataCatalogValidationCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	UseCommandletResultAsExitCode = true;
	HelpDescription = TEXT("Validate Catfishing Fish and Equipment runtime data catalogs.");
	HelpUsage = TEXT("UnrealEditor-Cmd.exe Catfishing.uproject -run=CatDataCatalogValidation -Unattended -NullRHI");
}

// 执行流程：读取项目默认 Fish/Equipment DeveloperSettings，复用运行时统一 validator；失败时输出一条阻断错误和逐条
// issue，成功时输出内容包 Hash 供证据记录。
int32 UCatDataCatalogValidationCommandlet::Main(const FString& Params)
{
	(void)Params;

	const FCatDataCatalogValidationReport Report = FCatDataCatalogValidator::ValidateRuntimeCatalogs(
		GetDefault<UCatFishCatalogSettings>(),
		GetDefault<UCatEquipmentSettings>());
	if (Report.bValid)
	{
		UE_LOG(LogCatDataCatalogValidationCommandlet, Display,
			TEXT("Event=data_catalog_validation_succeeded ContentHash=%s CatalogCount=%d"),
			*Report.ContentHashHex,
			Report.Catalogs.Num());
		return 0;
	}

	UE_LOG(LogCatDataCatalogValidationCommandlet, Error,
		TEXT("Event=data_catalog_validation_failed ContentHash=%s IssueCount=%d CatalogCount=%d"),
		*Report.ContentHashHex,
		Report.Issues.Num(),
		Report.Catalogs.Num());
	for (const FCatDataCatalogIssue& Issue : Report.Issues)
	{
		CatDataCatalogValidationCommandlet::LogIssue(Issue);
	}
	return 1;
}