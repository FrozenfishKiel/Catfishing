#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Character/CatCharacterDefinition.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsDefaultCharacterDefinitionTest,
	"Catfishing.Unit.AbilitySystem.Settings.DefaultCharacterDefinitionDrivesUnspecifiedCharacters",
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

// 测试流程：角色未指定 CatDefinitionId 时读默认猫 DA；显式角色 ID 优先，默认 ID 缺失时不绕回全局数值。
bool FCatAbilitySettingsDefaultCharacterDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatAbilitySettings* Settings = NewObject<UCatAbilitySettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建默认猫定义配置"), Settings);
	if (!Settings)
	{
		return false;
	}
	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialPoison = 1.0f;
	Settings->InitialFishingStrength = 2.0f;
	Settings->InitialFightStamina = 3.0f;
	Settings->DefaultCharacterDefinitionId = TEXT("DefaultCatTest");

	auto MakeReadyDefinition = [](const FName DefinitionId, const float Poison, const float Strength,
		const float Stamina)
	{
		UCatCharacterDefinition* Definition = NewObject<UCatCharacterDefinition>(GetTransientPackage());
		Definition->CatDefinitionId = DefinitionId;
		Definition->InitialPoison = Poison;
		Definition->FishingStrength = Strength;
		Definition->FightStaminaMaximum = Stamina;
		Definition->bEnableRuntimeDefinition = true;
		return Definition;
	};

	UCatCharacterDefinition* DefaultDefinition = MakeReadyDefinition(TEXT("DefaultCatTest"), 4.0f, 5.0f, 6.0f);
	UCatCharacterDefinition* ExplicitDefinition = MakeReadyDefinition(TEXT("ExplicitCatTest"), 7.0f, 8.0f, 9.0f);
	Settings->CharacterDefinitions = {
		TSoftObjectPtr<UCatCharacterDefinition>(DefaultDefinition),
		TSoftObjectPtr<UCatCharacterDefinition>(ExplicitDefinition)
	};

	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	TestTrue(TEXT("留空角色 ID 解析默认猫 DA"), Settings->TryGetInitialAttributesForCharacter(
		NAME_None, Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("默认猫 Poison 来自 DA"), Poison, 4.0f);
	TestEqual(TEXT("默认猫 FishingStrength 来自 DA"), FishingStrength, 5.0f);
	TestEqual(TEXT("默认猫 FightStamina 来自 DA"), FightStamina, 6.0f);

	TestTrue(TEXT("显式角色 ID 覆盖默认猫"), Settings->TryGetInitialAttributesForCharacter(
		TEXT("ExplicitCatTest"), Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("显式猫 Poison 来自自身 DA"), Poison, 7.0f);
	TestEqual(TEXT("显式猫 FishingStrength 来自自身 DA"), FishingStrength, 8.0f);
	TestEqual(TEXT("显式猫 FightStamina 来自自身 DA"), FightStamina, 9.0f);

	Settings->DefaultCharacterDefinitionId = TEXT("MissingDefaultCatTest");
	TestFalse(TEXT("默认 ID 缺失时 fail-closed"), Settings->TryGetInitialAttributesForCharacter(
		NAME_None, Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("默认定义缺失清空 Poison"), Poison, 0.0f);
	TestEqual(TEXT("默认定义缺失清空 FishingStrength"), FishingStrength, 0.0f);
	TestEqual(TEXT("默认定义缺失清空 FightStamina"), FightStamina, 0.0f);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
