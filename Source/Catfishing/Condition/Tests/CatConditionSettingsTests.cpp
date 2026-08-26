#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Condition/CatConditionSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionSettingsDefaultsTest,
	"Catfishing.Unit.Condition.Settings.DefaultsDoNotEnableDownedThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionSettingsValidThresholdsTest,
	"Catfishing.Unit.Condition.Settings.ValidThresholdsEnableDownedEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Settings 显式构造关闭态；即使项目配置已经启用 Condition，零值组合也不能推导出倒地规则。
bool FCatConditionSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatConditionSettings* Settings = NewObject<UCatConditionSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Condition Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableConditionRuntime = false;
	Settings->PoisonDownedThreshold = 0.0;
	TestFalse(TEXT("显式关闭的 Condition gate 不提供倒地阈值"), Settings->HasDownedThresholds());
	Settings->bEnableConditionRuntime = true;
	Settings->PoisonDownedThreshold = 0.0;
	TestFalse(TEXT("缺少 Poison 阈值时仍 fail-closed"), Settings->HasDownedThresholds());
	return !HasAnyErrors();
}

// 测试流程：显式启用运行 gate 并设置正 Poison 阈值；公开查询必须只在完整配置时返回 true。
bool FCatConditionSettingsValidThresholdsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatConditionSettings* Settings = NewObject<UCatConditionSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建有效 Condition Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableConditionRuntime = true;
	Settings->PoisonDownedThreshold = 10.0;
	TestTrue(TEXT("正 Poison 阈值启用倒地裁决"), Settings->HasDownedThresholds());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
