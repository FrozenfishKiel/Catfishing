#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ShopEconomy/CatShopEconomySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomySettingsProjectDefaultsTest,
	"Catfishing.Unit.ShopEconomy.Settings.ProjectDefaultsExposeWalletCatalogAndFreeBait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomySettingsUndecidedSentinelsFailClosedTest,
	"Catfishing.Unit.ShopEconomy.Settings.UndecidedPriceAndWalletSentinelsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomySettingsFishPurchasePriceTableTest,
	"Catfishing.Unit.ShopEconomy.Settings.FishPurchasePriceTableFailsClosedAndPicksWeightBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取 ShopEconomy Settings，先确认钱包 gate 已满足运行前提，再检查有限库存鱼竿订单
// 和免费普通饵目录项；这些断言只锁住显式经济配置，不替 Equipment 或 Items 验证下游定义。
bool FCatShopEconomySettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
	TestNotNull(TEXT("项目 ShopEconomy Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认团队经济运行 gate 已开启"), Settings->IsRuntimeEnabled());
	TestEqual(TEXT("项目默认团队钱包初始余额"), Settings->StartingTeamWalletBalance, 10);
	TestEqual(TEXT("项目默认售鱼最小入账金额"), Settings->MinimumFishSaleValue, 1);
	TestEqual(TEXT("项目默认免费普通饵入口已显式配置"), Settings->FreeOrdinaryBaitEntryId,
		FName(TEXT("FreeBasicBaitClaim")));

	const FCatShopCatalogEntry* RodOrder = Settings->CatalogEntries.FindByPredicate(
		[](const FCatShopCatalogEntry& Entry)
		{
			return Entry.EntryId == FName(TEXT("ShopRodBasicOrder"));
		});
	TestNotNull(TEXT("项目默认商店包含二级鱼竿订单"), RodOrder);
	if (RodOrder)
	{
		TestTrue(TEXT("二级鱼竿订单本身可进入运行库存"), RodOrder->IsRuntimeReady());
		TestEqual(TEXT("二级鱼竿订单交付给 Equipment"), RodOrder->Kind, ECatShopEntryKind::EquipmentGrant);
		TestEqual(TEXT("二级鱼竿订单指向 Work1 装备定义"), RodOrder->DefinitionId, FName(TEXT("Rod_Basic")));
		TestEqual(TEXT("二级鱼竿订单单价"), RodOrder->UnitPrice, 3);
		TestEqual(TEXT("二级鱼竿订单有限库存"), RodOrder->InitialStock, 1);
		TestFalse(TEXT("二级鱼竿订单不是无限库存"), RodOrder->bUnlimitedStock);
	}

	const FCatShopCatalogEntry* FreeBait = Settings->CatalogEntries.FindByPredicate(
		[](const FCatShopCatalogEntry& Entry)
		{
			return Entry.EntryId == FName(TEXT("FreeBasicBaitClaim"));
		});
	TestNotNull(TEXT("项目默认商店包含免费普通饵入口"), FreeBait);
	if (FreeBait)
	{
		TestTrue(TEXT("免费普通饵目录项本身可运行"), FreeBait->IsRuntimeReady());
		TestEqual(TEXT("免费普通饵交付为 Equipment 装备实例"), FreeBait->Kind, ECatShopEntryKind::EquipmentGrant);
		TestEqual(TEXT("免费普通饵指向基础普通饵定义"), FreeBait->DefinitionId, FName(TEXT("Bait_Basic")));
		TestEqual(TEXT("免费普通饵价格明确为 0"), FreeBait->UnitPrice, 0);
		TestTrue(TEXT("免费普通饵使用无限库存"), FreeBait->bUnlimitedStock);
	}

	const FCatShopCatalogEntry* ChumOrder = Settings->CatalogEntries.FindByPredicate(
		[](const FCatShopCatalogEntry& Entry)
		{
			return Entry.EntryId == FName(TEXT("ShopChumOrder"));
		});
	TestNotNull(TEXT("项目默认商店包含基础窝料订单"), ChumOrder);
	if (ChumOrder)
	{
		TestTrue(TEXT("基础窝料订单本身可运行"), ChumOrder->IsRuntimeReady());
		TestEqual(TEXT("基础窝料交付为局内耗材"), ChumOrder->Kind, ECatShopEntryKind::RunConsumableGrant);
		TestEqual(TEXT("基础窝料指向 Chum_Basic 定义"), ChumOrder->DefinitionId, FName(TEXT("Chum_Basic")));
		TestEqual(TEXT("基础窝料订单价格"), ChumOrder->UnitPrice, 1);
		TestTrue(TEXT("基础窝料使用无限库存"), ChumOrder->bUnlimitedStock);
	}

	return !HasAnyErrors();
}

// 测试流程：不碰项目配置，只用全新构造的对象确认两处"没裁过"的哨兵仍然把路走死。
// 这两条锁的是同一件事：默认值必须能和"产品真的裁了 0"区分开，否则漏填就会静默变成免费品或 0 元开局。
bool FCatShopEconomySettingsUndecidedSentinelsFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatShopCatalogEntry UntouchedEntry;
	TestEqual(TEXT("目录项默认单价是未裁哨兵而不是 0"), UntouchedEntry.UnitPrice, -1);

	FCatShopCatalogEntry MissingPriceEntry;
	MissingPriceEntry.EntryId = TEXT("MissingPriceOrder");
	MissingPriceEntry.Kind = ECatShopEntryKind::EquipmentGrant;
	MissingPriceEntry.DefinitionId = TEXT("SomeDefinition");
	MissingPriceEntry.InitialStock = 1;
	TestFalse(TEXT("漏填价格的目录项不能进入运行库存"), MissingPriceEntry.IsRuntimeReady());

	FCatShopCatalogEntry DecidedFreeEntry = MissingPriceEntry;
	DecidedFreeEntry.UnitPrice = 0;
	TestTrue(TEXT("显式裁定 0 元的目录项仍然合法"), DecidedFreeEntry.IsRuntimeReady());

	UCatShopEconomySettings* Probe = NewObject<UCatShopEconomySettings>();
	TestNotNull(TEXT("可构造独立 ShopEconomy Settings 探针"), Probe);
	if (!Probe)
	{
		return false;
	}
	Probe->bEnableShopEconomyRuntime = true;
	Probe->MinimumFishSaleValue = 1;
	Probe->StartingTeamWalletBalance = -1;
	TestFalse(TEXT("没裁过起始资金时整个经济 runtime 关闭"), Probe->IsRuntimeEnabled());
	Probe->StartingTeamWalletBalance = 0;
	TestTrue(TEXT("显式裁定 0 起始资金时经济 runtime 可开"), Probe->IsRuntimeEnabled());

	FCatShopCatalogEntry DailyEntry = DecidedFreeEntry;
	DailyEntry.bDailyRestock = true;
	TestFalse(TEXT("标了每日进货却没给进货量的条目非法"), DailyEntry.IsRuntimeReady());
	DailyEntry.DailyRestockQuantity = 2;
	TestTrue(TEXT("给了正进货量的每日进货条目合法"), DailyEntry.IsRuntimeReady());
	DailyEntry.bUnlimitedStock = true;
	TestFalse(TEXT("永不缺货和每日进货不能同时成立"), DailyEntry.IsRuntimeReady());

	return !HasAnyErrors();
}

// 测试流程：只验证收鱼价档位表这一个纯函数——空表、乱序、非正价、过轻的鱼都不给价，合法表按档取价。
bool FCatShopEconomySettingsFishPurchasePriceTableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	int32 Price = 0;
	TestFalse(TEXT("空档位表不给任何价格"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice({}, 1.0, Price));
	TestEqual(TEXT("空档位表把输出清零"), Price, 0);

	FCatShopFishWeightPrice Light;
	Light.MinimumWeightKilograms = 0.5;
	Light.Price = 3;
	FCatShopFishWeightPrice Heavy;
	Heavy.MinimumWeightKilograms = 2.0;
	Heavy.Price = 9;

	const TArray<FCatShopFishWeightPrice> Table = {Light, Heavy};
	TestFalse(TEXT("比最轻一档还轻的鱼没有档位"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Table, 0.2, Price));
	TestTrue(TEXT("落在轻档的鱼按轻档取价"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Table, 1.5, Price));
	TestEqual(TEXT("轻档价格正确"), Price, 3);
	TestTrue(TEXT("正好压在重档下限的鱼按重档取价"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Table, 2.0, Price));
	TestEqual(TEXT("重档价格正确"), Price, 9);
	TestFalse(TEXT("非正重量不给价格"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Table, 0.0, Price));

	const TArray<FCatShopFishWeightPrice> Unsorted = {Heavy, Light};
	TestFalse(TEXT("重量不递增的档位表整体不可用"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Unsorted, 3.0, Price));

	FCatShopFishWeightPrice CheaperHeavy = Heavy;
	CheaperHeavy.Price = 1;
	const TArray<FCatShopFishWeightPrice> Regressing = {Light, CheaperHeavy};
	TestFalse(TEXT("越重越便宜的档位表整体不可用"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(Regressing, 3.0, Price));

	FCatShopFishWeightPrice FreePrice = Light;
	FreePrice.Price = 0;
	const TArray<FCatShopFishWeightPrice> ZeroPriced = {FreePrice};
	TestFalse(TEXT("零价档位不可用"),
		UCatShopEconomySettings::TryEvaluateFishPurchasePrice(ZeroPriced, 1.0, Price));

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
