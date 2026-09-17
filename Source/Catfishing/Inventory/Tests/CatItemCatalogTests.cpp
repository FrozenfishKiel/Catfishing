#include "Inventory/CatInventorySettings.h"
#include "Misc/AutomationTest.h"
#include "Data/CatFishDefinition.h"
#include "Profile/CatProfileSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatItemCatalogIdentityTest,
	"Catfishing.Unit.Inventory.ItemCatalogIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 身份回归流程：构造两项乱序目录验证稳定排序，再制造别名、错行名和缺定义，确认查询拒绝半份目录。
bool FCatItemCatalogIdentityTest::RunTest(const FString& Parameters)
{
	UCatInventorySettings* Settings = NewObject<UCatInventorySettings>();
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FCatItemCatalogRow::StaticStruct();
	Settings->ItemCatalog = Table;
	UCatInventoryItemDefinition* First = NewObject<UCatInventoryItemDefinition>();
	UCatInventoryItemDefinition* Second = NewObject<UCatInventoryItemDefinition>();
	First->ItemId = 1;
	Second->ItemId = 2;
	FCatItemCatalogRow Row;
	Row.ItemId = 2;
	Row.ItemDefinition = Second;
	Table->AddRow(TEXT("2"), Row);
	Row.ItemId = 1;
	Row.ItemDefinition = First;
	Table->AddRow(TEXT("1"), Row);
	TArray<UCatInventoryItemDefinition*> Definitions;
	FString Error;
	TestTrue(TEXT("完整有效目录可读取"), Settings->GetItemDefinitions(Definitions, Error));
	TestEqual(TEXT("所有物品均列出"), Definitions.Num(), 2);
	if (Definitions.Num() == 2)
	{
		TestEqual(TEXT("排序使用数字编号"), Definitions[0]->ItemId, 1);
		TestEqual(TEXT("第二项身份不变"), Definitions[1]->ItemId, 2);
	}
	Table->AddRow(TEXT("Alias"), Row);
	TestFalse(TEXT("重复身份或非数字行名不能通过"), Settings->GetItemDefinitions(Definitions, Error));
	TestTrue(TEXT("失败不返回半份物品列表"), Definitions.IsEmpty());
	Table->RemoveRow(TEXT("Alias"));
	Second->ItemId = 1;
	TestFalse(TEXT("总表与定义身份不一致被拒绝"), Settings->GetItemDefinitions(Definitions, Error));
	Second->ItemId = 2;
	Row.ItemId = 3;
	Row.ItemDefinition.Reset();
	Table->AddRow(TEXT("3"), Row);
	TestFalse(TEXT("缺失定义不得被静默跳过"), Settings->GetItemDefinitions(Definitions, Error));
	TestNull(TEXT("零号不是有效物品"), Settings->FindRuntimeDefinition(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFormalItemCatalogTest,
	"Catfishing.Unit.Inventory.FormalItemCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 正式接线回归流程：直接从项目设置加载资产总表，验证真实包中的数字身份和目录行一致，不使用测试替身冒充资产已迁移。
bool FCatFormalItemCatalogTest::RunTest(const FString& Parameters)
{
	TArray<UCatInventoryItemDefinition*> Definitions;
	FString Error;
	TestTrue(TEXT("正式物品总表可完整读取"), GetDefault<UCatInventorySettings>()->GetItemDefinitions(Definitions, Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("正式总表包含物品"), !Definitions.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLegacyItemIdentityMigrationTest,
	"Catfishing.Unit.Inventory.LegacyIdentityMigrationIsAtomic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 在同一对象内混入可转换与未知身份，验证失败完全不写；随后验证继承冲突和装备槽位映射保持语义。
bool FCatLegacyItemIdentityMigrationTest::RunTest(const FString& Parameters)
{
	FString Error;
	auto* Fish = NewObject<UCatFishDefinition>();
	Fish->FishDefinitionId = TEXT("Blackfish");
	auto& Weight = Fish->BaitWeightMultipliers.AddDefaulted_GetRef();
	Weight.BaitDefinitionId = TEXT("UnknownLegacyBait");
	Weight.Multiplier = 3.0;
	TestFalse(TEXT("未知旧编号拒绝整份迁移"), UCatInventorySettings::MigrateLegacyItemReferences(Fish, Error));
	TestEqual(TEXT("失败不写鱼编号"), Fish->ItemId, 0);
	TestEqual(TEXT("失败保留旧鱼编号"), Fish->FishDefinitionId, FName(TEXT("Blackfish")));
	Weight.BaitDefinitionId = TEXT("MeatBait");
	Fish->InventoryDefinitionId = TEXT("Loach");
	TestFalse(TEXT("父子类旧身份冲突不按遍历顺序覆盖"), UCatInventorySettings::MigrateLegacyItemReferences(Fish, Error));
	TestEqual(TEXT("继承冲突同样不写数字"), Fish->ItemId, 0);
	Fish->InventoryDefinitionId = NAME_None;
	TestTrue(TEXT("完整预检通过才迁移"), UCatInventorySettings::MigrateLegacyItemReferences(Fish, Error));
	TestEqual(TEXT("黑鱼使用冻结编号"), Fish->ItemId, 3);
	TestEqual(TEXT("肉块饵使用冻结编号"), Weight.BaitItemId, 24);
	TestEqual(TEXT("倍率未改变"), Weight.Multiplier, 3.0);
	TestTrue(TEXT("旧身份清空"), Fish->FishDefinitionId.IsNone() && Weight.BaitDefinitionId.IsNone());
	TestTrue(TEXT("重复迁移幂等"), UCatInventorySettings::MigrateLegacyItemReferences(Fish, Error));
	auto* Profile = NewObject<UCatProfileSaveGame>();
	Profile->EquipmentSelectionBySlot.Add(TEXT("Rod"), TEXT("StarterRodT1"));
	TestTrue(TEXT("装备选择映射可迁移"), UCatInventorySettings::MigrateLegacyItemReferences(Profile, Error));
	TestEqual(TEXT("槽位名称不迁移，物品身份改为数字"), Profile->EquipmentItemBySlot.FindRef(TEXT("Rod")), 37);
	TestTrue(TEXT("旧可写索引清空"), Profile->EquipmentSelectionBySlot.IsEmpty());
	return !HasAnyErrors();
}
#endif
