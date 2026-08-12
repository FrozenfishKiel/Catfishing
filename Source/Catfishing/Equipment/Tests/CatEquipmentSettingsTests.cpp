#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"

namespace CatEquipmentSettingsTest
{
	// 构造流程：创建一条可被目录接受的一局草药耗材；它只服务装备目录合同测试，不表达真实恢复数值。
	static UCatEquipmentDefinition* MakeReadyHerbDefinition(const FName DefinitionId)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = ECatEquipmentKind::Herb;
		Definition->FunctionalRouteId = TEXT("HerbRecovery");
		Definition->bRunConsumable = true;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsDuplicateDefinitionTest,
	"Catfishing.Unit.Equipment.Settings.DuplicateRuntimeDefinitionsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsFindDefinitionTest,
	"Catfishing.Unit.Equipment.Settings.FindRuntimeDefinitionRequiresEnabledReadyDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：向瞬态装备目录放入两条同 ID 正式定义；查找接口必须拒绝重复 ID，避免装配或消耗命令读取不稳定条目。
bool FCatEquipmentSettingsDuplicateDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Equipment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	UCatEquipmentDefinition* FirstDefinition = CatEquipmentSettingsTest::MakeReadyHerbDefinition(TEXT("HerbA"));
	UCatEquipmentDefinition* SecondDefinition = CatEquipmentSettingsTest::MakeReadyHerbDefinition(TEXT("HerbA"));
	Settings->Definitions = {FirstDefinition, SecondDefinition};

	TestNull(TEXT("重复 EquipmentDefinitionId 返回空"), Settings->FindRuntimeDefinition(TEXT("HerbA")));
	return !HasAnyErrors();
}

// 测试流程：同一目录先放未启用定义，再替换为完整定义；公开查找只能返回已启用且 readiness 通过的条目。
bool FCatEquipmentSettingsFindDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Equipment Settings 查找场景"), Settings);
	if (!Settings)
	{
		return false;
	}

	UCatEquipmentDefinition* DisabledDefinition = CatEquipmentSettingsTest::MakeReadyHerbDefinition(TEXT("HerbA"));
	DisabledDefinition->bEnableRuntimeDefinition = false;
	Settings->Definitions = {DisabledDefinition};
	TestNull(TEXT("未启用定义不会被目录返回"), Settings->FindRuntimeDefinition(TEXT("HerbA")));

	UCatEquipmentDefinition* ReadyDefinition = CatEquipmentSettingsTest::MakeReadyHerbDefinition(TEXT("HerbA"));
	Settings->Definitions = {ReadyDefinition};
	TestEqual(TEXT("唯一完整定义可被目录返回"), Settings->FindRuntimeDefinition(TEXT("HerbA")), ReadyDefinition);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
