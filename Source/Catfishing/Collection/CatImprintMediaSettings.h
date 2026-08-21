#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatImprintMediaSettings.generated.h"

/** 印记媒体传输的显式安全边界；默认关闭，避免把没有正式成像和容量策略的字节链路误当成产品闭环。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Imprint Media"))
class CATFISHING_API UCatImprintMediaSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断媒体传输是否具备最小运行边界；必须显式启用并配置容量、块数和 MIME 白名单。 */
	bool IsRuntimeReady() const;

	/** 判断 Host Manifest 声明的 MIME 是否在白名单中；比较前会去除首尾空白并忽略大小写。 */
	bool IsMimeTypeAllowed(const FString& MimeType) const;

	/** 服务器媒体传输总开关；默认关闭，所有 Begin 请求 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableImprintMediaTransport = false;

	/** 单份媒体允许的最大字节数；超限时 Manifest 在收件人可见前被拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1"))
	int64 MaxMediaBytes = 4 * 1024 * 1024;

	/** 单个块允许的最大字节数；它限制 RPC/传输层一次承载的 payload。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1"))
	int32 MaxChunkBytes = 64 * 1024;

	/** 单份媒体允许的最大块数；防止极小块导致无限 ACK/重传状态。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1"))
	int32 MaxChunkCount = 128;

	/** 允许进入媒体链路的 MIME 白名单；空白名单表示产品格式未裁，必须拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TArray<FString> AllowedMimeTypes;
};
