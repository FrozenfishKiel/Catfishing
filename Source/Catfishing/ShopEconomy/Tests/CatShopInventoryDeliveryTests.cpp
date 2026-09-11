#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopInventoryDeliveryReplayTest,
	"Catfishing.Unit.ShopEconomy.CartPurchaseDeliversOnPaymentAndReplaysWithoutDuplicatePaymentOrItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 购物车交付边界回归：
// 1. 在独立 authority World 加载正式摊位蓝图和公共仓库，使用实际目录与公款配置。
// 2. 从当前货架选一项可付款商品，先预检整批接收，再通过公开经济和库存接口提交。
// 3. 账本一写下就是成交态（2026-09-09 裁「购买即入库」，没有待交付中间态），并检查金额和实物数量；
//    不依赖库存版本号作为提交证据。
// 4. 分别重放购买和发货，验证同一请求不会再次扣钱、发货或生成账本。
// 此用例覆盖服务边界；Controller 距离校验、UI 操作和网络传输由各自运行验证覆盖。
bool FCatShopInventoryDeliveryReplayTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建商店回归 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("启动 World 生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	UClass* KioskClass = LoadClass<ACatShopKioskActor>(nullptr,
		TEXT("/Game/UI/Shop/BP_CatShopKiosk.BP_CatShopKiosk_C"));
	if (!TestNotNull(TEXT("加载正式摊位蓝图"), KioskClass)) return false;
	ACatShopKioskActor* Kiosk = World->SpawnActor<ACatShopKioskActor>(KioskClass);
	ACatCampInventoryActor* Camp = World->SpawnActor<ACatCampInventoryActor>();
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	if (!TestTrue(TEXT("真实商店与公共库存可用"), Kiosk && Camp && Shop)) return false;
	UCatShopInventoryComponent* Shelf = Kiosk->GetShopInventory();
	UCatInventoryComponent* Inventory = Camp->GetInventoryComponent();
	if (!TestTrue(TEXT("正式货架目录已装配"), Shelf && Shelf->IsRuntimeCatalogReady() && Inventory)) return false;

	const FCatShopWalletSnapshot WalletBefore = Shop->GetWalletSnapshot();
	TArray<FCatShopCatalogEntry> Entries;
	Shelf->CollectDisplayCatalogEntries(Entries);
	FCatShopCatalogEntry Selected;
	UCatInventoryItemDefinition* Definition = nullptr;
	for (const FCatShopCatalogEntry& Entry : Entries)
	{
		FCatShopStockSnapshot Stock;
		if (Entry.UnitPrice > 0 && Entry.UnitPrice <= WalletBefore.Balance
			&& Shelf->TryGetStockSnapshot(Entry.EntryId, Stock)
			&& (Stock.bUnlimitedStock || Stock.RemainingStock > 0))
		{
			Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Entry.DefinitionId);
			if (Definition)
			{
				Selected = Entry;
				break;
			}
		}
	}
	if (!TestNotNull(TEXT("当前正式货架有可付款的物品"), Definition)) return false;
	const int32 QuantityBefore = Inventory->CountVisibleInventoryQuantityByDefinitionId(Selected.DefinitionId);
	FCatShopCartCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.StableNetId = TEXT("InventoryDeliveryRegression");
	Command.Context.ExpectedRevision = WalletBefore.Revision;
	Command.ShopInventoryId = Shelf->GetShopInventoryId();
	Command.Lines.AddDefaulted_GetRef().EntryId = Selected.EntryId;
	FCatInventoryReceiveBatch Batch;
	FCatInventoryDefinitionEntry& DeliveryEntry = Batch.DefinitionEntries.AddDefaulted_GetRef();
	DeliveryEntry.ItemDefinition = Definition;
	DeliveryEntry.Count = Selected.PurchaseQuantity;
	if (!TestEqual(TEXT("扣款前整批库存预检通过"),
		Inventory->ValidateInventoryDefinitionBatchGrantFromAuthority(Command.Context.RequestId,
			Command.Context.StableNetId, Batch), ECatDomainCommandError::None)) return false;
	const FDateTime BeforePurchaseUtc = FDateTime::UtcNow();
	const FCatShopCartTransactionResult Purchase = Shop->PurchaseCatalogCart(Command, Shelf);
	if (!TestTrue(TEXT("真实购买产生一笔成交账本"), Purchase.Command.bCommitted
		&& Purchase.Transactions.Num() == 1 && Purchase.Transactions[0].bPurchase)) return false;
	TestTrue(TEXT("账本留下可回看的提交时刻"), Purchase.Transactions[0].CommittedAtUtc >= BeforePurchaseUtc);
	const FCatDomainCommandResult Grant = Inventory->GrantInventoryDefinitionBatchFromAuthority(
		Command.Context.RequestId, Command.Context.StableNetId, Batch);
	if (!TestTrue(TEXT("实际物品已进入公共库存"), Grant.bCommitted)) return false;
	TestEqual(TEXT("购买只扣一次商品价格"), Shop->GetWalletSnapshot().Balance,
		WalletBefore.Balance - Selected.UnitPrice);
	TestEqual(TEXT("公共库存收到目录规定数量"),
		Inventory->CountVisibleInventoryQuantityByDefinitionId(Selected.DefinitionId),
		QuantityBefore + Selected.PurchaseQuantity);
	TestEqual(TEXT("购买重放取回首次终态"), Shop->PurchaseCatalogCart(Command, Shelf).Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("发货重放保留成功提交事实"), CatIsAcceptedDomainCommandResult(
		Inventory->GrantInventoryDefinitionBatchFromAuthority(Command.Context.RequestId,
			Command.Context.StableNetId, Batch)));
	TestEqual(TEXT("重放不再次扣款"), Shop->GetWalletSnapshot().Balance,
		WalletBefore.Balance - Selected.UnitPrice);
	TestEqual(TEXT("重放不再次发货"), Inventory->CountVisibleInventoryQuantityByDefinitionId(Selected.DefinitionId),
		QuantityBefore + Selected.PurchaseQuantity);
	TestEqual(TEXT("重放不增加交易记录"), Shop->GetTransactionLedgerSnapshot().Num(), 1);
	return !HasAnyErrors();
}

#endif
