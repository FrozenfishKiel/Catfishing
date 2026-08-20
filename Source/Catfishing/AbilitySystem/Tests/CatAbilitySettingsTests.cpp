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

// 测试流程：把一个瞬态 Settings 上被项目配置填过的项逐个清掉，再读正式 readiness 和数值接口，核对它整体拒绝运行并把所有输出清成安全值。
// 为什么要先清：DefaultGame.ini 会在引擎启动时把值写进 CDO，而 NewObject 是从 CDO 拷的，瞬态对象一出生就带着项目配置，
// 不清就测不到 fail-closed 那条路径。清的目标是「ini 设过的每一项」，不是 C++ 声明初值——四个数值字段的声明初值是 -1.0f（负值表示 Unset），
// 这里写 0.0f 只是另一个能让接口拒绝的值，两者不是同一个状态。
// 项目级 ini 默认值是否合理由 ProjectDefaults 用例负责，两个用例守的是不同的东西，不要合并。
bool FCatAbilitySettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatAbilitySettings* Settings = NewObject<UCatAbilitySettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Ability Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	// 逐项清掉 DefaultGame.ini 在 CDO 上设过的配置。两类残留的后果不同，都必须清干净：
	// gate 类（runtime 开关、复制策略、tuning 开关）若残留 ini 的开启值，下面的 fail-closed 断言会直接失败；
	// 数值类若残留，则会被已关闭的 gate 短路掉、断言空过一遍，用例看着绿其实什么都没验证。
	Settings->bEnableCharacterAbilityRuntime = false;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;
	Settings->bEnableInitialAttributeTuning = false;
	Settings->InitialPoison = 0.0f;
	Settings->InitialFishingStrength = 0.0f;
	Settings->InitialFightStamina = 0.0f;
	Settings->bEnableDiagnosticAbility = false;

	float Poison = 123.0f;
	float FishingStrength = 123.0f;
	float FightStamina = 123.0f;
	float DiagnosticDelta = 123.0f;
	TestFalse(TEXT("默认 Ability runtime gate 关闭"), Settings->IsRuntimeEnabled());
	TestFalse(TEXT("默认初始属性读取失败"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("失败时 Poison 输出被清零"), Poison, 0.0f);
	TestEqual(TEXT("失败时 FishingStrength 输出被清零"), FishingStrength, 0.0f);
	TestEqual(TEXT("失败时 FightStamina 输出被清零"), FightStamina, 0.0f);
	TestFalse(TEXT("默认诊断 Poison 改变量读取失败"), Settings->TryGetDiagnosticPoisonDelta(DiagnosticDelta));
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
	Settings->DiagnosticPoisonDelta = -3.5f;

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
	TestEqual(TEXT("诊断 Poison 改变量保持配置值"), DiagnosticDelta, -3.5f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySettingsProjectDefaultsTest,
	"Catfishing.Unit.AbilitySystem.Settings.ProjectDefaultsEnableWork04RuntimeSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取正式 Ability Settings，验证 Character-owned ASC、Full 复制和完整初始三属性已经
// 显式接入，同时确认开发诊断 Ability 默认不作为正式玩法入口。
// 力量和体力断言的是飞书拍定值本身，不是"某个正数"：50 来自猫品种力量表 rev3 的普通猫档，100 来自钓鱼规则 rev212 §4.2 的猫体力上限。
// 写死这两个数是为了让任何一次改动都必须先说明它对应飞书的哪次修订，避免初值再次漂回没有出处的工程臆造值。
bool FCatAbilitySettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	TestNotNull(TEXT("项目默认 Ability Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	float Poison = -1.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	float DiagnosticDelta = 99.0f;
	TestTrue(TEXT("项目默认启用 Character Ability runtime"), Settings->IsRuntimeEnabled());
	TestTrue(TEXT("项目默认初始三属性可读取"), Settings->TryGetInitialAttributes(Poison, FishingStrength, FightStamina));
	TestEqual(TEXT("项目默认初始 Poison 为安全切片值"), Poison, 0.0f);
	TestEqual(TEXT("项目默认 FishingStrength 取飞书普通猫力量 50"), FishingStrength, 50.0f);
	TestEqual(TEXT("项目默认 FightStamina 取飞书猫体力上限 100"), FightStamina, 100.0f);
	TestFalse(TEXT("项目默认不启用开发诊断 Poison 改变量"), Settings->TryGetDiagnosticPoisonDelta(DiagnosticDelta));
	TestEqual(TEXT("诊断未启用时输出清零"), DiagnosticDelta, 0.0f);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
