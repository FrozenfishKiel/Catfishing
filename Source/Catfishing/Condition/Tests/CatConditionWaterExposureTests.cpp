#include "Misc/AutomationTest.h"

#include "Condition/CatConditionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatConditionWaterExposureThresholdContractTest,
	"Catfishing.Unit.Condition.WaterExposureThresholdContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCatConditionWaterExposureThresholdContractTest::RunTest(const FString& Parameters)
{
	UCatConditionSettings* Settings = NewObject<UCatConditionSettings>();
	Settings->bEnableConditionRuntime = true;
	Settings->WetWaterDepthCentimeters = 1.0;
	Settings->DangerousWaterDepthCentimeters = 45.0;
	Settings->DangerousWaterExitDepthCentimeters = 35.0;
	Settings->DangerousWaterConfirmationSeconds = 0.2;

	TestTrue(TEXT("有限并带滞回的水深阈值可启用"), Settings->HasWaterExposureThresholds());

	Settings->DangerousWaterExitDepthCentimeters = Settings->DangerousWaterDepthCentimeters;
	TestFalse(TEXT("退出深度必须严格小于危险进入深度"), Settings->HasWaterExposureThresholds());

	Settings->DangerousWaterExitDepthCentimeters = 35.0;
	Settings->DangerousWaterDepthCentimeters = 0.0;
	TestFalse(TEXT("未配置正危险深度时保持关闭"), Settings->HasWaterExposureThresholds());
	return true;
}

#endif
