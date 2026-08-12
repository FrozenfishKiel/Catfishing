#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Camp/CatCampSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampSettingsRuntimeReadinessTest,
	"Catfishing.Unit.Camp.Settings.RuntimeRequiresEnabledPositiveRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只通过瞬态 Settings 的公开 readiness 裁决固定营地入口；默认 gate、零半径和完整配置分别证明营地模块不会从占位值开放交互。
bool FCatCampSettingsRuntimeReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatCampSettings* Settings = NewObject<UCatCampSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Camp Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("默认固定营地配置不可运行"), Settings->IsRuntimeReady());
	Settings->bEnableCampRuntime = true;
	TestFalse(TEXT("只开 gate 但没有正交互半径仍不可运行"), Settings->IsRuntimeReady());
	Settings->InteractionRadiusCentimeters = 300.0;
	TestTrue(TEXT("显式 gate 与正交互半径启用固定营地入口"), Settings->IsRuntimeReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
