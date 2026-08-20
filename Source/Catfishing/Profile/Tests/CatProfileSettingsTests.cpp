#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Profile/CatProfileSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSettingsPersistenceAndBridgeTest,
	"Catfishing.Unit.Profile.Settings.PersistenceAndImprintBridgeRequireExplicitSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Profile Settings 分别验证本地存档和外部成像桥 gate；桥接必须建立在可用存档槽位上，不能单独开启后报告成功。
bool FCatProfileSettingsPersistenceAndBridgeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatProfileSettings* Settings = NewObject<UCatProfileSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Profile Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableProfilePersistence = false;
	Settings->SaveSlotBaseName.Reset();
	Settings->bEnableExternalImprintCaptureBridge = false;

	TestFalse(TEXT("默认档案持久化不可用"), Settings->IsPersistenceReady());
	TestFalse(TEXT("默认外部成像桥不可用"), Settings->IsExternalImprintBridgeReady());
	Settings->bEnableExternalImprintCaptureBridge = true;
	TestFalse(TEXT("没有持久化槽位时外部成像桥仍不可用"), Settings->IsExternalImprintBridgeReady());

	Settings->bEnableProfilePersistence = true;
	Settings->SaveSlotBaseName = TEXT("  ");
	TestFalse(TEXT("空白存档槽位不可用"), Settings->IsPersistenceReady());
	Settings->SaveSlotBaseName = TEXT("CatProfileTest");
	TestTrue(TEXT("显式持久化 gate 与槽位启用存档"), Settings->IsPersistenceReady());
	TestTrue(TEXT("持久化就绪且桥 gate 开启后外部成像桥可用"), Settings->IsExternalImprintBridgeReady());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatProfileSettingsProjectDefaultsTest,
	"Catfishing.Unit.Profile.Settings.ProjectDefaultsEnablePersistenceAndBridgeHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取 Profile Settings，确认本地 SaveGame 槽位和外部成像桥 handoff 已显式开启；该测
// 试只验证配置 gate，不伪造成像订阅者或图片文件。
bool FCatProfileSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatProfileSettings* Settings = GetDefault<UCatProfileSettings>();
	TestNotNull(TEXT("项目 Profile Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认 Profile 持久化可用"), Settings->IsPersistenceReady());
	TestEqual(TEXT("项目默认 Profile 槽位基础名"), Settings->SaveSlotBaseName, FString(TEXT("CatProfile")));
	TestTrue(TEXT("项目默认外部成像桥 handoff 已开启"), Settings->IsExternalImprintBridgeReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
