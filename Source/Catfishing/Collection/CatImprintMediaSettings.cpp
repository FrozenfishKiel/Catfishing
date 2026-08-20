#include "Collection/CatImprintMediaSettings.h"

// 运行边界流程：同时要求显式启用、容量/块上限为正且至少有一个 MIME 白名单，避免默认配置把占位媒体链路打开。
bool UCatImprintMediaSettings::IsRuntimeReady() const
{
	return bEnableImprintMediaTransport && MaxMediaBytes > 0 && MaxChunkBytes > 0
		&& MaxChunkCount > 0 && !AllowedMimeTypes.IsEmpty();
}

// MIME 校验流程：把输入和白名单都裁剪并转小写后比较；空字符串和未配置白名单都会 fail-closed。
bool UCatImprintMediaSettings::IsMimeTypeAllowed(const FString& MimeType) const
{
	const FString Normalized = MimeType.TrimStartAndEnd().ToLower();
	if (Normalized.IsEmpty())
	{
		return false;
	}
	for (const FString& Allowed : AllowedMimeTypes)
	{
		if (Allowed.TrimStartAndEnd().ToLower() == Normalized)
		{
			return true;
		}
	}
	return false;
}