#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CatDataCatalogValidationCommandlet.generated.h"

/** Editor/CI 使用的数据目录验证入口；它只读取当前项目默认目录配置，不生成内容、不连接外部文档，也不替代运行时 fail-closed gate。 */
UCLASS()
class CATFISHING_API UCatDataCatalogValidationCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	/** 配置命令行验证的执行环境和退出码语义；CI 依赖 Main 的返回值判断内容目录是否可进入运行时。 */
	UCatDataCatalogValidationCommandlet();

	/** 校验默认 Fish 与 Equipment 目录并输出结构化日志；返回 0 表示内容包可运行，返回 1 表示必须阻断 Editor/CI/Cook 流程。 */
	virtual int32 Main(const FString& Params) override;
};