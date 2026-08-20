#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "Tests/AutomationCommon.h"

namespace CatShopEconomyServiceTest
{
	/** 默认 Settings 覆盖守卫；测试期间写入可运行经济配置，析构恢复，避免污染其他自动化。 */
	struct FShopSettingsOverride
	{
		/** 被覆盖的 ShopEconomy Settings 默认对象；只在测试生命周期内写入。 */
		UCatShopEconomySettings* Settings = nullptr;

		/** 测试前 runtime gate 原值；析构时恢复。 */
		bool bSavedRuntime = false;

		/** 测试前初始团队钱包余额；析构时恢复。 */
		int32 SavedStartingBalance = 0;

		/** 测试前最小售鱼金额；析构时恢复。 */
		int32 SavedMinimumFishSaleValue = 1;

		/** 测试前免费普通饵目录项；析构时恢复。 */
		FName SavedFreeOrdinaryBaitEntryId = NAME_None;

		/** 测试前 1 级保底竿免费自取目录项；析构时恢复。 */
		FName SavedFreeStarterRodEntryId = NAME_None;

		/** 测试前收鱼价裁定状态；析构时恢复。 */
		ECatDomainPolicy SavedFishPurchasePricePolicy = ECatDomainPolicy::Unset;

		/** 测试前收鱼价档位表；析构时恢复。 */
		TArray<FCatShopFishWeightPrice> SavedFishPurchasePriceAnchors;

		/** 测试前完整商店目录；析构时恢复。 */
		TArray<FCatShopCatalogEntry> SavedCatalogEntries;

		/** 构造流程：保存默认对象全部可变字段，再写入测试显式 runtime gate 和基础钱包。 */
		FShopSettingsOverride()
		{
			Settings = GetMutableDefault<UCatShopEconomySettings>();
			if (Settings)
			{
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
				Settings->CatalogEntries.Reset();
				// 这张档位表只是本测试用来让售鱼有一个确定价格的刻度，不是飞书裁下来的收购价，不得据此填进项目配置。
				Settings->FishPurchasePricePolicy = ECatDomainPolicy::Enabled;
				Settings->FishPurchasePriceAnchors.Reset();
				FCatShopFishWeightPrice TestAnchor;
				TestAnchor.MinimumWeightKilograms = 0.5;
				TestAnchor.Price = 5;
				Settings->FishPurchasePriceAnchors.Add(TestAnchor);
			}
		}

		/** 析构流程：恢复测试前默认配置，避免后续 WorldSubsystem 初始化读到测试目录。 */
		~FShopSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableShopEconomyRuntime = bSavedRuntime;
				Settings->StartingTeamWalletBalance = SavedStartingBalance;
				Settings->MinimumFishSaleValue = SavedMinimumFishSaleValue;
				Settings->FreeOrdinaryBaitEntryId = SavedFreeOrdinaryBaitEntryId;
				Settings->FreeStarterRodEntryId = SavedFreeStarterRodEntryId;
				Settings->FishPurchasePricePolicy = SavedFishPurchasePricePolicy;
				Settings->FishPurchasePriceAnchors = SavedFishPurchasePriceAnchors;
				Settings->CatalogEntries = SavedCatalogEntries;
			}
		}

		/** 写入一条有限库存装备订单和一条无限免费普通饵订单；测试只观察经济事实，不创建 Equipment 定义。 */
		void ConfigurePurchaseCatalog()
		{
			if (!Settings)
			{
				return;
			}
			FCatShopCatalogEntry RodEntry;
			RodEntry.EntryId = TEXT("StarterRodOrder");
			RodEntry.Kind = ECatShopEntryKind::EquipmentGrant;
			RodEntry.DefinitionId = TEXT("StarterRodA");
			RodEntry.UnitPrice = 3;
			RodEntry.InitialStock = 1;

			FCatShopCatalogEntry FreeBaitEntry;
			FreeBaitEntry.EntryId = TEXT("FreeBasicBait");
			FreeBaitEntry.Kind = ECatShopEntryKind::RunConsumableGrant;
			FreeBaitEntry.DefinitionId = TEXT("BasicBait");
			FreeBaitEntry.UnitPrice = 0;
			FreeBaitEntry.bUnlimitedStock = true;

			Settings->CatalogEntries = {RodEntry, FreeBaitEntry};
			Settings->FreeOrdinaryBaitEntryId = FreeBaitEntry.EntryId;
		}
	};

	/** 购买命令构造流程：填入服务器身份、钱包版本和目录项；测试不提交价格或库存。 */
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

	/**
	 * 售鱼命令构造流程：填入 Items 已提交证据、鱼实例、来源、重量和与测试档位表一致的报价。
	 * 重量取 1.0 千克，落在夹具那条 0.5 千克起价 5 的档位上；报价必须和服务器估出来的一样，否则服务会整笔拒绝。
	 */
	static FCatShopFishSaleCommand MakeFishSaleCommand(const FString& StableNetId, const int64 ExpectedRevision,
		const FGuid FishInstanceId, const FGuid RequestId = FGuid::NewGuid())
	{
		FCatShopFishSaleCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.ItemsCommitId = FGuid::NewGuid();
		Command.SourceKind = ECatShopFishSaleSource::PersonalGuard;
		Command.WeightKilograms = 1.0;
		Command.SaleValue = 5;
		return Command;
	}

	/** 交付确认命令构造流程：引用首次订单 TransactionId 和钱包版本，再填入下游成功回执事实。 */
	static FCatShopDeliveryConfirmationCommand MakeDeliveryCommand(const FString& StableNetId,
		const FCatShopTransactionRecord& Transaction, const FGuid RequestId = FGuid::NewGuid())
	{
		FCatShopDeliveryConfirmationCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = Transaction.WalletRevision;
		Command.Context.StableNetId = StableNetId;
		Command.TransactionId = Transaction.TransactionId;
		Command.DeliveryReceiptId = FGuid::NewGuid();
		Command.DeliveryRevision = 11;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyPurchaseAndFreeBaitTest,
	"Catfishing.Unit.ShopEconomy.Service.PurchaseStockWalletLedgerAndFreeBait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyFishSaleAndCloseTest,
	"Catfishing.Unit.ShopEconomy.Service.FishSaleCreditsWalletReplaysAndCloseRejectsNewCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyRejectedRequestReplayTest,
	"Catfishing.Unit.ShopEconomy.Service.RejectedRequestsReplayBeforeMutableState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopEconomyClosedShopRejectsEveryWriteEntryTest,
	"Catfishing.Unit.ShopEconomy.Service.ClosedShopRejectsEveryWriteEntryAndStillReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：配置一条有限库存装备订单和一条无限免费普通饵；购买必须扣钱包/库存/账本并进入 Pending，交付确认只推进账本
// 状态，稳定重放不二次扣，payload 漂移必须拒绝。
bool FCatShopEconomyPurchaseAndFreeBaitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopEconomyServiceTest::FShopSettingsOverride SettingsOverride;
	SettingsOverride.ConfigurePurchaseCatalog();
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 ShopEconomy 购买测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("ShopEconomy 购买测试服务可创建"), Service);
		if (Service)
		{
			const FCatShopWalletSnapshot InitialWallet = Service->GetWalletSnapshot();
			TestEqual(TEXT("初始钱包 Revision 为 1"), InitialWallet.Revision, static_cast<int64>(1));
			TestEqual(TEXT("初始钱包余额来自配置"), InitialWallet.Balance, 10);

			const FGuid PurchaseRequestId = FGuid::NewGuid();
			const FCatShopTransactionResult Purchase = Service->PurchaseCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), InitialWallet.Revision,
					TEXT("StarterRodOrder"), PurchaseRequestId));
			TestTrue(TEXT("首次购买提交成功"), Purchase.Command.bCommitted);
			TestEqual(TEXT("首次购买无错误"), Purchase.Command.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("购买扣除钱包"), Purchase.Wallet.Balance, 7);
			TestEqual(TEXT("购买推进钱包 Revision"), Purchase.Wallet.Revision, static_cast<int64>(2));
			TestEqual(TEXT("有限库存购买后归零"), Purchase.Stock.RemainingStock, 0);
			TestEqual(TEXT("有限库存扣减推进库存 Revision"), Purchase.Stock.Revision, static_cast<int64>(2));
			TestEqual(TEXT("账本记录购买类别"), Purchase.Transaction.Kind, ECatShopTransactionKind::Purchase);
			TestEqual(TEXT("账本记录交付类别"), Purchase.Transaction.EntryKind, ECatShopEntryKind::EquipmentGrant);
			TestEqual(TEXT("购买订单先进入待交付"), Purchase.Transaction.DeliveryState, ECatShopDeliveryState::Pending);
			TestEqual(TEXT("账本记录钱包负增量"), Purchase.Transaction.WalletDelta, -3);

			const FGuid DeliveryRequestId = FGuid::NewGuid();
			const FCatShopDeliveryConfirmationCommand DeliveryCommand = CatShopEconomyServiceTest::MakeDeliveryCommand(
				TEXT("PlayerA"), Purchase.Transaction, DeliveryRequestId);
			const FCatShopTransactionResult Delivered = Service->ConfirmTransactionDelivery(DeliveryCommand);
			TestTrue(TEXT("交付确认提交成功"), Delivered.Command.bCommitted);
			TestEqual(TEXT("交付确认无错误"), Delivered.Command.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("交付确认不改变钱包余额"), Delivered.Wallet.Balance, 7);
			TestEqual(TEXT("交付确认推进订单状态"), Delivered.Transaction.DeliveryState, ECatShopDeliveryState::Delivered);
			TestEqual(TEXT("交付确认保留同一交易 ID"), Delivered.Transaction.TransactionId, Purchase.Transaction.TransactionId);

			const FCatShopTransactionResult DeliveryReplay = Service->ConfirmTransactionDelivery(DeliveryCommand);
			TestFalse(TEXT("重复交付确认不再次提交"), DeliveryReplay.Command.bCommitted);
			TestEqual(TEXT("重复交付确认返回 AlreadyResolved"), DeliveryReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重复交付确认仍指向同一交易"), DeliveryReplay.Transaction.TransactionId, Purchase.Transaction.TransactionId);
			FCatShopDeliveryConfirmationCommand DeliveryDrift = DeliveryCommand;
			DeliveryDrift.DeliveryReceiptId = FGuid::NewGuid();
			const FCatShopTransactionResult DeliveryPayloadDrift = Service->ConfirmTransactionDelivery(DeliveryDrift);
			TestFalse(TEXT("同 RequestId 更换交付回执不提交"), DeliveryPayloadDrift.Command.bCommitted);
			TestEqual(TEXT("同 RequestId 更换交付回执返回 InvalidPayload"), DeliveryPayloadDrift.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			const FCatShopTransactionResult Replay = Service->PurchaseCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), InitialWallet.Revision,
					TEXT("StarterRodOrder"), PurchaseRequestId));
			TestFalse(TEXT("重复购买不再次提交"), Replay.Command.bCommitted);
			TestEqual(TEXT("重复购买返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重复购买保留首次交易 ID"), Replay.Transaction.TransactionId, Purchase.Transaction.TransactionId);
			TestEqual(TEXT("交付后重复购买返回当前交付状态"), Replay.Transaction.DeliveryState, ECatShopDeliveryState::Delivered);
			TestEqual(TEXT("重复购买不二次扣钱包"), Service->GetWalletSnapshot().Balance, 7);
			const FCatShopTransactionResult PurchasePayloadDrift = Service->PurchaseCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), InitialWallet.Revision,
					TEXT("FreeBasicBait"), PurchaseRequestId));
			TestFalse(TEXT("同 RequestId 更换购买条目不提交"), PurchasePayloadDrift.Command.bCommitted);
			TestEqual(TEXT("同 RequestId 更换购买条目返回 InvalidPayload"), PurchasePayloadDrift.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			const FCatShopTransactionResult SoldOut = Service->PurchaseCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					TEXT("StarterRodOrder")));
			TestFalse(TEXT("售罄后新购买不提交"), SoldOut.Command.bCommitted);
			TestEqual(TEXT("售罄后返回 CapacityExceeded"), SoldOut.Command.Error, ECatDomainCommandError::CapacityExceeded);

			const FCatShopTransactionResult FreeBait = Service->ClaimFreeCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					TEXT("FreeBasicBait")));
			TestTrue(TEXT("免费普通饵领取提交成功"), FreeBait.Command.bCommitted);
			TestEqual(TEXT("免费普通饵不改变钱包余额"), FreeBait.Wallet.Balance, 7);
			TestEqual(TEXT("免费普通饵不推进钱包 Revision"), FreeBait.Wallet.Revision, static_cast<int64>(2));
			TestTrue(TEXT("免费普通饵使用无限库存"), FreeBait.Stock.bUnlimitedStock);
			TestEqual(TEXT("免费普通饵记录免费自取交易类别"), FreeBait.Transaction.Kind, ECatShopTransactionKind::FreeClaim);
			TestEqual(TEXT("免费普通饵订单也等待 Equipment 交付回执"), FreeBait.Transaction.DeliveryState, ECatShopDeliveryState::Pending);

			Service->CloseCommands();
			const FCatShopTransactionResult Closed = Service->ClaimFreeCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					TEXT("FreeBasicBait")));
			TestFalse(TEXT("关闭后新免费饵命令不提交"), Closed.Command.bCommitted);
			TestEqual(TEXT("关闭后新免费饵命令返回 CommandsClosed"), Closed.Command.Error, ECatDomainCommandError::CommandsClosed);

			const FCatShopTransactionResult ClosedDelivery = Service->ConfirmTransactionDelivery(
				CatShopEconomyServiceTest::MakeDeliveryCommand(TEXT("PlayerA"), FreeBait.Transaction));
			TestFalse(TEXT("关闭后待交付订单不在 Shop 内继续推进"), ClosedDelivery.Command.bCommitted);
			TestEqual(TEXT("关闭后待交付订单确认返回 CommandsClosed"), ClosedDelivery.Command.Error, ECatDomainCommandError::CommandsClosed);
			const TArray<FCatShopTransactionRecord> LedgerAfterClose = Service->GetTransactionLedgerSnapshot();
			TestEqual(TEXT("账本只包含两条成功交易"), LedgerAfterClose.Num(), 2);
			if (LedgerAfterClose.Num() == 2)
			{
				TestEqual(TEXT("关闭后未交付普通饵仍保持 Pending"), LedgerAfterClose[1].DeliveryState, ECatShopDeliveryState::Pending);
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先让购买、免费饵和售鱼用未来钱包 Revision 首次失败，再推进真实钱包到该 Revision；同 RequestId 重放必须命
// 中首次拒绝，不能因为当前状态变得可提交而复活。
bool FCatShopEconomyRejectedRequestReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopEconomyServiceTest::FShopSettingsOverride SettingsOverride;
	SettingsOverride.ConfigurePurchaseCatalog();
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 ShopEconomy 拒绝重放测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("ShopEconomy 拒绝重放测试服务可创建"), Service);
		if (Service)
		{
			const FCatShopWalletSnapshot InitialWallet = Service->GetWalletSnapshot();
			const FGuid RejectedPurchaseRequestId = FGuid::NewGuid();
			const FCatShopPurchaseCommand RejectedPurchaseCommand = CatShopEconomyServiceTest::MakePurchaseCommand(
				TEXT("PlayerA"), InitialWallet.Revision + 1, TEXT("StarterRodOrder"), RejectedPurchaseRequestId);
			const FCatShopTransactionResult RejectedPurchase = Service->PurchaseCatalogEntry(RejectedPurchaseCommand);
			TestFalse(TEXT("未来钱包 Revision 的购买首次不提交"), RejectedPurchase.Command.bCommitted);
			TestEqual(TEXT("未来钱包 Revision 的购买返回 RevisionConflict"), RejectedPurchase.Command.Error,
				ECatDomainCommandError::RevisionConflict);

			const FCatShopTransactionResult SaleToReachPurchaseRevision = Service->ApplyFishSale(
				CatShopEconomyServiceTest::MakeFishSaleCommand(TEXT("PlayerA"), InitialWallet.Revision, FGuid::NewGuid()));
			TestTrue(TEXT("售鱼推进钱包到购买原本等待的 Revision"), SaleToReachPurchaseRevision.Command.bCommitted);
			TestEqual(TEXT("钱包已到达被拒购买的 ExpectedRevision"), Service->GetWalletSnapshot().Revision,
				InitialWallet.Revision + 1);
			const FCatShopTransactionResult PurchaseReplay = Service->PurchaseCatalogEntry(RejectedPurchaseCommand);
			TestFalse(TEXT("被拒购买重放不因钱包 Revision 变匹配而提交"), PurchaseReplay.Command.bCommitted);
			TestEqual(TEXT("被拒购买重放返回 AlreadyResolved"), PurchaseReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			FCatShopStockSnapshot RodStock;
			TestTrue(TEXT("购买未复活后仍能读取鱼竿库存"), Service->TryGetStockSnapshot(TEXT("StarterRodOrder"), RodStock));
			TestEqual(TEXT("被拒购买没有扣减库存"), RodStock.RemainingStock, 1);
			TestEqual(TEXT("账本只包含用于推进版本的售鱼"), Service->GetTransactionLedgerSnapshot().Num(), 1);

			const FGuid RejectedFreeRequestId = FGuid::NewGuid();
			const FCatShopPurchaseCommand RejectedFreeCommand = CatShopEconomyServiceTest::MakePurchaseCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision + 1, TEXT("FreeBasicBait"), RejectedFreeRequestId);
			const FCatShopTransactionResult RejectedFree = Service->ClaimFreeCatalogEntry(RejectedFreeCommand);
			TestFalse(TEXT("未来钱包 Revision 的免费饵首次不提交"), RejectedFree.Command.bCommitted);
			TestEqual(TEXT("未来钱包 Revision 的免费饵返回 RevisionConflict"), RejectedFree.Command.Error,
				ECatDomainCommandError::RevisionConflict);

			const FCatShopTransactionResult SaleToReachFreeRevision = Service->ApplyFishSale(
				CatShopEconomyServiceTest::MakeFishSaleCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					FGuid::NewGuid()));
			TestTrue(TEXT("售鱼推进钱包到免费饵原本等待的 Revision"), SaleToReachFreeRevision.Command.bCommitted);
			const FCatShopTransactionResult FreeReplay = Service->ClaimFreeCatalogEntry(RejectedFreeCommand);
			TestFalse(TEXT("被拒免费饵重放不因钱包 Revision 变匹配而提交"), FreeReplay.Command.bCommitted);
			TestEqual(TEXT("被拒免费饵重放返回 AlreadyResolved"), FreeReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("免费饵未复活成第三条交易"), Service->GetTransactionLedgerSnapshot().Num(), 2);

			const FGuid RejectedSaleRequestId = FGuid::NewGuid();
			const FCatShopFishSaleCommand RejectedSaleCommand = CatShopEconomyServiceTest::MakeFishSaleCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision + 1, FGuid::NewGuid(), RejectedSaleRequestId);
			const FCatShopTransactionResult RejectedSale = Service->ApplyFishSale(RejectedSaleCommand);
			TestFalse(TEXT("未来钱包 Revision 的售鱼首次不提交"), RejectedSale.Command.bCommitted);
			TestEqual(TEXT("未来钱包 Revision 的售鱼返回 RevisionConflict"), RejectedSale.Command.Error,
				ECatDomainCommandError::RevisionConflict);

			const FCatShopTransactionResult SaleToReachRejectedSaleRevision = Service->ApplyFishSale(
				CatShopEconomyServiceTest::MakeFishSaleCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					FGuid::NewGuid()));
			TestTrue(TEXT("售鱼推进钱包到被拒售鱼原本等待的 Revision"), SaleToReachRejectedSaleRevision.Command.bCommitted);
			const FCatShopTransactionResult SaleReplay = Service->ApplyFishSale(RejectedSaleCommand);
			TestFalse(TEXT("被拒售鱼重放不因钱包 Revision 变匹配而提交"), SaleReplay.Command.bCommitted);
			TestEqual(TEXT("被拒售鱼重放返回 AlreadyResolved"), SaleReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("拒绝重放没有新增售鱼账本"), Service->GetTransactionLedgerSnapshot().Num(), 3);
			TestEqual(TEXT("拒绝重放没有额外增加钱包"), Service->GetWalletSnapshot().Balance, 25);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：售鱼入账必须带 Items 提交证据并按钱包 Revision 并发；重复命令不二次加钱，同 RequestId 换提交证据会被拒绝，关闭后拒绝新售鱼。
bool FCatShopEconomyFishSaleAndCloseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopEconomyServiceTest::FShopSettingsOverride SettingsOverride;
	SettingsOverride.Settings->StartingTeamWalletBalance = 0;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 ShopEconomy 售鱼测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("ShopEconomy 售鱼测试服务可创建"), Service);
		if (Service)
		{
			const FCatShopWalletSnapshot InitialWallet = Service->GetWalletSnapshot();
			const FGuid SaleRequestId = FGuid::NewGuid();
			const FGuid FishInstanceId = FGuid::NewGuid();
			const FCatShopFishSaleCommand SaleCommand = CatShopEconomyServiceTest::MakeFishSaleCommand(
				TEXT("PlayerA"), InitialWallet.Revision, FishInstanceId, SaleRequestId);
			const FCatShopTransactionResult Sale = Service->ApplyFishSale(SaleCommand);
			TestTrue(TEXT("首次售鱼入账提交成功"), Sale.Command.bCommitted);
			TestEqual(TEXT("售鱼入账无错误"), Sale.Command.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("售鱼增加钱包余额"), Sale.Wallet.Balance, 5);
			TestEqual(TEXT("售鱼推进钱包 Revision"), Sale.Wallet.Revision, static_cast<int64>(2));
			TestEqual(TEXT("售鱼账本记录鱼实例"), Sale.Transaction.FishInstanceId, FishInstanceId);
			TestEqual(TEXT("售鱼不需要下游交付"), Sale.Transaction.DeliveryState, ECatShopDeliveryState::None);
			TestEqual(TEXT("售鱼账本记录正增量"), Sale.Transaction.WalletDelta, 5);

			const FCatShopTransactionResult Replay = Service->ApplyFishSale(SaleCommand);
			TestFalse(TEXT("重复售鱼不再次提交"), Replay.Command.bCommitted);
			TestEqual(TEXT("重复售鱼返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重复售鱼保留首次交易 ID"), Replay.Transaction.TransactionId, Sale.Transaction.TransactionId);
			TestEqual(TEXT("重复售鱼不二次加钱"), Service->GetWalletSnapshot().Balance, 5);
			FCatShopFishSaleCommand SalePayloadDrift = SaleCommand;
			SalePayloadDrift.ItemsCommitId = FGuid::NewGuid();
			const FCatShopTransactionResult SaleDrift = Service->ApplyFishSale(SalePayloadDrift);
			TestFalse(TEXT("同 RequestId 更换 Items 提交证据不提交"), SaleDrift.Command.bCommitted);
			TestEqual(TEXT("同 RequestId 更换 Items 提交证据返回 InvalidPayload"), SaleDrift.Command.Error,
				ECatDomainCommandError::InvalidPayload);

			const FCatShopTransactionResult Stale = Service->ApplyFishSale(
				CatShopEconomyServiceTest::MakeFishSaleCommand(TEXT("PlayerA"), InitialWallet.Revision,
					FGuid::NewGuid()));
			TestFalse(TEXT("陈旧钱包 Revision 的售鱼不提交"), Stale.Command.bCommitted);
			TestEqual(TEXT("陈旧钱包 Revision 返回 RevisionConflict"), Stale.Command.Error, ECatDomainCommandError::RevisionConflict);

			Service->CloseCommands();
			const FCatShopTransactionResult Closed = Service->ApplyFishSale(
				CatShopEconomyServiceTest::MakeFishSaleCommand(TEXT("PlayerA"), Service->GetWalletSnapshot().Revision,
					FGuid::NewGuid()));
			TestFalse(TEXT("关闭后新售鱼不提交"), Closed.Command.bCommitted);
			TestEqual(TEXT("关闭后新售鱼返回 CommandsClosed"), Closed.Command.Error, ECatDomainCommandError::CommandsClosed);
			TestEqual(TEXT("售鱼账本只包含首次成功交易"), Service->GetTransactionLedgerSnapshot().Num(), 1);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先在开摊状态下走完购买、免费饵、售鱼入账和交付确认四条写口各一次，拿到可复用的命令与账本事实；随后调用一
// 次 CloseCommands 表达商人猫收摊，再用全新 RequestId 逐一重打这四条写口，确认全部落到 CommandsClosed 而不是
// NotFound、CapacityExceeded 或 PolicyUndecided 这类会掩盖收摊语义的错误。最后用收摊前的原始命令重放，确认既有
// RequestId 仍能拿回首次终态，并且钱包、库存、账本这些查询在收摊后照常可读——收摊冻结的是买卖，不是本局经济事实本身。
bool FCatShopEconomyClosedShopRejectsEveryWriteEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopEconomyServiceTest::FShopSettingsOverride SettingsOverride;
	SettingsOverride.ConfigurePurchaseCatalog();
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 ShopEconomy 收摊测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UCatShopEconomyService* Service = WorldWrapper.GetTestWorld()
			? WorldWrapper.GetTestWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
		TestNotNull(TEXT("ShopEconomy 收摊测试服务可创建"), Service);
		if (Service)
		{
			const FCatShopWalletSnapshot InitialWallet = Service->GetWalletSnapshot();
			const FCatShopPurchaseCommand PurchaseCommand = CatShopEconomyServiceTest::MakePurchaseCommand(
				TEXT("PlayerA"), InitialWallet.Revision, TEXT("StarterRodOrder"));
			const FCatShopTransactionResult Purchase = Service->PurchaseCatalogEntry(PurchaseCommand);
			TestTrue(TEXT("收摊前购买成功"), Purchase.Command.bCommitted);

			const FCatShopPurchaseCommand FreeBaitCommand = CatShopEconomyServiceTest::MakePurchaseCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, TEXT("FreeBasicBait"));
			const FCatShopTransactionResult FreeBait = Service->ClaimFreeCatalogEntry(FreeBaitCommand);
			TestTrue(TEXT("收摊前免费普通饵领取成功"), FreeBait.Command.bCommitted);

			const FCatShopFishSaleCommand SaleCommand = CatShopEconomyServiceTest::MakeFishSaleCommand(
				TEXT("PlayerA"), Service->GetWalletSnapshot().Revision, FGuid::NewGuid());
			const FCatShopTransactionResult Sale = Service->ApplyFishSale(SaleCommand);
			TestTrue(TEXT("收摊前售鱼入账成功"), Sale.Command.bCommitted);

			const FCatShopDeliveryConfirmationCommand DeliveryCommand = CatShopEconomyServiceTest::MakeDeliveryCommand(
				TEXT("PlayerA"), Purchase.Transaction);
			const FCatShopTransactionResult Delivered = Service->ConfirmTransactionDelivery(DeliveryCommand);
			TestTrue(TEXT("收摊前交付确认成功"), Delivered.Command.bCommitted);

			const FCatShopWalletSnapshot WalletBeforeClose = Service->GetWalletSnapshot();
			const int32 LedgerCountBeforeClose = Service->GetTransactionLedgerSnapshot().Num();
			TestEqual(TEXT("收摊前账本记录三笔交易"), LedgerCountBeforeClose, 3);

			Service->CloseCommands();

			const FCatShopTransactionResult ClosedPurchase = Service->PurchaseCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), WalletBeforeClose.Revision,
					TEXT("StarterRodOrder")));
			TestFalse(TEXT("收摊后购买不提交"), ClosedPurchase.Command.bCommitted);
			TestEqual(TEXT("收摊后购买返回 CommandsClosed"), ClosedPurchase.Command.Error,
				ECatDomainCommandError::CommandsClosed);

			const FCatShopTransactionResult ClosedFreeBait = Service->ClaimFreeCatalogEntry(
				CatShopEconomyServiceTest::MakePurchaseCommand(TEXT("PlayerA"), WalletBeforeClose.Revision,
					TEXT("FreeBasicBait")));
			TestFalse(TEXT("收摊后免费普通饵不提交"), ClosedFreeBait.Command.bCommitted);
			TestEqual(TEXT("收摊后免费普通饵返回 CommandsClosed"), ClosedFreeBait.Command.Error,
				ECatDomainCommandError::CommandsClosed);

			const FCatShopFishSaleCommand ClosedSaleCommand = CatShopEconomyServiceTest::MakeFishSaleCommand(
				TEXT("PlayerA"), WalletBeforeClose.Revision, FGuid::NewGuid());
			ECatDomainCommandError ValidateError = ECatDomainCommandError::None;
			int64 ValidateWalletRevision = 0;
			TestFalse(TEXT("收摊后售鱼预检不放行"), Service->ValidateFishSale(ClosedSaleCommand, ValidateError,
				ValidateWalletRevision));
			TestEqual(TEXT("收摊后售鱼预检返回 CommandsClosed"), ValidateError, ECatDomainCommandError::CommandsClosed);
			const FCatShopTransactionResult ClosedSale = Service->ApplyFishSale(ClosedSaleCommand);
			TestFalse(TEXT("收摊后售鱼入账不提交"), ClosedSale.Command.bCommitted);
			TestEqual(TEXT("收摊后售鱼入账返回 CommandsClosed"), ClosedSale.Command.Error,
				ECatDomainCommandError::CommandsClosed);

			const FCatShopTransactionResult ClosedDelivery = Service->ConfirmTransactionDelivery(
				CatShopEconomyServiceTest::MakeDeliveryCommand(TEXT("PlayerA"), FreeBait.Transaction));
			TestFalse(TEXT("收摊后交付确认不提交"), ClosedDelivery.Command.bCommitted);
			TestEqual(TEXT("收摊后交付确认返回 CommandsClosed"), ClosedDelivery.Command.Error,
				ECatDomainCommandError::CommandsClosed);

			const FCatShopTransactionResult PurchaseReplay = Service->PurchaseCatalogEntry(PurchaseCommand);
			TestFalse(TEXT("收摊后购买重放不提交"), PurchaseReplay.Command.bCommitted);
			TestEqual(TEXT("收摊后购买重放返回首次终态"), PurchaseReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("收摊后购买重放仍指向首次交易"), PurchaseReplay.Transaction.TransactionId,
				Purchase.Transaction.TransactionId);
			TestEqual(TEXT("收摊后购买重放返回当前交付状态"), PurchaseReplay.Transaction.DeliveryState,
				ECatShopDeliveryState::Delivered);

			const FCatShopTransactionResult FreeBaitReplay = Service->ClaimFreeCatalogEntry(FreeBaitCommand);
			TestFalse(TEXT("收摊后免费普通饵重放不提交"), FreeBaitReplay.Command.bCommitted);
			TestEqual(TEXT("收摊后免费普通饵重放返回首次终态"), FreeBaitReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("收摊后免费普通饵重放仍指向首次交易"), FreeBaitReplay.Transaction.TransactionId,
				FreeBait.Transaction.TransactionId);

			const FCatShopTransactionResult SaleReplay = Service->ApplyFishSale(SaleCommand);
			TestFalse(TEXT("收摊后售鱼重放不提交"), SaleReplay.Command.bCommitted);
			TestEqual(TEXT("收摊后售鱼重放返回首次终态"), SaleReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("收摊后售鱼重放仍指向首次交易"), SaleReplay.Transaction.TransactionId,
				Sale.Transaction.TransactionId);

			const FCatShopTransactionResult DeliveryReplay = Service->ConfirmTransactionDelivery(DeliveryCommand);
			TestFalse(TEXT("收摊后交付确认重放不提交"), DeliveryReplay.Command.bCommitted);
			TestEqual(TEXT("收摊后交付确认重放返回首次终态"), DeliveryReplay.Command.Error,
				ECatDomainCommandError::AlreadyResolved);

			TestEqual(TEXT("收摊后钱包余额不变"), Service->GetWalletSnapshot().Balance, WalletBeforeClose.Balance);
			TestEqual(TEXT("收摊后钱包 Revision 不变"), Service->GetWalletSnapshot().Revision, WalletBeforeClose.Revision);
			TestEqual(TEXT("收摊后账本不再增加"), Service->GetTransactionLedgerSnapshot().Num(), LedgerCountBeforeClose);
			FCatShopStockSnapshot RodStock;
			TestTrue(TEXT("收摊后库存查询照常可读"), Service->TryGetStockSnapshot(TEXT("StarterRodOrder"), RodStock));
			TestEqual(TEXT("收摊后库存维持收摊前事实"), RodStock.RemainingStock, 0);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS

