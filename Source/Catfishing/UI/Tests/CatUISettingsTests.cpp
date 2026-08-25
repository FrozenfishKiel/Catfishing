#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "UI/CatUISettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsLakeStatusGateTest,
	"Catfishing.Unit.UI.Settings.LakeStatusViewUsesOnlyExplicitGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先验证没有配置覆盖时正式类默认不创建 LakeReach 白盒根，再显式打开和关闭同一 gate，证明玩家默认路径与开发验证路径都由同一设置承担。
bool FCatUISettingsLakeStatusGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("正式类默认不装配 Lake 白盒状态 View"), Settings->IsLakeStatusViewEnabled());
	Settings->bEnableLakeStatusView = true;
	TestTrue(TEXT("显式开启后才允许装配 LakeReach 根"), Settings->IsLakeStatusViewEnabled());
	Settings->bEnableLakeStatusView = false;
	TestFalse(TEXT("显式关闭后再次回到玩家默认不可见路径"), Settings->IsLakeStatusViewEnabled());
	TestEqual(TEXT("原生菜单输入有稳定默认键名"), Settings->LakeMenuToggleKeyName, FName(TEXT("Tab")));
	TestTrue(TEXT("菜单输入优先级保持非负"), Settings->LakeMenuInputPriority >= 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
