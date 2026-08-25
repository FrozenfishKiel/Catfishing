#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CatAbilitySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsDefaultsTest,
	"Catfishing.Unit.AbilitySystem.Settings.DefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsValidValuesTest,
	"Catfishing.Unit.AbilitySystem.Settings.ValidRuntimeConfigurationExposesAttributesAndDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsPartialValuesTest,
	"Catfishing.Unit.AbilitySystem.Settings.PartialInitialAttributesFailAndClearOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：创建瞬态 Settings 对象并显式制造关闭配置，直接读取正式 readiness 和数值接口；失败时必须把所有输出清成安全值。
bool FCatAbilitySettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatAbilitySettings* Settings = NewObject<UCatAbilitySettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Ability Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableCharacterAbilityRuntime = false;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;
	Settings->bEnableInitialAttributeTuning = false;
	Settings->InitialPoison = -1.0f;
	Settings->InitialFishingStrength = -1.0f;
	Settings->InitialFightStamina = -1.0f;
	Settings->bEnableDiagnosticAbility = false;
	Settings->DiagnosticPoisonDelta = 0.0f;

	float Poison = 123.0f;
	float FishingStrength = 123.0f;
	float FightStamina = 123.0f;
	float DiagnosticDelta = 123.0f;
	TestFalse(TEXT("显式关闭 Ability runtime gate"), Settings->IsRuntimeEnabled());
	TestFalse(TEXT("关闭配置下初始属性读取失败"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("失败时 Poison 输出被清零"), Poison, 0.0f);
	TestEqual(TEXT("失败时 FishingStrength 输出被清零"), FishingStrength, 0.0f);
	TestEqual(TEXT("失败时 FightStamina 输出被清零"), FightStamina, 0.0f);
	TestFalse(TEXT("关闭配置下诊断 Poison 改变量读取失败"), Settings->TryGetDiagnosticPoisonDelta(DiagnosticDelta));
	TestEqual(TEXT("失败时诊断改变量被清零"), DiagnosticDelta, 0.0f);
	return !HasAnyErrors();
}

// 测试流程：显式打开正式 runtime、Full 复制策略、初始属性和诊断 gate；随后只通过公开读取接口核对完整配置被一次性暴露。
bool FCatAbilitySettingsValidValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatAbilitySettings* Settings = NewObject<UCatAbilitySettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建有效配置场景的 Ability Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialPoison = 1.0f;
	Settings->InitialFishingStrength = 4.0f;
	Settings->InitialFightStamina = 5.0f;
	Settings->bEnableDiagnosticAbility = true;
	Settings->DiagnosticPoisonDelta = 3.5f;

	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	float DiagnosticDelta = 0.0f;
	TestTrue(TEXT("显式 Full 策略启用 Ability runtime"), Settings->IsRuntimeEnabled());
	TestTrue(TEXT("完整初始属性配置可读取"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("Poison 读取配置值"), Poison, 1.0f);
	TestEqual(TEXT("FishingStrength 读取配置值"), FishingStrength, 4.0f);
	TestEqual(TEXT("FightStamina 读取配置值"), FightStamina, 5.0f);
	TestTrue(TEXT("非零诊断 Poison 改变量可读取"), Settings->TryGetDiagnosticPoisonDelta(DiagnosticDelta));
	TestEqual(TEXT("诊断 Poison 改变量保持配置值"), DiagnosticDelta, 3.5f);
	return !HasAnyErrors();
}

// 测试流程：只破坏一个必需初始属性，确认公开读取接口整体失败并清空所有输出，避免 Character ASC 获得半套初值。
bool FCatAbilitySettingsPartialValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatAbilitySettings* Settings = NewObject<UCatAbilitySettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建半配置 Ability Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialPoison = 0.0f;
	Settings->InitialFishingStrength = 4.0f;
	Settings->InitialFightStamina = 0.0f;

	float Poison = 99.0f;
	float FishingStrength = 99.0f;
	float FightStamina = 99.0f;
	TestFalse(TEXT("FightStamina 未配置时初始属性整体失败"),
		Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("半配置失败清空 Poison"), Poison, 0.0f);
	TestEqual(TEXT("半配置失败清空 FishingStrength"), FishingStrength, 0.0f);
	TestEqual(TEXT("半配置失败清空 FightStamina"), FightStamina, 0.0f);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
