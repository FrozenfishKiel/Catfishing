#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsDefaultsTest,
	"Catfishing.Unit.AbilitySystem.Settings.DefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsValidValuesTest,
	"Catfishing.Unit.AbilitySystem.Settings.ValidRuntimeConfigurationExposesAttributes",
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

	float Poison = 123.0f;
	float FishingStrength = 123.0f;
	float FightStamina = 123.0f;
	TestFalse(TEXT("显式关闭 Ability runtime gate"), Settings->IsRuntimeEnabled());
	TestFalse(TEXT("关闭配置下初始属性读取失败"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("失败时 Poison 输出被清零"), Poison, 0.0f);
	TestEqual(TEXT("失败时 FishingStrength 输出被清零"), FishingStrength, 0.0f);
	TestEqual(TEXT("失败时 FightStamina 输出被清零"), FightStamina, 0.0f);
	return !HasAnyErrors();
}

// 测试流程：显式打开正式 runtime、Full 复制策略和初始属性 gate；随后只通过公开读取接口核对三项身体初值被一次性暴露。
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

	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	TestTrue(TEXT("显式 Full 策略启用 Ability runtime"), Settings->IsRuntimeEnabled());
	TestTrue(TEXT("完整初始属性配置可读取"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("Poison 读取配置值"), Poison, 1.0f);
	TestEqual(TEXT("FishingStrength 读取配置值"), FishingStrength, 4.0f);
	TestEqual(TEXT("FightStamina 读取配置值"), FightStamina, 5.0f);
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
