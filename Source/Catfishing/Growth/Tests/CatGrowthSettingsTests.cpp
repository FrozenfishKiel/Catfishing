#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Growth/CatGrowthSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGrowthSettingsReadinessTest,
	"Catfishing.Unit.Growth.Settings.RequiresExplicitRuntimeAndSlotLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Growth Settings 显式构造关闭态；只有运行 gate 与正槽长同时成立，吃鱼成长才开放。
bool FCatGrowthSettingsReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatGrowthSettings* Settings = NewObject<UCatGrowthSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Growth Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableGrowthRuntime = false;
	Settings->ExperiencePerChoiceSlot = 0;
	TestFalse(TEXT("显式关闭的 Growth 配置 fail-closed"), Settings->IsRuntimeReady());
	Settings->bEnableGrowthRuntime = true;
	Settings->ExperiencePerChoiceSlot = 0;
	TestFalse(TEXT("缺少经验槽长度时仍 fail-closed"), Settings->IsRuntimeReady());
	Settings->ExperiencePerChoiceSlot = 10;
	TestTrue(TEXT("显式 gate 与槽长开放成长"), Settings->IsRuntimeReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
