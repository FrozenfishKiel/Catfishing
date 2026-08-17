#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Curves/CurveFloat.h"
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
	FCatEquipmentDefinitionSpecialBaitTest,
	"Catfishing.Unit.Equipment.Definition.SpecialBaitMustBeRunConsumable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentDefinitionChumTest,
	"Catfishing.Unit.Equipment.Definition.ChumRequiresSpatialInfluenceSpec",
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
	TestTrue(TEXT("Rod 配置正最大耐久后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：配置特殊饵定义并切换 bRunConsumable；特殊饵必须是一局消耗品，不能被普通无限饵规则吞掉。
bool FCatEquipmentDefinitionSpecialBaitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建特殊饵定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->EquipmentDefinitionId = TEXT("SpecialBaitA");
	Definition->Kind = ECatEquipmentKind::Bait;
	Definition->LoadoutSlotId = TEXT("BaitSlot");
	Definition->FunctionalRouteId = TEXT("SpecialBaitRoute");
	Definition->bSpecialBait = true;
	Definition->bRunConsumable = false;
	TestFalse(TEXT("特殊饵不是一局消耗品时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->bRunConsumable = true;
	TestTrue(TEXT("特殊饵显式作为一局消耗品后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：配置 Chum 定义并验证完整空间 Influence；只有贡献、范围、寿命、曲线和数量上限全部显式提供才可运行。
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
	TestFalse(TEXT("Chum 缺少空间 Influence 时不可运行"), Definition->IsRuntimeDefinitionReady());
	UCurveFloat* Distance = NewObject<UCurveFloat>(GetTransientPackage());
	Distance->FloatCurve.AddKey(0.0f, 1.0f); Distance->FloatCurve.AddKey(1.0f, 0.0f);
	UCurveFloat* Time = NewObject<UCurveFloat>(GetTransientPackage());
	Time->FloatCurve.AddKey(0.0f, 1.0f); Time->FloatCurve.AddKey(1.0f, 0.0f);
	Definition->ChumInfluence.RadiusCentimeters = 500.0;
	Definition->ChumInfluence.DurationSeconds = 60.0;
	Definition->ChumInfluence.BaseContribution.Fishy = 1.0;
	Definition->ChumInfluence.DistanceFalloffCurve = Distance;
	Definition->ChumInfluence.TimeFalloffCurve = Time;
	Definition->ChumInfluence.MaximumQuantityPerPlacement = 3;
	TestTrue(TEXT("Chum 完整空间 Influence 后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
