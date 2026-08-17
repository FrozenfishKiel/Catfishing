#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatChumFieldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumFieldSettingsDefaultsTest,
	"Catfishing.Unit.Environment.ChumField.SettingsDefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatChumFieldSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatChumFieldSettings* Settings = NewObject<UCatChumFieldSettings>(GetTransientPackage());
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
