#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Camp/CatCampSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampSettingsRuntimeReadinessTest,
	"Catfishing.Unit.Camp.Settings.RuntimeRequiresEnabledPositiveRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先显式构造未配置状态证明类级 fail-closed，再读取项目默认 CampSettings，证明默认 gate、正半径和篝火封面事件不会停留在占位值。
bool FCatCampSettingsRuntimeReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatCampSettings* Settings = NewObject<UCatCampSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Camp Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableCampRuntime = false;
	Settings->InteractionRadiusCentimeters = 0.0;
	TestFalse(TEXT("未配置固定营地时不可运行"), Settings->IsRuntimeReady());
	Settings->bEnableCampRuntime = true;
	TestFalse(TEXT("只开 gate 但没有正交互半径仍不可运行"), Settings->IsRuntimeReady());
	Settings->InteractionRadiusCentimeters = 300.0;
	TestTrue(TEXT("显式 gate 与正交互半径启用固定营地入口"), Settings->IsRuntimeReady());

	const UCatCampSettings* ProjectSettings = GetDefault<UCatCampSettings>();
	TestNotNull(TEXT("可读取项目默认 Camp Settings"), ProjectSettings);
	if (ProjectSettings)
	{
		TestTrue(TEXT("项目默认配置开放正式 Lake 固定营地入口"), ProjectSettings->IsRuntimeReady());
		TestTrue(TEXT("项目默认营地半径为正值"), ProjectSettings->InteractionRadiusCentimeters > 0.0);
		TestFalse(TEXT("项目默认篝火封面事件已显式命名"), ProjectSettings->CampfireCoverEventId.IsNone());
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
