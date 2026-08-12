#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "UI/CatUISettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsLakeStatusGateTest,
	"Catfishing.Unit.UI.Settings.LakeStatusViewUsesOnlyExplicitGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只读取 UI Settings 的 Lake 状态 View 开关；它证明 UI 装配不从玩法默认状态、Widget 类存在与否或 Online 生命周期推导开启。
bool FCatUISettingsLakeStatusGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("默认不装配 Lake 状态 View"), Settings->IsLakeStatusViewEnabled());
	Settings->bEnableLakeStatusView = true;
	TestTrue(TEXT("显式 gate 开启 Lake 状态 View"), Settings->IsLakeStatusViewEnabled());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
