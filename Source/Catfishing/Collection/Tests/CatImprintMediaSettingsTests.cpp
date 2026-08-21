#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Collection/CatImprintMediaSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatImprintMediaSettingsProjectDefaultsTest,
	"Catfishing.Unit.Collection.ImprintMediaSettings.ProjectDefaultsEnableBoundedImageTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取媒体传输 Settings，确认 Work7 只开放有容量、块数和 MIME 白名单的图片传输；未知
// 格式仍 fail-closed，避免把任意字节流当作可保存相册媒体。
bool FCatImprintMediaSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatImprintMediaSettings* Settings = GetDefault<UCatImprintMediaSettings>();
	TestNotNull(TEXT("项目 Imprint Media Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认媒体传输可运行"), Settings->IsRuntimeReady());
	TestEqual(TEXT("项目默认单媒体大小上限"), Settings->MaxMediaBytes, static_cast<int64>(4194304));
	TestEqual(TEXT("项目默认块大小上限"), Settings->MaxChunkBytes, 65536);
	TestEqual(TEXT("项目默认块数量上限"), Settings->MaxChunkCount, 128);
	TestTrue(TEXT("项目默认允许 PNG"), Settings->IsMimeTypeAllowed(TEXT(" image/png ")));
	TestTrue(TEXT("项目默认允许 JPEG"), Settings->IsMimeTypeAllowed(TEXT("IMAGE/JPEG")));
	TestFalse(TEXT("项目默认拒绝未裁格式"), Settings->IsMimeTypeAllowed(TEXT("image/gif")));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
