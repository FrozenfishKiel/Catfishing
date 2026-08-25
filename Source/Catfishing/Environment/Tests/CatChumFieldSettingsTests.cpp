#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatChumFieldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumFieldSettingsDefaultsTest,
	"Catfishing.Unit.Environment.ChumField.SettingsDefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先把可能从 DefaultGame 继承来的 ChumField 配置显式清空，再逐项补齐最小 runtime 条件；
// 这样用例证明的是 Settings 自身的 fail-closed 语义，不会随项目当前正式配置开关变化而漂移。
bool FCatChumFieldSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatChumFieldSettings* Settings = NewObject<UCatChumFieldSettings>(GetTransientPackage());
	Settings->bEnableChumFieldRuntime = false;
	Settings->MaxActiveFieldsPerRegion = 0;
	Settings->MaxRawContributionPerRegion = 0.0;
	Settings->MaxPlacementRangeCentimeters = 0.0;
	Settings->MaxAimDeviationDegrees = 0.0;
	Settings->ExpiredCleanupIntervalSeconds = 0.0;
	TestFalse(TEXT("settings default closed"), Settings->IsRuntimeReady());
	Settings->bEnableChumFieldRuntime = true;
	Settings->MaxActiveFieldsPerRegion = 2;
	Settings->MaxRawContributionPerRegion = 20.0;
	Settings->MaxPlacementRangeCentimeters = 1000.0;
	Settings->MaxAimDeviationDegrees = 45.0;
	Settings->PlacementLineOfSightChannel = ECC_Visibility;
	Settings->ExpiredCleanupIntervalSeconds = 1.0;
	TestTrue(TEXT("all explicit values open settings"), Settings->IsRuntimeReady());
	Settings->MaxAimDeviationDegrees = 181.0;
	TestFalse(TEXT("angle over 180 rejected"), Settings->IsRuntimeReady());
	return !HasAnyErrors();
}

#endif
