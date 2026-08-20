#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"
#include "Tests/AutomationCommon.h"

namespace CatShopOrderChainTest
{
	/**
	 * ShopEconomy 默认配置覆盖守卫；本文件的每个用例都要在建 World 之前写好配置，
	 * 因为服务是在 WorldSubsystem 初始化时把钱包、目录和收鱼价一次性冻结下来的。
	 */
	struct FShopSettingsFixture
	{
		/** 被覆盖的 ShopEconomy Settings 默认对象。 */
		UCatShopEconomySettings* Settings = nullptr;

		/** 测试前 runtime gate；析构恢复。 */
		bool bSavedRuntime = false;

		/** 测试前初始团队钱包余额；析构恢复。 */
		int32 SavedStartingBalance = 0;

		/** 测试前最小售鱼金额；析构恢复。 */
		int32 SavedMinimumFishSaleValue = 1;

		/** 测试前免费普通饵目录项；析构恢复。 */
		FName SavedFreeOrdinaryBaitEntryId = NAME_None;

		/** 测试前免费保底竿目录项；析构恢复。 */
		FName SavedFreeStarterRodEntryId = NAME_None;

		/** 测试前收鱼价裁定状态；析构恢复。 */
		ECatDomainPolicy SavedFishPurchasePricePolicy = ECatDomainPolicy::Unset;

		/** 测试前收鱼价档位表；析构恢复。 */
		TArray<FCatShopFishWeightPrice> SavedFishPurchasePriceAnchors;

		/** 测试前完整商店目录；析构恢复。 */
		TArray<FCatShopCatalogEntry> SavedCatalogEntries;

		/** 构造流程：保存默认对象全部可变字段，再写入一个"经济能跑但什么都没配"的干净起点。 */
		FShopSettingsFixture()
		{
			Settings = GetMutableDefault<UCatShopEconomySettings>();
			if (!Settings)
			{
				return;
			}
			bSavedRuntime = Settings->bEnableShopEconomyRuntime;
			SavedStartingBalance = Settings->StartingTeamWalletBalance;
			SavedMinimumFishSaleValue = Settings->MinimumFishSaleValue;
			SavedFreeOrdinaryBaitEntryId = Settings->FreeOrdinaryBaitEntryId;
			SavedFreeStarterRodEntryId = Settings->FreeStarterRodEntryId;
			SavedFishPurchasePricePolicy = Settings->FishPurchasePricePolicy;
			SavedFishPurchasePriceAnchors = Settings->FishPurchasePriceAnchors;
			SavedCatalogEntries = Settings->CatalogEntries;
			Settings->bEnableShopEconomyRuntime = true;
			Settings->StartingTeamWalletBalance = 10;
			Settings->MinimumFishSaleValue = 1;
			Settings->FreeOrdinaryBaitEntryId = NAME_None;
			Settings->FreeStarterRodEntryId = NAME_None;
			Settings->FishPurchasePricePolicy = ECatDomainPolicy::Unset;
			Settings->FishPurchasePriceAnchors.Reset();
			Settings->CatalogEntries.Reset();
		}

		/** 析构流程：原样还回默认配置，避免本文件的临时目录和临时鱼价被后续自动化读到。 */
		~FShopSettingsFixture()
		{
			if (!Settings)
			{
				return;
			}
			Settings->bEnableShopEconomyRuntime = bSavedRuntime;
			Settings->StartingTeamWalletBalance = SavedStartingBalance;
			Settings->MinimumFishSaleValue = SavedMinimumFishSaleValue;
			Settings->FreeOrdinaryBaitEntryId = SavedFreeOrdinaryBaitEntryId;
			Settings->FreeStarterRodEntryId = SavedFreeStarterRodEntryId;
			Settings->FishPurchasePricePolicy = SavedFishPurchasePricePolicy;
			Settings->FishPurchasePriceAnchors = SavedFishPurchasePriceAnchors;
			Settings->CatalogEntries = SavedCatalogEntries;
		}

		/** 追加一条目录项；测试用的价格和库存都是刻度，不是飞书裁下来的数值。 */
		void AddEntry(const FName EntryId, const ECatShopEntryKind Kind, const FName DefinitionId,
			const int32 UnitPrice, const int32 InitialStock, const bool bUnlimitedStock,
			const bool bDailyRestock = false, const int32 DailyRestockQuantity = 0)
		{
			if (!Settings)
			{
				return;
			}
			FCatShopCatalogEntry Entry;
			Entry.EntryId = EntryId;
			Entry.Kind = Kind;
			Entry.DefinitionId = DefinitionId;
			Entry.UnitPrice = UnitPrice;
			Entry.InitialStock = InitialStock;
			Entry.bUnlimitedStock = bUnlimitedStock;
			Entry.bDailyRestock = bDailyRestock;
			Entry.DailyRestockQuantity = DailyRestockQuantity;
			Settings->CatalogEntries.Add(Entry);
		}

		/** 写入一张只有一档的收鱼价表；同样只是测试刻度，用来把"估价能用"和"估价没裁"分开验。 */
		void DecideFishPrice(const double MinimumWeightKilograms, const int32 Price)
		{
			if (!Settings)
			{
				return;
			}
			FCatShopFishWeightPrice Anchor;
			Anchor.MinimumWeightKilograms = MinimumWeightKilograms;
			Anchor.Price = Price;
			Settings->FishPurchasePricePolicy = ECatDomainPolicy::Enabled;
			Settings->FishPurchasePriceAnchors = {Anchor};
		}
	};

	/** Equipment 目录覆盖守卫；订单链测试需要装备库能在运行目录里查到一条真实定义。 */
	struct FEquipmentCatalogFixture
	{
		/** 被覆盖的 Equipment Settings 默认对象。 */
		UCatEquipmentSettings* Settings = nullptr;

		/** 测试前目录 SchemaVersion；析构恢复。 */
		int32 SavedContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

		/** 测试前目录数据修订；析构恢复。 */
		int64 SavedDataRevision = 0;

		/** 测试前目录来源戳；析构恢复。 */
		FCatDataCatalogSourceStamp SavedSourceStamp;

		/** 测试前正式定义列表；析构恢复。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 测试前 starter 鱼竿 ID；析构恢复。 */
		FName SavedStarterRodDefinitionId = NAME_None;

		/** 测试前 starter 鱼饵 ID；析构恢复。 */
		FName SavedStarterBaitDefinitionId = NAME_None;

		/** 测试前 starter 鱼漂 ID；析构恢复。 */
		FName SavedStarterFloatDefinitionId = NAME_None;

		/** 测试前维修浮木 ID；析构恢复。 */
		FName SavedDriftwoodDefinitionId = NAME_None;

		/**
		 * 测试前的一局耗材随身携带上限；只保存和还原，构造时不改写。
		 * 需要验"栈满了会怎样"的用例自己把 Settings->RunConsumableStackCapacity 压到一个小刻度，
		 * 其余用例照常用项目配置值，析构统一还回去。
		 */
		int32 SavedRunConsumableStackCapacity = 0;

		/** 本测试目录里唯一那条鱼竿的稳定 ID；商店订单也引用它。 */
		FName RodDefinitionId = TEXT("OrderChainRod");

		/** 本测试创建的瞬态鱼竿定义；加根后软引用目录才能稳定解析到测试结束。 */
		TObjectPtr<UCatEquipmentDefinition> RodDefinition = nullptr;

		/** 本测试目录里唯一那条普通饵的稳定 ID；耗材类订单引用它。 */
		FName BaitDefinitionId = TEXT("OrderChainBait");

		/** 本测试创建的瞬态普通饵定义（一局消耗品）；加根后软引用目录才能稳定解析到测试结束。 */
		TObjectPtr<UCatEquipmentDefinition> BaitDefinition = nullptr;

		/** 构造流程：保存原目录，再换成一份只含一条鱼竿和一条普通饵、没有 starter 与维修引用的最小可运行目录。 */
		FEquipmentCatalogFixture()
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			if (!Settings)
			{
				return;
			}
			SavedContentSchemaVersion = Settings->ContentSchemaVersion;
			SavedDataRevision = Settings->DataRevision;
			SavedSourceStamp = Settings->SourceStamp;
			SavedDefinitions = Settings->Definitions;
			SavedStarterRodDefinitionId = Settings->StarterRodDefinitionId;
			SavedStarterBaitDefinitionId = Settings->StarterBaitDefinitionId;
			SavedStarterFloatDefinitionId = Settings->StarterFloatDefinitionId;
			SavedDriftwoodDefinitionId = Settings->DriftwoodDefinitionId;
			SavedRunConsumableStackCapacity = Settings->RunConsumableStackCapacity;

			RodDefinition = NewObject<UCatEquipmentDefinition>(
				GetTransientPackage(), TEXT("CatShopOrderChainAutomationRod"));
			if (RodDefinition)
			{
				RodDefinition->AddToRoot();
				RodDefinition->bEnableRuntimeDefinition = true;
				RodDefinition->EquipmentDefinitionId = RodDefinitionId;
				RodDefinition->Kind = ECatEquipmentKind::Rod;
				RodDefinition->LoadoutSlotId = TEXT("Rod");
				RodDefinition->FunctionalRouteId = TEXT("OrderChainRodRoute");
				RodDefinition->MaximumRodDurability = 100.0;
				RodDefinition->RodStrength = 60.0;
				RodDefinition->MaximumLineLengthMeters = 80.0;
			}
			BaitDefinition = NewObject<UCatEquipmentDefinition>(
				GetTransientPackage(), TEXT("CatShopOrderChainAutomationBait"));
			if (BaitDefinition)
			{
				BaitDefinition->AddToRoot();
				BaitDefinition->bEnableRuntimeDefinition = true;
				BaitDefinition->EquipmentDefinitionId = BaitDefinitionId;
				BaitDefinition->Kind = ECatEquipmentKind::Bait;
				BaitDefinition->LoadoutSlotId = TEXT("Bait");
				BaitDefinition->FunctionalRouteId = TEXT("OrderChainBaitRoute");
				BaitDefinition->bRunConsumable = true;
			}

			Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
			Settings->DataRevision = 1;
			Settings->SourceStamp.SourceKind = TEXT("Automation");
			Settings->SourceStamp.SourceNodeToken = TEXT("CatShopOrderChainTest");
			Settings->SourceStamp.SourceRevision = 1;
			Settings->SourceStamp.SourceSliceName = TEXT("ShopOrderChain");
			Settings->StarterRodDefinitionId = NAME_None;
			Settings->StarterBaitDefinitionId = NAME_None;
			Settings->StarterFloatDefinitionId = NAME_None;
			Settings->DriftwoodDefinitionId = NAME_None;
			Settings->Definitions.Reset();
			if (RodDefinition)
			{
				Settings->Definitions.Add(RodDefinition.Get());
			}
			if (BaitDefinition)
			{
				Settings->Definitions.Add(BaitDefinition.Get());
			}
		}

		/** 析构流程：先还原目录字段，再解除瞬态定义的根引用。 */
		~FEquipmentCatalogFixture()
		{
			if (Settings)
			{
				Settings->ContentSchemaVersion = SavedContentSchemaVersion;
				Settings->DataRevision = SavedDataRevision;
				Settings->SourceStamp = SavedSourceStamp;
				Settings->Definitions = SavedDefinitions;
				Settings->StarterRodDefinitionId = SavedStarterRodDefinitionId;
				Settings->StarterBaitDefinitionId = SavedStarterBaitDefinitionId;
				Settings->StarterFloatDefinitionId = SavedStarterFloatDefinitionId;
				Settings->DriftwoodDefinitionId = SavedDriftwoodDefinitionId;
				Settings->RunConsumableStackCapacity = SavedRunConsumableStackCapacity;
			}
			if (RodDefinition)
			{
				RodDefinition->RemoveFromRoot();
			}
			if (BaitDefinition)
			{
				BaitDefinition->RemoveFromRoot();
			}
		}
	};

	/** 购买命令构造流程：填服务器身份、钱包版本和目录项；测试不提交价格或库存。 */
	static FCatShopPurchaseCommand MakePurchaseCommand(const FString& StableNetId, const int64 ExpectedRevision,
		const FName EntryId, const FGuid RequestId = FGuid::NewGuid())
	{
		FCatShopPurchaseCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.EntryId = EntryId;
		return Command;
	}

	/** 耗材栈读数流程：按定义 ID 从收货人快照里取当前数量；没有这一栈按 0 算，用例可以直接和期望值比。 */
	static int32 GetConsumableQuantity(const FCatEquipmentLoadoutSnapshot& Snapshot, const FName DefinitionId)
	{
		const FCatRunConsumableStack* Stack = Snapshot.Consumables.FindByPredicate(
			[DefinitionId](const FCatRunConsumableStack& Candidate)
			{
				return Candidate.DefinitionId == DefinitionId;
			});
		return Stack ? Stack->Quantity : 0;
	}

	/** 售鱼命令构造流程：调用方按参数报价，服务器仍会用重量自己核一次。 */
	static FCatShopFishSaleCommand MakeFishSaleCommand(const FString& StableNetId, const int64 ExpectedRevision,
		const ECatShopFishSaleSource SourceKind, const double WeightKilograms, const int32 SaleValue)
	{
		FCatShopFishSaleCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FGuid::NewGuid();
		Command.ItemsCommitId = FGuid::NewGuid();
		Command.SourceKind = SourceKind;
		Command.WeightKilograms = WeightKilograms;
		Command.SaleValue = SaleValue;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyFishSaleFailsClosedWithoutPriceTableTest,
	"Catfishing.Unit.ShopEconomy.Service.FishSaleFailsClosedWithoutDecidedPriceTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyFishSaleAcceptsEverySourceAtAppraisedValueTest,
	"Catfishing.Unit.ShopEconomy.Service.FishSaleAcceptsEverySourceOnlyAtAppraisedValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyDailyRestockTest,
	"Catfishing.Unit.ShopEconomy.Service.DailyRestockOnlyRefillsDailyEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyFreeStarterRodTest,
	"Catfishing.Unit.ShopEconomy.Service.FreeStarterRodIsUnlimitedAndSeparateFromFreeBait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyPublicSnapshotTest,
	"Catfishing.Unit.ShopEconomy.Service.PublicSnapshotAndBroadcastExposeWalletAndLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorChainTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.PurchaseCreatesTeamLibraryInstanceAndConfirmsDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorConsumableAndTakeTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.ConsumableOrderGrantsRecipientStackAndLibraryTakeRemovesInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorConsumableCapacityGateTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.FullConsumableStackRejectsOrderBeforeAnyPayment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：收鱼价没被裁定时，估价接口不给价、预检不放行、入账整笔拒绝，且钱包和账本一个字都不动。
// 这条锁的是 E1 最重要的一点：飞书没给斜率之前，售鱼这条路必须整条走不通，而不是按某个工程默认价先跑起来。
bool FCatShopEconomyFishSaleFailsClosedWithoutPriceTableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FShopSettingsFixture Fixture;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建收鱼价 fail-closed 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("收鱼价 fail-closed 测试服务可创建"), Service);
		if (Service)
		{
			int32 Appraised = 7;
			TestFalse(TEXT("没裁过收鱼价时估价接口不给价"), Service->TryAppraiseFishSale(2.0, Appraised));
			TestEqual(TEXT("估价失败时输出被清零"), Appraised, 0);

			const FCatShopWalletSnapshot InitialWallet = Service->GetWalletSnapshot();
			const FCatShopFishSaleCommand SaleCommand = CatShopOrderChainTest::MakeFishSaleCommand(
				TEXT("PlayerA"), InitialWallet.Revision, ECatShopFishSaleSource::SharedFishTank, 2.0, 9);

			ECatDomainCommandError ValidateError = ECatDomainCommandError::None;
			int64 ValidateWalletRevision = 0;
			TestFalse(TEXT("没裁过收鱼价时售鱼预检不放行"),
				Service->ValidateFishSale(SaleCommand, ValidateError, ValidateWalletRevision));
			TestEqual(TEXT("预检返回未裁决而不是无效载荷"), ValidateError, ECatDomainCommandError::PolicyUndecided);

			const FCatShopTransactionResult Sale = Service->ApplyFishSale(SaleCommand);
			TestFalse(TEXT("没裁过收鱼价时售鱼不入账"), Sale.Command.bCommitted);
			TestEqual(TEXT("售鱼返回未裁决"), Sale.Command.Error, ECatDomainCommandError::PolicyUndecided);
			TestEqual(TEXT("被拒售鱼不改钱包余额"), Service->GetWalletSnapshot().Balance, InitialWallet.Balance);
			TestEqual(TEXT("被拒售鱼不改钱包版本"), Service->GetWalletSnapshot().Revision, InitialWallet.Revision);
			TestEqual(TEXT("被拒售鱼不写账本"), Service->GetTransactionLedgerSnapshot().Num(), 0);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：裁定收鱼价之后，自己鱼护、共用鱼缸和偷来的鱼三种来源都收，但只按服务器估出来的价收；
// 没声明来源、报价对不上估价、鱼太轻查不到档位这三种都必须拒绝。
bool FCatShopEconomyFishSaleAcceptsEverySourceAtAppraisedValueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FShopSettingsFixture Fixture;
	Fixture.Settings->StartingTeamWalletBalance = 0;
	Fixture.DecideFishPrice(0.5, 4);
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建售鱼来源测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("售鱼来源测试服务可创建"), Service);
		if (Service)
		{
			int32 Appraised = 0;
			TestTrue(TEXT("裁定后估价接口给出价格"), Service->TryAppraiseFishSale(1.0, Appraised));
			TestEqual(TEXT("估价按档位表取值"), Appraised, 4);
			TestFalse(TEXT("比最轻一档还轻的鱼仍然没有价格"), Service->TryAppraiseFishSale(0.1, Appraised));

			const ECatShopFishSaleSource Sources[] = {
				ECatShopFishSaleSource::PersonalGuard,
				ECatShopFishSaleSource::SharedFishTank,
				ECatShopFishSaleSource::StolenEscrow};
			int32 ExpectedBalance = 0;
			for (const ECatShopFishSaleSource Source : Sources)
			{
				const FCatShopTransactionResult Sale = Service->ApplyFishSale(
					CatShopOrderChainTest::MakeFishSaleCommand(TEXT("PlayerA"),
						Service->GetWalletSnapshot().Revision, Source, 1.0, 4));
				ExpectedBalance += 4;
				TestTrue(TEXT("三种来源的鱼都能卖"), Sale.Command.bCommitted);
				TestEqual(TEXT("售鱼按估价入账"), Sale.Wallet.Balance, ExpectedBalance);
				TestEqual(TEXT("账本记录这条鱼的来源"), Sale.Transaction.FishSource, Source);
			}

			const FCatShopTransactionResult Undeclared = Service->ApplyFishSale(
				CatShopOrderChainTest::MakeFishSaleCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, ECatShopFishSaleSource::Unknown, 1.0, 4));
			TestFalse(TEXT("没声明来源的鱼不收"), Undeclared.Command.bCommitted);
			TestEqual(TEXT("没声明来源返回无效载荷"), Undeclared.Command.Error, ECatDomainCommandError::InvalidPayload);

			const FCatShopTransactionResult Forged = Service->ApplyFishSale(
				CatShopOrderChainTest::MakeFishSaleCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, ECatShopFishSaleSource::PersonalGuard, 1.0, 400));
			TestFalse(TEXT("报价高于估价的售鱼被拒"), Forged.Command.bCommitted);
			TestEqual(TEXT("报价对不上估价返回无效载荷"), Forged.Command.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("伪造报价没有进钱包"), Service->GetWalletSnapshot().Balance, ExpectedBalance);

			const FCatShopTransactionResult TooLight = Service->ApplyFishSale(
				CatShopOrderChainTest::MakeFishSaleCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, ECatShopFishSaleSource::PersonalGuard, 0.1, 4));
			TestFalse(TEXT("查不到档位的鱼卖不掉"), TooLight.Command.bCommitted);
			TestEqual(TEXT("查不到档位返回未裁决"), TooLight.Command.Error, ECatDomainCommandError::PolicyUndecided);
			TestEqual(TEXT("售鱼账本只包含三条成功交易"), Service->GetTransactionLedgerSnapshot().Num(), 3);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：一份目录里同时放每日进货的特殊饵、永不缺货的基础补给和一局只进一次的限量竿；
// 推进一天之后只有每日进货那条回满，另外两条保持原样；同一天重复推进不补第二次货，收摊之后也不再补货。
bool FCatShopEconomyDailyRestockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FShopSettingsFixture Fixture;
	Fixture.AddEntry(TEXT("DailyBait"), ECatShopEntryKind::RunConsumableGrant, TEXT("SpecialBait"), 1, 2, false, true, 2);
	Fixture.AddEntry(TEXT("AlwaysStocked"), ECatShopEntryKind::RunConsumableGrant, TEXT("BasicSupply"), 1, 0, true);
	Fixture.AddEntry(TEXT("OneShotRod"), ECatShopEntryKind::EquipmentGrant, TEXT("SomeRod"), 1, 1, false);
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建每日进货测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("每日进货测试服务可创建"), Service);
		if (Service)
		{
			const auto BuyOnce = [Service](const FName EntryId)
			{
				return Service->PurchaseCatalogEntry(CatShopOrderChainTest::MakePurchaseCommand(
					TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, EntryId));
			};
			TestTrue(TEXT("买掉第一份每日进货饵"), BuyOnce(TEXT("DailyBait")).Command.bCommitted);
			TestTrue(TEXT("买掉第二份每日进货饵"), BuyOnce(TEXT("DailyBait")).Command.bCommitted);
			TestTrue(TEXT("买掉唯一那根限量竿"), BuyOnce(TEXT("OneShotRod")).Command.bCommitted);

			FCatShopStockSnapshot Stock;
			TestTrue(TEXT("可读每日进货饵库存"), Service->TryGetStockSnapshot(TEXT("DailyBait"), Stock));
			TestEqual(TEXT("当天的每日进货饵已经卖光"), Stock.RemainingStock, 0);

			TestTrue(TEXT("推进到第二天补货成功"), Service->AdvanceShopDay(2));
			TestTrue(TEXT("补货后可读每日进货饵库存"), Service->TryGetStockSnapshot(TEXT("DailyBait"), Stock));
			TestEqual(TEXT("每日进货饵回到当日进货量"), Stock.RemainingStock, 2);
			TestTrue(TEXT("补货后可读限量竿库存"), Service->TryGetStockSnapshot(TEXT("OneShotRod"), Stock));
			TestEqual(TEXT("一局只进一次的限量竿不跟着补货"), Stock.RemainingStock, 0);
			TestTrue(TEXT("补货后可读永不缺货项库存"), Service->TryGetStockSnapshot(TEXT("AlwaysStocked"), Stock));
			TestTrue(TEXT("永不缺货项仍然是无限库存"), Stock.bUnlimitedStock);

			TestTrue(TEXT("补货后还能再买两份每日进货饵"), BuyOnce(TEXT("DailyBait")).Command.bCommitted);
			TestFalse(TEXT("同一天重复推进不再补货"), Service->AdvanceShopDay(2));
			TestFalse(TEXT("回到过去的天序号不补货"), Service->AdvanceShopDay(1));
			TestTrue(TEXT("同一天没补货，库存仍是买掉一份之后的数"),
				Service->TryGetStockSnapshot(TEXT("DailyBait"), Stock));
			TestEqual(TEXT("同一天重复推进没有凭空多出一份"), Stock.RemainingStock, 1);

			Service->CloseCommands();
			TestFalse(TEXT("商人猫收摊后不再进货"), Service->AdvanceShopDay(3));
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：1 级保底竿和普通饵是两条各自配置的免费自取入口，都必须是 0 元且无限库存；
// 一条有限库存的"免费"竿不能走免费自取，否则保底竿会在某天被领光而不再保底。
bool FCatShopEconomyFreeStarterRodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FShopSettingsFixture Fixture;
	Fixture.AddEntry(TEXT("FreeStarterRod"), ECatShopEntryKind::EquipmentGrant, TEXT("StarterRodT1"), 0, 0, true);
	Fixture.AddEntry(TEXT("FreeBasicBait"), ECatShopEntryKind::RunConsumableGrant, TEXT("BasicBait"), 0, 0, true);
	Fixture.AddEntry(TEXT("LimitedFreeRod"), ECatShopEntryKind::EquipmentGrant, TEXT("StarterRodT1"), 0, 1, false);
	Fixture.Settings->FreeStarterRodEntryId = TEXT("FreeStarterRod");
	Fixture.Settings->FreeOrdinaryBaitEntryId = TEXT("FreeBasicBait");
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建保底竿测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("保底竿测试服务可创建"), Service);
		if (Service)
		{
			const int32 InitialBalance = Service->GetWalletSnapshot().Balance;
			for (int32 Attempt = 0; Attempt < 3; ++Attempt)
			{
				const FCatShopTransactionResult Claim = Service->ClaimFreeCatalogEntry(
					CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
						Service->GetWalletSnapshot().Revision, TEXT("FreeStarterRod")));
				TestTrue(TEXT("保底竿可以反复免费自取"), Claim.Command.bCommitted);
				TestEqual(TEXT("保底竿走免费自取账本"), Claim.Transaction.Kind, ECatShopTransactionKind::FreeClaim);
				TestTrue(TEXT("保底竿使用无限库存"), Claim.Stock.bUnlimitedStock);
			}
			TestEqual(TEXT("免费自取不花公款"), Service->GetWalletSnapshot().Balance, InitialBalance);

			const FCatShopTransactionResult FreeBait = Service->ClaimFreeCatalogEntry(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, TEXT("FreeBasicBait")));
			TestTrue(TEXT("普通饵仍然是另一条独立的免费自取入口"), FreeBait.Command.bCommitted);

			const FCatShopTransactionResult LimitedFree = Service->ClaimFreeCatalogEntry(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, TEXT("LimitedFreeRod")));
			TestFalse(TEXT("没被配置成免费自取的条目不能白拿"), LimitedFree.Command.bCommitted);
			TestEqual(TEXT("没被配置成免费自取返回未裁决"), LimitedFree.Command.Error,
				ECatDomainCommandError::PolicyUndecided);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：买一次、卖一次之后，公开快照必须同时给出公款余额和两条流水，且每笔真实提交各广播一次；
// 被拒绝的命令一条广播都不发，公开流水里也不带服务器私有身份。
bool FCatShopEconomyPublicSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FShopSettingsFixture Fixture;
	Fixture.AddEntry(TEXT("ShopRod"), ECatShopEntryKind::EquipmentGrant, TEXT("SomeRod"), 3, 1, false);
	Fixture.DecideFishPrice(0.5, 4);
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建公开快照测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("公开快照测试服务可创建"), Service);
		if (Service)
		{
			int32 BroadcastCount = 0;
			TArray<FCatShopPublicTransaction> Broadcasts;
			FDelegateHandle Handle = Service->OnPublicTransactionCommitted.AddLambda(
				[&BroadcastCount, &Broadcasts](const FCatShopPublicTransaction& Public)
			{
				++BroadcastCount;
				Broadcasts.Add(Public);
			});

			const FCatShopTransactionResult Purchase = Service->PurchaseCatalogEntry(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, TEXT("ShopRod")));
			TestTrue(TEXT("购买成功"), Purchase.Command.bCommitted);
			TestEqual(TEXT("购买广播一次"), BroadcastCount, 1);

			const FCatShopTransactionResult Sale = Service->ApplyFishSale(
				CatShopOrderChainTest::MakeFishSaleCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, ECatShopFishSaleSource::PersonalGuard, 1.0, 4));
			TestTrue(TEXT("售鱼成功"), Sale.Command.bCommitted);
			TestEqual(TEXT("售鱼再广播一次"), BroadcastCount, 2);

			const FCatShopTransactionResult SoldOut = Service->PurchaseCatalogEntry(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					Service->GetWalletSnapshot().Revision, TEXT("ShopRod")));
			TestFalse(TEXT("售罄后购买失败"), SoldOut.Command.bCommitted);
			TestEqual(TEXT("被拒命令不广播"), BroadcastCount, 2);

			if (Broadcasts.Num() == 2)
			{
				TestEqual(TEXT("第一条广播是购买"), Broadcasts[0].Kind, ECatShopTransactionKind::Purchase);
				TestEqual(TEXT("购买广播带负增量"), Broadcasts[0].WalletDelta, -3);
				TestNull(TEXT("服务自己构造的广播不带公开身份"), Broadcasts[0].ActorPlayerState.Get());
				TestEqual(TEXT("第二条广播是售鱼"), Broadcasts[1].Kind, ECatShopTransactionKind::FishSale);
				TestEqual(TEXT("售鱼广播带来源"), Broadcasts[1].FishSource, ECatShopFishSaleSource::PersonalGuard);
			}

			const FCatShopPublicEconomySnapshot Snapshot = Service->BuildPublicSnapshot();
			TestEqual(TEXT("公开快照给出当前余额"), Snapshot.Balance, Service->GetWalletSnapshot().Balance);
			TestEqual(TEXT("公开快照给出当前钱包版本"), Snapshot.WalletRevision, Service->GetWalletSnapshot().Revision);
			TestEqual(TEXT("公开快照包含两条流水"), Snapshot.Transactions.Num(), 2);
			TestEqual(TEXT("公开快照的商店天序号从 0 起"), Snapshot.ShopDayIndex, 0);
			TestTrue(TEXT("推进一天后快照跟着走"), Service->AdvanceShopDay(1));
			TestEqual(TEXT("公开快照反映商店已经换到第一天"), Service->BuildPublicSnapshot().ShopDayIndex, 1);

			Service->OnPublicTransactionCommitted.Remove(Handle);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：走完"付款 → 装备库真的多出一件东西 → 拿实例当回执 → 账本推到已交付"这条链，
// 再用同一个 RequestId 重打确认不会买第二次、也不会入库第二件；
// 最后用一条指向不存在定义的订单确认它在扣钱之前就被挡下——商店没有退款写口，先扣钱再发现东西发不出去就是白扣。
bool FCatShopOrderCoordinatorChainTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FEquipmentCatalogFixture EquipmentFixture;
	CatShopOrderChainTest::FShopSettingsFixture ShopFixture;
	ShopFixture.AddEntry(TEXT("RodOrder"), ECatShopEntryKind::EquipmentGrant,
		EquipmentFixture.RodDefinitionId, 3, 1, false);
	ShopFixture.AddEntry(TEXT("GhostOrder"), ECatShopEntryKind::EquipmentGrant, TEXT("NotInCatalog"), 2, 1, false);
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建订单链测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
		UCatTeamEquipmentLibrary* Library = World ? World->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
		UCatShopEconomyService* Service = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("订单协调器可创建"), Coordinator);
		TestNotNull(TEXT("团队装备库可创建"), Library);
		TestNotNull(TEXT("商店服务可创建"), Service);
		if (Coordinator && Library && Service)
		{
			TestEqual(TEXT("开局装备库是空的"), Library->GetSnapshot().Instances.Num(), 0);

			const FGuid PurchaseRequestId = FGuid::NewGuid();
			const FCatShopPurchaseCommand PurchaseCommand = CatShopOrderChainTest::MakePurchaseCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, TEXT("RodOrder"), PurchaseRequestId);
			const FCatShopOrderResult Order = Coordinator->SubmitPurchase(PurchaseCommand);
			TestTrue(TEXT("订单付款成功"), Order.Transaction.Command.bCommitted);
			TestTrue(TEXT("交付确认成功"), Order.Delivery.bCommitted);
			TestEqual(TEXT("交付无错误"), Order.Delivery.Error, ECatDomainCommandError::None);
			TestTrue(TEXT("装备库真的造出了一件实物"), Order.Instance.InstanceId.IsValid());
			TestEqual(TEXT("实物指向订单里的定义"), Order.Instance.DefinitionId, EquipmentFixture.RodDefinitionId);
			TestEqual(TEXT("实物从定义冻结了类别"), Order.Instance.Kind, ECatEquipmentKind::Rod);
			TestEqual(TEXT("装备库多了一件东西"), Library->GetSnapshot().Instances.Num(), 1);
			TestEqual(TEXT("装备库版本推进到 1"), Library->GetSnapshot().Revision, static_cast<int64>(1));
			TestEqual(TEXT("账本推到已交付"), Order.Transaction.Transaction.DeliveryState,
				ECatShopDeliveryState::Delivered);
			TestEqual(TEXT("账本回执就是那件实物"), Order.Transaction.Transaction.DeliveryReceiptId,
				Order.Instance.InstanceId);

			const FCatShopOrderResult Replay = Coordinator->SubmitPurchase(PurchaseCommand);
			TestFalse(TEXT("同 RequestId 重打不再付一次款"), Replay.Transaction.Command.bCommitted);
			TestEqual(TEXT("重打返回首次订单终态"), Replay.Transaction.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重打拿回同一件实物"), Replay.Instance.InstanceId, Order.Instance.InstanceId);
			TestEqual(TEXT("重打没有让装备库多出第二件"), Library->GetSnapshot().Instances.Num(), 1);

			const FCatShopWalletSnapshot WalletBeforeGhost = Service->GetWalletSnapshot();
			const int32 LedgerCountBeforeGhost = Service->GetTransactionLedgerSnapshot().Num();
			const FCatShopOrderResult Ghost = Coordinator->SubmitPurchase(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					WalletBeforeGhost.Revision, TEXT("GhostOrder")));
			TestFalse(TEXT("指向不存在定义的订单在扣钱之前就被挡住"), Ghost.Transaction.Command.bCommitted);
			TestEqual(TEXT("订单这一段报依赖不可用"), Ghost.Transaction.Command.Error,
				ECatDomainCommandError::DependencyUnavailable);
			TestFalse(TEXT("查不到定义时交付不成功"), Ghost.Delivery.bCommitted);
			TestEqual(TEXT("查不到定义返回依赖不可用"), Ghost.Delivery.Error,
				ECatDomainCommandError::DependencyUnavailable);
			TestFalse(TEXT("查不到定义时没有造出实物"), Ghost.Instance.InstanceId.IsValid());
			TestEqual(TEXT("装备库没有因失败订单多出东西"), Library->GetSnapshot().Instances.Num(), 1);
			TestFalse(TEXT("被挡住的订单没有写进账本"), Ghost.Transaction.Transaction.TransactionId.IsValid());
			TestEqual(TEXT("被挡住的订单不写第二条账本记录"),
				Service->GetTransactionLedgerSnapshot().Num(), LedgerCountBeforeGhost);
			TestEqual(TEXT("被挡住的订单一分钱都没扣"), Service->GetWalletSnapshot().Balance,
				WalletBeforeGhost.Balance);
			TestEqual(TEXT("被挡住的订单不推进钱包版本"), Service->GetWalletSnapshot().Revision,
				WalletBeforeGhost.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：
// 1. 免费领一条 RunConsumableGrant 目录项但不给收货人：订单在提交之前就被挡住，两段都报依赖不可用，账本里根本没有这条记录，装备库不多东西。
// 2. 同 RequestId 带上收货人的 Equipment 组件重打：这次订单才真的成立，交付落到收货人耗材栈（数量 1）、账本推到已交付、回执就是订单 RequestId、装备库仍然是空的。
// 3. 再重打一次：AlreadyResolved，数量仍是 1。
// 4. 买一条装备类订单进库，然后按实例取走：版本错的取用被 RevisionConflict 挡住并且库不变；正确版本取走后库变空、按订
// 单仍能找回那件实物（供订单重放）、
//    同 RequestId 重放返回 AlreadyResolved 且不会再取第二次、换新 RequestId 再取同一实例返回 NotFound。
bool FCatShopOrderCoordinatorConsumableAndTakeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FEquipmentCatalogFixture EquipmentFixture;
	CatShopOrderChainTest::FShopSettingsFixture ShopFixture;
	ShopFixture.AddEntry(TEXT("FreeBait"), ECatShopEntryKind::RunConsumableGrant,
		EquipmentFixture.BaitDefinitionId, 0, 0, true);
	ShopFixture.AddEntry(TEXT("RodOrder"), ECatShopEntryKind::EquipmentGrant,
		EquipmentFixture.RodDefinitionId, 3, 1, false);
	ShopFixture.Settings->FreeOrdinaryBaitEntryId = TEXT("FreeBait");
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建耗材订单/取用测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
		UCatTeamEquipmentLibrary* Library = World ? World->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
		UCatShopEconomyService* Service = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Equipment = Host ? NewObject<UCatEquipmentComponent>(Host) : nullptr;
		if (Host && Equipment)
		{
			Host->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
		}
		TestNotNull(TEXT("订单协调器可创建"), Coordinator);
		TestNotNull(TEXT("团队装备库可创建"), Library);
		TestNotNull(TEXT("商店服务可创建"), Service);
		TestNotNull(TEXT("收货人 Equipment 组件可创建"), Equipment);
		if (Coordinator && Library && Service && Equipment)
		{
			const FGuid BaitRequestId = FGuid::NewGuid();
			const FCatShopPurchaseCommand BaitCommand = CatShopOrderChainTest::MakePurchaseCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, TEXT("FreeBait"), BaitRequestId);
			const FCatShopOrderResult NoRecipient = Coordinator->SubmitFreeClaim(BaitCommand);
			TestFalse(TEXT("没有收货人时订单在提交之前就被挡住"), NoRecipient.Transaction.Command.bCommitted);
			TestEqual(TEXT("没有收货人时订单这一段报依赖不可用"), NoRecipient.Transaction.Command.Error, ECatDomainCommandError::DependencyUnavailable);
			TestEqual(TEXT("没有收货人时交付报依赖不可用"), NoRecipient.Delivery.Error, ECatDomainCommandError::DependencyUnavailable);
			TestFalse(TEXT("被挡住的订单没有写进账本"), NoRecipient.Transaction.Transaction.TransactionId.IsValid());
			TestEqual(TEXT("被挡住的订单不写账本记录"), Service->GetTransactionLedgerSnapshot().Num(), 0);
			TestEqual(TEXT("耗材订单不会往装备库塞东西"), Library->GetSnapshot().Instances.Num(), 0);

			const FCatShopOrderResult Delivered = Coordinator->SubmitFreeClaim(BaitCommand, Equipment);
			TestTrue(TEXT("补上收货人后同一个 RequestId 这次才真的下单"), Delivered.Transaction.Command.bCommitted);
			TestTrue(TEXT("带收货人重打时交付确认成功"), Delivered.Delivery.bCommitted);
			TestEqual(TEXT("账本推到已交付"), Delivered.Transaction.Transaction.DeliveryState, ECatShopDeliveryState::Delivered);
			TestEqual(TEXT("耗材订单的回执就是订单 RequestId"), Delivered.Transaction.Transaction.DeliveryReceiptId, BaitRequestId);
			TestFalse(TEXT("耗材订单不带装备库实例"), Delivered.Instance.InstanceId.IsValid());
			TestEqual(TEXT("收货人耗材栈多了 1 份饵"),
				CatShopOrderChainTest::GetConsumableQuantity(Equipment->GetSnapshot(), EquipmentFixture.BaitDefinitionId), 1);
			TestEqual(TEXT("装备库仍然是空的"), Library->GetSnapshot().Instances.Num(), 0);

			const FCatShopOrderResult Replay = Coordinator->SubmitFreeClaim(BaitCommand, Equipment);
			TestEqual(TEXT("已交付的耗材订单重放返回 AlreadyResolved"), Replay.Delivery.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重放不再给第二份饵"),
				CatShopOrderChainTest::GetConsumableQuantity(Equipment->GetSnapshot(), EquipmentFixture.BaitDefinitionId), 1);

			const FCatShopOrderResult RodOrder = Coordinator->SubmitPurchase(CatShopOrderChainTest::MakePurchaseCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, TEXT("RodOrder")));
			TestTrue(TEXT("装备类订单入库成功"), RodOrder.Delivery.bCommitted);
			TestEqual(TEXT("装备类订单后库里一件"), Library->GetSnapshot().Instances.Num(), 1);

			FCatTeamEquipmentTakeCommand Take;
			Take.Context.RequestId = FGuid::NewGuid();
			Take.Context.StableNetId = TEXT("PlayerB");
			Take.Context.ExpectedRevision = Library->GetSnapshot().Revision + 1;
			Take.InstanceId = RodOrder.Instance.InstanceId;
			const FCatTeamEquipmentGrantResult StaleTake = Library->TakeInstance(Take);
			TestEqual(TEXT("版本前提错误的取用被 RevisionConflict 挡住"), StaleTake.Command.Error, ECatDomainCommandError::RevisionConflict);
			TestEqual(TEXT("被挡住的取用不动库"), Library->GetSnapshot().Instances.Num(), 1);

			Take.Context.RequestId = FGuid::NewGuid();
			Take.Context.ExpectedRevision = Library->GetSnapshot().Revision;
			const FCatTeamEquipmentGrantResult Taken = Library->TakeInstance(Take);
			TestTrue(TEXT("正确版本的取用成功"), Taken.Command.bCommitted);
			TestEqual(TEXT("取走的就是那件实物"), Taken.Instance.InstanceId, RodOrder.Instance.InstanceId);
			TestEqual(TEXT("取走后库变空"), Library->GetSnapshot().Instances.Num(), 0);
			TestEqual(TEXT("取走推进库版本"), Library->GetSnapshot().Revision, Taken.Command.Revision);
			FCatTeamEquipmentInstance FoundAfterTake;
			TestTrue(TEXT("取走后按订单仍能找回那件实物（订单重放要回执）"),
				Library->TryFindInstanceBySourceTransaction(RodOrder.Transaction.Transaction.TransactionId, FoundAfterTake));
			TestEqual(TEXT("找回的就是被取走那件"), FoundAfterTake.InstanceId, RodOrder.Instance.InstanceId);

			const FCatTeamEquipmentGrantResult TakeReplay = Library->TakeInstance(Take);
			TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved"), TakeReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重放拿回同一件实物"), TakeReplay.Instance.InstanceId, RodOrder.Instance.InstanceId);
			Take.Context.RequestId = FGuid::NewGuid();
			Take.Context.ExpectedRevision = Library->GetSnapshot().Revision;
			const FCatTeamEquipmentGrantResult TakeAgain = Library->TakeInstance(Take);
			TestEqual(TEXT("已取走的实例换新 RequestId 再取返回 NotFound"), TakeAgain.Command.Error, ECatDomainCommandError::NotFound);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：把随身携带上限压到 2，先用两笔正常订单把窝料栈买到顶，再提交第三笔。
// 第三笔必须在扣钱之前就被拒：订单不成立、账本不多一条记录、钱包余额和版本一个都不动、限量条目的剩余库存和库存版本也
// 不动、耗材栈仍然是 2。锁的是"先扣钱后发货、发不出去只打一行日志"那个缺陷——商店服务里没有任何退款写口，
// 钱一旦划走就找不回来，所以交付前提必须全部问在扣钱之前。
bool FCatShopOrderCoordinatorConsumableCapacityGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderChainTest::FEquipmentCatalogFixture EquipmentFixture;
	CatShopOrderChainTest::FShopSettingsFixture ShopFixture;
	// 限量、收费的耗材条目：收费才能看出钱包动没动，限量才能看出库存动没动。
	ShopFixture.AddEntry(TEXT("ChumOrder"), ECatShopEntryKind::RunConsumableGrant,
		EquipmentFixture.BaitDefinitionId, 1, 4, false);
	if (EquipmentFixture.Settings)
	{
		EquipmentFixture.Settings->RunConsumableStackCapacity = 2;
	}
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建耗材上限订单测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
		UCatShopEconomyService* Service = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatEquipmentComponent* Equipment = Host ? NewObject<UCatEquipmentComponent>(Host) : nullptr;
		if (Host && Equipment)
		{
			Host->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
		}
		TestNotNull(TEXT("订单协调器可创建"), Coordinator);
		TestNotNull(TEXT("商店服务可创建"), Service);
		TestNotNull(TEXT("收货人 Equipment 组件可创建"), Equipment);
		if (Coordinator && Service && Equipment)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FCatShopOrderResult Filled = Coordinator->SubmitPurchase(
					CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
						Service->GetWalletSnapshot().Revision, TEXT("ChumOrder")), Equipment);
				TestTrue(TEXT("上限之内的窝料订单正常成交"), Filled.Delivery.bCommitted);
			}
			TestEqual(TEXT("买满之后耗材栈到顶是 2"),
				CatShopOrderChainTest::GetConsumableQuantity(Equipment->GetSnapshot(), EquipmentFixture.BaitDefinitionId), 2);

			const FCatShopWalletSnapshot WalletBefore = Service->GetWalletSnapshot();
			FCatShopStockSnapshot StockBefore;
			TestTrue(TEXT("满栈之前能读到窝料条目库存"), Service->TryGetStockSnapshot(TEXT("ChumOrder"), StockBefore));
			const int32 LedgerCountBefore = Service->GetTransactionLedgerSnapshot().Num();

			const FCatShopOrderResult Overflow = Coordinator->SubmitPurchase(
				CatShopOrderChainTest::MakePurchaseCommand(TEXT("PlayerA"),
					WalletBefore.Revision, TEXT("ChumOrder")), Equipment);
			TestFalse(TEXT("栈满时订单不成立"), Overflow.Transaction.Command.bCommitted);
			TestEqual(TEXT("栈满时订单这一段就报 CapacityExceeded"), Overflow.Transaction.Command.Error,
				ECatDomainCommandError::CapacityExceeded);
			TestEqual(TEXT("栈满时交付这一段同样报 CapacityExceeded"), Overflow.Delivery.Error,
				ECatDomainCommandError::CapacityExceeded);
			TestFalse(TEXT("栈满时没有生成账本记录"), Overflow.Transaction.Transaction.TransactionId.IsValid());

			const FCatShopWalletSnapshot WalletAfter = Service->GetWalletSnapshot();
			TestEqual(TEXT("被拒的订单一分钱都没扣"), WalletAfter.Balance, WalletBefore.Balance);
			TestEqual(TEXT("被拒的订单不推进钱包版本"), WalletAfter.Revision, WalletBefore.Revision);
			FCatShopStockSnapshot StockAfter;
			TestTrue(TEXT("被拒之后仍能读到窝料条目库存"), Service->TryGetStockSnapshot(TEXT("ChumOrder"), StockAfter));
			TestEqual(TEXT("被拒的订单不扣库存"), StockAfter.RemainingStock, StockBefore.RemainingStock);
			TestEqual(TEXT("被拒的订单不推进库存版本"), StockAfter.Revision, StockBefore.Revision);
			TestEqual(TEXT("被拒的订单不写账本"), Service->GetTransactionLedgerSnapshot().Num(), LedgerCountBefore);
			TestEqual(TEXT("被拒之后耗材栈仍是 2"),
				CatShopOrderChainTest::GetConsumableQuantity(Equipment->GetSnapshot(), EquipmentFixture.BaitDefinitionId), 2);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
