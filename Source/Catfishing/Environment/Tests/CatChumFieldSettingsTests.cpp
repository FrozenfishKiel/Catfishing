#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatChumFieldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumInfluenceAreaMultiplierTest,
	"Catfishing.Unit.Environment.ChumFields.AreaMultiplierConvertsToRadiusScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatChumInfluenceAreaMultiplierTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatChumFieldSettings* Settings = NewObject<UCatChumFieldSettings>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient chum field settings"), Settings))
	{
		return false;
	}

	double RadiusScale = 99.0;
	Settings->InfluenceAreaMultiplier = 2.0;
	TestTrue(TEXT("positive area multiplier resolves"), Settings->TryGetInfluenceRadiusScale(RadiusScale));
	TestEqual(TEXT("doubling circle area scales radius by square root of two"),
		RadiusScale, FMath::Sqrt(2.0), UE_DOUBLE_SMALL_NUMBER);
	Settings->bEnableChumFieldRuntime = true;
	Settings->MaxActiveFieldsPerRegion = 1;
	Settings->MaxRawContributionPerRegion = 1.0;
	Settings->MaxPlacementRangeCentimeters = 1.0;
	Settings->MaxAimDeviationDegrees = 1.0;
	Settings->PlacementLineOfSightChannel = ECC_Visibility;
	Settings->ExpiredCleanupIntervalSeconds = 1.0;
	TestTrue(TEXT("valid area multiplier participates in runtime readiness"), Settings->IsRuntimeReady());

	Settings->InfluenceAreaMultiplier = 0.0;
	TestFalse(TEXT("zero area multiplier is rejected"), Settings->TryGetInfluenceRadiusScale(RadiusScale));
	TestEqual(TEXT("rejected multiplier clears radius scale"), RadiusScale, 0.0);
	TestFalse(TEXT("invalid area multiplier disables chum field runtime"), Settings->IsRuntimeReady());
	return !HasAnyErrors();
}

#endif
