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

// 测试流程：用瞬态 Settings 读取倒地阈值 gate；默认配置必须保持关闭，证明组件不会从零值推导倒地规则。
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
	Settings->FatigueDownedThreshold = 0.0;

	TestFalse(TEXT("默认 Condition gate 不提供倒地阈值"), Settings->HasDownedThresholds());
	Settings->bEnableConditionRuntime = true;
	Settings->PoisonDownedThreshold = 10.0;
	Settings->FatigueDownedThreshold = 0.0;
	TestFalse(TEXT("缺少 Fatigue 阈值时仍 fail-closed"), Settings->HasDownedThresholds());
	return !HasAnyErrors();
}

// 测试流程：显式启用运行 gate 并设置两项正阈值；公开查询必须只在完整配置时返回 true。
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
	Settings->FatigueDownedThreshold = 20.0;
	TestTrue(TEXT("完整正阈值启用倒地裁决"), Settings->HasDownedThresholds());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionSettingsProjectDefaultsTest,
	"Catfishing.Unit.Condition.Settings.ProjectDefaultsEnableWork04RuntimeSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取正式 Condition Settings，确认倒地阈值显式启用、初始身体不会因零属性立即倒地。
bool FCatConditionSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	TestNotNull(TEXT("项目默认 Condition Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认启用倒地阈值裁决"), Settings->HasDownedThresholds());
	TestEqual(TEXT("项目默认 Poison 倒地阈值高于初始零值"), Settings->PoisonDownedThreshold, 100.0);
	TestEqual(TEXT("项目默认 Fatigue 倒地阈值高于初始零值"), Settings->FatigueDownedThreshold, 100.0);
	TestEqual(TEXT("项目默认野外自救减少 Fatigue"), Settings->FieldRestFatigueRelief, 15.0);
	TestEqual(TEXT("项目默认营地休息减少更多 Fatigue"), Settings->CampRestFatigueRelief, 50.0);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
