#pragma once

#include "CoreMinimal.h"
#include "CatDataCatalogTypes.generated.h"

/** 内容目录校验发现的稳定问题类别；Editor、CI 和运行时入口用它做 fail-closed 判断，具体领域仍保留自己的诊断语义。 */
UENUM(BlueprintType)
enum class ECatDataCatalogIssueCode : uint8
{
	/** 没有问题；只作为默认值存在，正常校验结果不会把它写进 Issues。 */
	None,
	/** 目录声明的 SchemaVersion 与当前代码支持的版本不一致。 */
	UnsupportedSchemaVersion,
	/** 显式目录里有空软引用、加载失败或缺失资源。 */
	MissingDefinition,
	/** 目录没有记录可追溯的人工落盘或离线导入来源。 */
	MissingSource,
	/** 单条 Definition readiness 没有通过，不能进入运行时目录。 */
	InvalidDefinition,
	/** 同一个稳定 ID 在同一目录中出现超过一次。 */
	DuplicateStableId,
	/** Definition 引用了不存在或类别不匹配的其他稳定 ID。 */
	InvalidReference
};

/** 内容目录的一条校验问题；保留 Catalog、StableId 和字段名，方便编辑器日志、自动化和人工修表定位。 */
USTRUCT(BlueprintType)
struct FCatDataCatalogIssue
{
	GENERATED_BODY()

	/** 问题所属的目录名，例如 FishCatalog 或 EquipmentCatalog；合并多个目录报告时用它定位来源。 */
	UPROPERTY(BlueprintReadOnly)
	FName CatalogName = NAME_None;

	/** 问题关联的稳定 ID；缺失定义或无法解析到具体条目时保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName StableId = NAME_None;

	/** 问题关联的字段或引用类别，例如 Definitions、PreferredSpecialBaitIds 或 DriftwoodDefinitionId。 */
	UPROPERTY(BlueprintReadOnly)
	FName FieldName = NAME_None;

	/** 机器可判断的问题类别；Review、Automation 和 CI 读这个字段，而不是解析人工说明。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDataCatalogIssueCode Code = ECatDataCatalogIssueCode::None;

	/** 给人工修数据看的简短说明；它不作为程序分支依据。 */
	UPROPERTY(BlueprintReadOnly)
	FString Message;
};

/** 内容目录的来源戳；它记录人工落盘或离线导入依据，但不授权运行时回读飞书、文件夹或 Content 扫描。 */
USTRUCT(BlueprintType)
struct FCatDataCatalogSourceStamp
{
	GENERATED_BODY()

	/** 来源类型表示这批目录来自哪类权威资料；CI 用它区分 FeishuSheet、FeishuDoc 或 ManualLanding 等入口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName SourceKind = NAME_None;

	/** 来源节点或文档的稳定标识；人工验收可据此回到原始资料，运行时代码不会用它发起网络读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString SourceNodeToken;

	/** 落盘时观察到的来源修订；它证明目录不是无来源手写常量，内容身份仍由 DataRevision 和 ContentHash 承担。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int64 SourceRevision = 0;

	/** 来源内部的表页或章节名；鱼表这类多 Sheet 资料用它排除已废弃页面，普通文档可以留空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString SourceSliceName;
};

/** 内容目录的统一校验结果；运行时只在 bValid 为 true 时消费目录，ContentHash 绑定当前显式内容。 */
USTRUCT(BlueprintType)
struct FCatDataCatalogValidationResult
{
	GENERATED_BODY()

	/** 目录是否能进入运行时；只有 Issues 为空且版本受当前代码支持时才为 true。 */
	UPROPERTY(BlueprintReadOnly)
	bool bValid = false;

	/** 目录声明并参与 ContentHash 的 SchemaVersion；代码只接受当前支持版本。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SchemaVersion = 1;

	/** 数据人员或导入流程声明的内容修订；它参与 ContentHash 和 EncounterSpec 证据。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DataRevision = 0;

	/** 本目录声明的来源戳；校验会要求关键字段完整，但 ContentHash 不依赖外部文档元数据。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDataCatalogSourceStamp SourceStamp;

	/** 当前显式目录内容的 Blake3 十六进制摘要；空目录或非法目录也会返回可诊断摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FString ContentHashHex;

	/** 本次校验发现的全部问题；运行路径不得选择性忽略其中一部分继续成功。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatDataCatalogIssue> Issues;
};

/** 多个正式内容目录的合并校验报告；CI/Editor 用它一次性判断 Fish 和 Equipment 是否属于可运行内容包。 */
USTRUCT(BlueprintType)
struct FCatDataCatalogValidationReport
{
	GENERATED_BODY()

	/** 所有必需目录是否整体可运行；只要任一目录缺失、非法或来源不可追溯，就保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bValid = false;

	/** 合并报告的稳定摘要；它由各目录 ContentHash 组合而成，用于记录本次验证看到的内容包身份。 */
	UPROPERTY(BlueprintReadOnly)
	FString ContentHashHex;

	/** 每个目录自己的校验结果；Review 可以从这里看到单目录 Revision、Hash 和来源戳。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatDataCatalogValidationResult> Catalogs;

	/** 合并后的全部问题；调用方不得只看 Fish 或只看 Equipment 后继续宣称内容包有效。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatDataCatalogIssue> Issues;
};