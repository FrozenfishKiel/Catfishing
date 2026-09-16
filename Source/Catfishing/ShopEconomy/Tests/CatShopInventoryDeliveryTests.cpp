#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Camp/CatCampInventoryActor.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopInventoryDeliveryReplayTest,
	"Catfishing.Unit.ShopEconomy.PurchaseImmediatelyStocksAndReplaysWithoutDuplicatePaymentOrItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 购物车交付边界回归：
// 1. 在独立 authority World 加载正式摊位蓝图和公共仓库，使用实际目录与公款配置。
// 2. 验证交付回调拒绝拒绝且公款与账本不变，再验证定义批次提交拒绝能恢复库存。
// 3. 使用落后的钱包快照购买，核对仍按服务器余额成交，通知时实物已入库、金额正确且已有唯一账本。
// 4. 重放购买时核对统一回执仍被接受，再篡改同号载荷验证拒绝；两者均不重复扣钱、发货或广播。
// 5. 在已有物品上复核回滚后的槽位、实例身份和堆叠数量；带回调的实例批次必须在执行回调前拒绝。
// 6. 实际填满仓库格子与堆叠并确认无法再接收，再用新货架通过报价预检；核对购买因容量拒绝且货架、公款与账本不变。
// 此用例只覆盖服务边界；Controller 身份校验、UI 操作和网络传输不在本用例内。
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
	UCatShopTradeController* Trading = World->GetSubsystem<UCatShopTradeController>();
	if (!TestTrue(TEXT("真实商店与公共库存可用"), Kiosk && Camp && Shop && Trading)) return false;
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
		if (Entry.UnitPrice > 0 && Entry.UnitPrice <= WalletBefore.Balance / 2
			&& Shelf->TryGetStockSnapshot(Entry.EntryId, Stock)
			&& Stock.bUnlimitedStock)
		{
			Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Entry.ItemId);
			if (Definition)
			{
				Selected = Entry;
				break;
			}
		}
	}
	if (!TestNotNull(TEXT("当前正式货架有可付款的物品"), Definition)) return false;
	const int32 QuantityBefore = Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId);
	FCatShopCartCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.StableNetId = TEXT("InventoryDeliveryRegression");
	Command.ShopInventoryId = Shelf->GetShopInventoryId();
	Command.Lines.AddDefaulted_GetRef().EntryId = Selected.EntryId;
	// 两人基于同一份旧公款快照先准备好请求，随后服务器顺序处理；第二笔不能因为第一笔改变版本而拒绝。
	FCatShopCartCommand SecondCommand = Command;
	SecondCommand.Context.RequestId = FGuid::NewGuid();
	SecondCommand.Context.StableNetId = TEXT("SecondBuyer");
	FCatInventoryReceiveBatch Batch;
	FCatInventoryDefinitionEntry& DeliveryEntry = Batch.DefinitionEntries.AddDefaulted_GetRef();
	DeliveryEntry.ItemDefinition = Definition;
	DeliveryEntry.Count = Selected.PurchaseQuantity;
	const FCatShopCartTransactionResult MissingInventory = Shop->PurchaseCatalogCart(Command, Shelf, [](TFunctionRef<bool()> Pay) { return false; });
	TestFalse(TEXT("交付回调拒绝不成交"), MissingInventory.Command.bCommitted);
	TestEqual(TEXT("交付回调拒绝不扣钱"), Shop->GetWalletSnapshot().Balance, WalletBefore.Balance);
	TestEqual(TEXT("交付回调拒绝不写账本"), Shop->GetTransactionLedgerSnapshot().Num(), 0);
	TestFalse(TEXT("拒绝请求重放不转为成交"), Trading->RunCartOrder(Command, Shelf, Camp).CartTransaction.Command.bCommitted);
	Command.Context.RequestId = FGuid::NewGuid();

	bool bCommitCalled = false;
	TestFalse(TEXT("同步提交失败拒绝整个入库批次"), Inventory->TryAddInventoryBatch(Batch, [&]()
	{
		bCommitCalled = true;
		return false;
	}, false));
	TestTrue(TEXT("容量成立后确实执行了提交回调"), bCommitCalled);
	TestEqual(TEXT("提交失败恢复原有物品数量"),
		Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId), QuantityBefore);

	int32 BroadcastCount = 0;
	// 监听首次成交与随后重放，回调读取通知当刻的钱货和账本；失败提前返回或重放断言结束后均解除绑定。
	const FDelegateHandle Handle = Shop->OnPublicTransactionCommitted.AddLambda(
		[&](const FCatShopPublicTransaction& Transaction)
		{
			++BroadcastCount;
			TestEqual(TEXT("成交通知时实物已入库"),
				Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId),
				QuantityBefore + Selected.PurchaseQuantity);
			TestEqual(TEXT("成交通知时公款已扣除"), Shop->GetWalletSnapshot().Balance,
				WalletBefore.Balance - Selected.UnitPrice);
			TestEqual(TEXT("成交通知时账本已完成"), Shop->GetTransactionLedgerSnapshot().Num(), 1);
		});
	const FCatShopCartTransactionResult Purchase = Trading->RunCartOrder(Command, Shelf, Camp).CartTransaction;
	if (!TestTrue(TEXT("一次购买完成实物与账本"), Purchase.Command.bCommitted && Purchase.Transactions.Num() == 1))
	{
		Shop->OnPublicTransactionCommitted.Remove(Handle);
		return false;
	}
	TestEqual(TEXT("每笔成交只广播一次"), BroadcastCount, 1);
	TestEqual(TEXT("购买只扣一次商品价格"), Shop->GetWalletSnapshot().Balance,
		WalletBefore.Balance - Selected.UnitPrice);
	TestEqual(TEXT("公共库存收到目录规定数量"),
		Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId),
		QuantityBefore + Selected.PurchaseQuantity);
	// 同号重放不进入交付回调，不能因收货方已变化再次扣款。
	const FCatShopCartTransactionResult Replay = Shop->PurchaseCatalogCart(Command, Shelf, [](TFunctionRef<bool()> Pay) { return false; });
	TestEqual(TEXT("购买重放不依赖当前仓库"), Replay.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("成功重放保留统一回执的已接受事实"), CatIsAcceptedDomainCommandResult(Replay.Command));
	FCatShopCartCommand ChangedCommand = Command;
	ChangedCommand.Lines[0].CartCount += 1;
	TestEqual(TEXT("同一请求不能更换数量"), Trading->RunCartOrder(ChangedCommand, Shelf, Camp).CartTransaction.Command.Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("重放不再次扣款"), Shop->GetWalletSnapshot().Balance,
		WalletBefore.Balance - Selected.UnitPrice);
	TestEqual(TEXT("重放不再次发货"), Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId),
		QuantityBefore + Selected.PurchaseQuantity);
	TestEqual(TEXT("重放不增加交易记录"), Shop->GetTransactionLedgerSnapshot().Num(), 1);
	TestEqual(TEXT("重放不再次广播"), BroadcastCount, 1);
	Shop->OnPublicTransactionCommitted.Remove(Handle);
	const TArray<FCatInventoryEntry> ExistingItems = Inventory->GetInventoryEntries();
	TestFalse(TEXT("已有物品上追加批次后拒绝仍可恢复"), Inventory->TryAddInventoryBatch(Batch, []() { return false; }, false));
	TestEqual(TEXT("恢复后数量不变"), Inventory->CountVisibleInventoryQuantityByItemId(Selected.ItemId),
		QuantityBefore + Selected.PurchaseQuantity);
	TestEqual(TEXT("恢复后保留原槽位数"), Inventory->GetInventoryEntries().Num(), ExistingItems.Num());
	for (int32 Index = 0; Index < ExistingItems.Num(); ++Index)
	{
		TestTrue(TEXT("恢复后保留原实例身份"), Inventory->GetInventoryEntries()[Index].Instance == ExistingItems[Index].Instance);
		TestEqual(TEXT("恢复后保留堆叠数量"), Inventory->GetInventoryEntries()[Index].StackCount, ExistingItems[Index].StackCount);
	}
	FCatInventoryReceiveBatch InstanceBatch;
	for (const FCatInventoryEntry& Entry : ExistingItems)
	{
		if (Entry.Instance)
		{
			FCatInventoryInstanceEntry& InstanceEntry = InstanceBatch.InstanceEntries.AddDefaulted_GetRef();
			InstanceEntry.ItemInstance = Entry.Instance;
			InstanceEntry.Count = 1;
			break;
		}
	}
	bCommitCalled = false;
	TestFalse(TEXT("带付款回调时不接收已有实例"), Inventory->TryAddInventoryBatch(InstanceBatch, [&]()
	{
		bCommitCalled = true;
		return true;
	}, false));
	TestFalse(TEXT("拒绝实例批次不执行付款"), bCommitCalled);

	// 使用独立仓库构造满仓状态；随后另建同蓝图货架，避免首次购买已耗尽限量商品而先触发售罄拒绝。
	// 原仓库已完成身份与回滚检查；销毁后保持世界只有一个无角色仓库，符合正式单仓回退规则。
	Camp->Destroy();
	ACatCampInventoryActor* FullCamp = World->SpawnActor<ACatCampInventoryActor>();
	if (!TestNotNull(TEXT("创建容量不足仓库"), FullCamp)) return false;
	// 容量配置只扩充格子，不裁掉既有格子；填满实物和堆叠后再验证拒绝，避免把售罄或配置值误当满仓。
	FCatInventoryReceiveBatch FillBatch = Batch;
	FillBatch.DefinitionEntries[0].Count = FullCamp->GetInventoryComponent()->GetInventorySlotCount()
		* FMath::Max(1, Definition->GetMaxStackCount());
	if (!TestTrue(TEXT("实际填满公共仓库"), FullCamp->GetInventoryComponent()->TryAddInventoryBatch(FillBatch))) return false;
	TestFalse(TEXT("待购批次确实无法接收"), FullCamp->GetInventoryComponent()->CanFullyAcceptInventoryBatch(Batch));
	ACatShopKioskActor* FreshKiosk = World->SpawnActor<ACatShopKioskActor>(KioskClass);
	if (!TestNotNull(TEXT("满仓测试使用未售罄的新货架"), FreshKiosk)) return false;
	UCatShopInventoryComponent* FreshShelf = FreshKiosk->GetShopInventory();
	FCatShopCartCommand FullCommand = Command;
	FullCommand.Context.RequestId = FGuid::NewGuid();
	FullCommand.ShopInventoryId = FreshShelf->GetShopInventoryId();
	// 新请求绑定新货架；报价断言先确认余额、价格和货架前提，失败即结束，不能当作容量验证通过。
	FCatShopResolvedCart FullQuote;
	ECatDomainCommandError QuoteError = ECatDomainCommandError::None;
	if (!TestTrue(TEXT("满仓测试报价与货架库存前提成立"),
		Shop->ResolveCatalogCartForAuthority(FullCommand, FreshShelf, FullQuote, QuoteError))) return false;
	FCatShopStockSnapshot StockBefore;
	FreshShelf->TryGetStockSnapshot(Selected.EntryId, StockBefore);
	const FCatShopCartTransactionResult FullResult = Trading->RunCartOrder(FullCommand, FreshShelf, FullCamp).CartTransaction;
	TestEqual(TEXT("满仓拒绝成交"), FullResult.Command.Error, ECatDomainCommandError::CapacityExceeded);
	FCatShopStockSnapshot StockAfter;
	FreshShelf->TryGetStockSnapshot(Selected.EntryId, StockAfter);
	TestEqual(TEXT("满仓不消耗货架库存"), StockAfter.RemainingStock, StockBefore.RemainingStock);
	TestEqual(TEXT("满仓不推进货架版本"), StockAfter.Revision, StockBefore.Revision);
	TestEqual(TEXT("满仓不扣公款"), Shop->GetWalletSnapshot().Balance, WalletBefore.Balance - Selected.UnitPrice);
	TestEqual(TEXT("满仓不新增账本"), Shop->GetTransactionLedgerSnapshot().Num(), 1);
	// 第二位玩家在首笔付款前已形成请求，服务端按当前余额继续成交。
	SecondCommand.ShopInventoryId = FreshShelf->GetShopInventoryId();
	FullCamp->Destroy();
	auto* SecondCamp = World->SpawnActor<ACatCampInventoryActor>();
	TestTrue(TEXT("第二位买家不受旧钱包快照限制"), Trading->RunCartOrder(SecondCommand, FreshShelf, SecondCamp).Delivery.bCommitted);
	TestEqual(TEXT("两笔独立订单只各扣一次"), Shop->GetWalletSnapshot().Balance, WalletBefore.Balance - 2 * Selected.UnitPrice);
	return !HasAnyErrors();
}

#endif
