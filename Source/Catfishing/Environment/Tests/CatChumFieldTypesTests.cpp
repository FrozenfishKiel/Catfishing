#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Curves/CurveFloat.h"
#include "Environment/CatChumFieldTypes.h"

#include <limits>

namespace CatChumFieldTypesTest
{
	static UCurveFloat* MakeCurve(const float Start = 1.0f, const float End = 0.0f)
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage());
		Curve->FloatCurve.AddKey(0.0f, Start);
		Curve->FloatCurve.AddKey(1.0f, End);
		return Curve;
	}

	static FCatChumInfluenceSpec MakeSpec()
	{
		FCatChumInfluenceSpec Spec;
		Spec.RadiusCentimeters = 500.0;
		Spec.DurationSeconds = 60.0;
		Spec.BaseContribution.Fishy = 2.0;
		Spec.BaseContribution.Fragrant = 3.0;
		Spec.BaseContribution.Fermented = 4.0;
		Spec.DistanceFalloffCurve = MakeCurve();
		Spec.TimeFalloffCurve = MakeCurve();
		Spec.MaximumQuantityPerPlacement = 4;
		return Spec;
	}
}

#define CAT_CHUM_TYPES_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Environment.ChumField." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_CHUM_TYPES_TEST(FCatChumTypesDefaultsTest, "TypesDefaultsFailClosed")
CAT_CHUM_TYPES_TEST(FCatChumSpecValidationTest, "SpecRequiresContributionRadiusLifetimeCurvesAndQuantity")
CAT_CHUM_TYPES_TEST(FCatChumQuantityTest, "QuantityScalesContributionButNotRadiusOrDuration")

bool FCatChumTypesDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatChumInfluenceSpec Spec;
	FCatChumRuntimeInfluence Runtime;
	FCatChumFalloffTable Table;
	TestFalse(TEXT("default spec is closed"), Spec.IsRuntimeReady());
	TestFalse(TEXT("default spec cannot build"), Spec.BuildRuntimeInfluence(1, Runtime));
	TestFalse(TEXT("default LUT is closed"), Table.IsRuntimeReady());
	TestFalse(TEXT("default water handle is invalid"), FCatPlaceChumCommand().ExpectedWaterRegionHandle.IsValid());
	TestFalse(TEXT("default place result is not committed"), FCatPlaceChumResult().bCommitted);
	TestFalse(TEXT("default sample failed closed"), FCatChumSample().bSucceeded);
	return !HasAnyErrors();
}

bool FCatChumSpecValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatChumFieldTypesTest;
	FCatChumInfluenceSpec Spec = MakeSpec();
	FCatChumRuntimeInfluence Runtime;
	TestTrue(TEXT("complete spec is ready"), Spec.IsRuntimeReady());
	TestTrue(TEXT("complete spec builds quantity one"), Spec.BuildRuntimeInfluence(1, Runtime));

	FCatChumInfluenceSpec Invalid = Spec;
	Invalid.RadiusCentimeters = 0.0;
	TestFalse(TEXT("radius is required"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.DurationSeconds = 0.0;
	TestFalse(TEXT("duration is required"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.BaseContribution = FCatChumVector();
	TestFalse(TEXT("positive contribution is required"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.DistanceFalloffCurve.Reset();
	TestFalse(TEXT("distance curve is required"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.TimeFalloffCurve = NewObject<UCurveFloat>(GetTransientPackage());
	TestFalse(TEXT("empty time curve is rejected"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.DistanceFalloffCurve = MakeCurve(-1.0f, 0.0f);
	TestFalse(TEXT("negative curve output is rejected"), Invalid.IsRuntimeReady());
	Invalid = Spec; Invalid.TimeFalloffCurve = MakeCurve(std::numeric_limits<float>::quiet_NaN(), 0.0f);
	TestFalse(TEXT("nonfinite curve output is rejected"), Invalid.IsRuntimeReady());
	TestFalse(TEXT("zero quantity is rejected"), Spec.BuildRuntimeInfluence(0, Runtime));
	TestFalse(TEXT("quantity over authored cap is rejected"), Spec.BuildRuntimeInfluence(5, Runtime));
	return !HasAnyErrors();
}

bool FCatChumQuantityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatChumInfluenceSpec Spec = CatChumFieldTypesTest::MakeSpec();
	FCatChumRuntimeInfluence One;
	FCatChumRuntimeInfluence Three;
	TestTrue(TEXT("build quantity one"), Spec.BuildRuntimeInfluence(1, One));
	TestTrue(TEXT("build quantity three"), Spec.BuildRuntimeInfluence(3, Three));
	TestEqual(TEXT("quantity scales fishy"), Three.BaseContribution.Fishy, One.BaseContribution.Fishy * 3.0);
	TestEqual(TEXT("quantity scales fragrant"), Three.BaseContribution.Fragrant, One.BaseContribution.Fragrant * 3.0);
	TestEqual(TEXT("quantity scales fermented"), Three.BaseContribution.Fermented, One.BaseContribution.Fermented * 3.0);
	TestEqual(TEXT("quantity does not scale radius"), Three.RadiusCentimeters, One.RadiusCentimeters);
	TestEqual(TEXT("quantity does not scale duration"), Three.DurationSeconds, One.DurationSeconds);
	TestEqual(TEXT("distance LUT starts at authored value"), Three.DistanceFalloff.Evaluate(0.0), 1.0);
	TestEqual(TEXT("distance LUT clamps upper input"), Three.DistanceFalloff.Evaluate(2.0), 0.0);
	return !HasAnyErrors();
}

#undef CAT_CHUM_TYPES_TEST

#endif
