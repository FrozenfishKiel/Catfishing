#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionDefaultsTest,
	"Catfishing.Unit.Equipment.Definition.DefaultsAreNotRuntimeReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionRodTest,
	"Catfishing.Unit.Equipment.Definition.RodRequiresPositiveDurability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionBaitTest,
	"Catfishing.Unit.Equipment.Definition.EveryBaitMustBeRunConsumable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionChumTest,
	"Catfishing.Unit.Equipment.Definition.ChumRequiresPositiveContribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionDeprecatedKindsTest,
	"Catfishing.Unit.Equipment.Definition.DeprecatedHerbAndDriftwoodAreNotRuntimeReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionFloatTest,
	"Catfishing.Unit.Equipment.Definition.FloatRequiresPositiveRangeAndAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：创建默认装备定义并调用正式 readiness；Unset 类别、身份和功能路线不能被当作可运行道具。
bool FCatEquipmentDefinitionDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建默认装备定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	TestFalse(TEXT("默认装备定义不可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：只通过公开字段配置 Rod 定义；缺耐久时失败，补正耐久后通过，证明维修和失败预算不会读取半定义鱼竿。
bool FCatEquipmentDefinitionRodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建 Rod 定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->EquipmentDefinitionId = TEXT("RodA");
	Definition->Kind = ECatEquipmentKind::Rod;
	Definition->LoadoutSlotId = TEXT("RodSlot");
	Definition->FunctionalRouteId = TEXT("StarterRod");
	TestFalse(TEXT("Rod 缺少正最大耐久时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->MaximumRodDurability = 100.0;
	TestFalse(TEXT("Rod 只有耐久、缺强度与放线上限时仍不可运行（遛鱼判定缺输入）"), Definition->IsRuntimeDefinitionReady());
	Definition->RodStrength = 25.0;
	TestFalse(TEXT("Rod 缺放线上限时仍不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->MaximumLineLengthMeters = 60.0;
	TestTrue(TEXT("Rod 配置正耐久、正强度、正放线上限后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：先按普通饵配一条 Bait 定义并切换 bRunConsumable，再把它改成特殊饵重复一遍；两种饵都必须显式声明为一局消耗品才可运行。
// 这锁的是飞书钓鱼规则 §3.4（2026-08-18 拍定）"咬钩后无论结局均消耗 1 份饵"的口径：旧规则里普通饵无限、bRunConsumable 必须为 false，现已作废。
bool FCatEquipmentDefinitionBaitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建鱼饵定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->EquipmentDefinitionId = TEXT("OrdinaryBaitA");
	Definition->Kind = ECatEquipmentKind::Bait;
	Definition->LoadoutSlotId = TEXT("BaitSlot");
	Definition->FunctionalRouteId = TEXT("OrdinaryBaitRoute");
	Definition->bSpecialBait = false;
	Definition->bRunConsumable = false;
	TestFalse(TEXT("普通饵不声明为一局消耗品时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->bRunConsumable = true;
	TestTrue(TEXT("普通饵显式作为一局消耗品后可运行"), Definition->IsRuntimeDefinitionReady());

	Definition->EquipmentDefinitionId = TEXT("SpecialBaitA");
	Definition->FunctionalRouteId = TEXT("SpecialBaitRoute");
	Definition->bSpecialBait = true;
	Definition->bRunConsumable = false;
	TestFalse(TEXT("特殊饵不是一局消耗品时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->bRunConsumable = true;
	TestTrue(TEXT("特殊饵显式作为一局消耗品后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：配置 Chum 定义并验证三轴贡献；全零贡献失败，任一正贡献通过，证明窝料数值由数据定义显式提供。
bool FCatEquipmentDefinitionChumTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建窝料定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->EquipmentDefinitionId = TEXT("ChumA");
	Definition->Kind = ECatEquipmentKind::Chum;
	Definition->FunctionalRouteId = TEXT("ChumRoute");
	Definition->bRunConsumable = true;
	TestFalse(TEXT("Chum 缺少正三轴贡献时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->ChumContribution.Fishy = 1.0;
	TestTrue(TEXT("Chum 提供正贡献后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}


// 测试流程：把旧 Herb/Driftwood 配到看似完整；readiness 仍必须拒绝，防止旧恢复/修竿路径重新进入正式目录。
bool FCatEquipmentDefinitionDeprecatedKindsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* HerbDefinition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	UCatEquipmentDefinition* DriftwoodDefinition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建旧 Herb 定义"), HerbDefinition);
	TestNotNull(TEXT("可创建旧 Driftwood 定义"), DriftwoodDefinition);
	if (!HerbDefinition || !DriftwoodDefinition)
	{
		return false;
	}

	HerbDefinition->bEnableRuntimeDefinition = true;
	HerbDefinition->EquipmentDefinitionId = TEXT("OldHerb");
	HerbDefinition->Kind = ECatEquipmentKind::Herb;
	HerbDefinition->FunctionalRouteId = TEXT("DeprecatedHerb");
	HerbDefinition->bRunConsumable = true;

	DriftwoodDefinition->bEnableRuntimeDefinition = true;
	DriftwoodDefinition->EquipmentDefinitionId = TEXT("OldDriftwood");
	DriftwoodDefinition->Kind = ECatEquipmentKind::Driftwood;
	DriftwoodDefinition->FunctionalRouteId = TEXT("DeprecatedRepair");
	DriftwoodDefinition->bRunConsumable = true;

	TestFalse(TEXT("旧 Herb 不再可进入 runtime catalog"), HerbDefinition->IsRuntimeDefinitionReady());
	TestFalse(TEXT("旧 Driftwood 不再可进入 runtime catalog"), DriftwoodDefinition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：Float 必须同时给出正射程与正精准度偏移半径（抛竿落点与遛鱼开局 D₀ 都读它们），少一项就不可运行；
// 再反向确认非 Float 类别不许携带这两个字段，避免"竿上写了个漂射程"这种半配置内容悄悄进目录并改变 ContentHash。
bool FCatEquipmentDefinitionFloatTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建鱼漂定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->EquipmentDefinitionId = TEXT("FloatA");
	Definition->Kind = ECatEquipmentKind::Float;
	Definition->LoadoutSlotId = TEXT("Float");
	Definition->FunctionalRouteId = TEXT("FloatRoute");
	TestFalse(TEXT("鱼漂缺射程与精准度时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->FloatCastRangeMeters = 5.0;
	TestFalse(TEXT("鱼漂只有射程没有精准度时仍不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->FloatAccuracyOffsetRadiusMeters = 0.7;
	TestTrue(TEXT("鱼漂射程与精准度都为正后可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->bRunConsumable = true;
	TestFalse(TEXT("鱼漂不是一局消耗品"), Definition->IsRuntimeDefinitionReady());
	Definition->bRunConsumable = false;

	UCatEquipmentDefinition* RodDefinition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建鱼竿定义"), RodDefinition);
	if (!RodDefinition)
	{
		return false;
	}
	RodDefinition->bEnableRuntimeDefinition = true;
	RodDefinition->EquipmentDefinitionId = TEXT("RodA");
	RodDefinition->Kind = ECatEquipmentKind::Rod;
	RodDefinition->LoadoutSlotId = TEXT("Rod");
	RodDefinition->FunctionalRouteId = TEXT("RodRoute");
	RodDefinition->MaximumRodDurability = 40.0;
	RodDefinition->RodStrength = 25.0;
	RodDefinition->MaximumLineLengthMeters = 60.0;
	TestTrue(TEXT("竿字段齐全的鱼竿可运行"), RodDefinition->IsRuntimeDefinitionReady());
	RodDefinition->FloatCastRangeMeters = 3.0;
	TestFalse(TEXT("鱼竿携带漂射程时不可运行"), RodDefinition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}


#endif // WITH_DEV_AUTOMATION_TESTS
