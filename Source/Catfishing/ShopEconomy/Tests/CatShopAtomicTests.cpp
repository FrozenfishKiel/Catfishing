#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "GameFramework/PlayerController.h"
#include "UI/Shop/CatShopInteractionComponent.h"
#include "Camp/CatCampInventoryActor.h"
#include "FishContainers/CatFishTankActor.h"
#include "FishContainers/CatFishContainerSettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/Tests/CatItemCatalogTestFixture.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopCartAtomicTest,
	"Catfishing.Unit.ShopEconomy.CartRollbackEveryDeliveryAndPaymentStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatShopCartAtomicTest::RunTest(const FString&)
{
	for (int32 FailureStep = 0; FailureStep <= 5; ++FailureStep)
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
		Wrapper.ForwardErrorMessages(this);
		auto* World = Wrapper.GetTestWorld();
		auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
		auto* Trading = World->GetSubsystem<UCatShopTradeController>();
		auto* Kiosk = World->SpawnActor<ACatShopKioskActor>();
		auto* Rack = World->SpawnActor<ACatCampInventoryActor>();
		auto* Store = World->SpawnActor<ACatCampInventoryActor>();
		auto* Tank = World->SpawnActor<ACatFishTankActor>();
		Rack->GetInventoryComponent()->RestoreTeamStorageRoleFromAuthority(ECatTeamStorageRole::EquipmentRack);
		Store->GetInventoryComponent()->RestoreTeamStorageRoleFromAuthority(ECatTeamStorageRole::SupplyStore);
		auto* Shelf = Kiosk->GetShopInventory();
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
		Table->RowStruct = FCatShopCatalogTableRow::StaticStruct();
		const auto Add = [&](const FName Id, const int32 Definition, const int32 Price)
		{
			FCatShopCatalogTableRow Row;
			Row.ItemId = Definition;
			Row.UnitPrice = Price;
			Row.InitialStock = 3;
			Table->AddRow(Id, Row);
		};
		Add(TEXT("Rod"), 37, 11);
		Add(TEXT("Bait"), 4, 7);
		const auto& Upgrades = GetDefault<UCatFishContainerSettings>()->SharedFishTankCapacityUpgrades;
		if (!TestEqual(TEXT("formal two tank tiers"), Upgrades.Num(), 2)) return false;
		Add(TEXT("Tier1"), Upgrades[0].UpgradeItemId, 300);
		Add(TEXT("Tier2"), Upgrades[1].UpgradeItemId, 700);
		Shelf->ShopCatalogTable = Table.Get();
		if (!Shelf->RebuildInitialInventoryFromCatalog() || !Shop->RestoreWalletFromAuthority(2000)) return false;
		int32 Broadcasts = 0, Observed = 0;
		FCatShopPublicTransaction Last;
		Shop->OnPublicTransactionCommitted.AddLambda([&](const auto& Cart) { ++Broadcasts; Last = Cart; });
		Rack->GetInventoryComponent()->OnInventoryObservedChanged.AddLambda([&](auto...) { ++Observed; });
		Store->GetInventoryComponent()->OnInventoryObservedChanged.AddLambda([&](auto...) { ++Observed; });
		Tank->GetFishInventoryComponent()->OnInventoryObservedChanged.AddLambda([&](auto...) { ++Observed; });
		FCatShopCartCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.StableNetId = TEXT("CartAtomicFixture");
		Command.ShopInventoryId = Shelf->GetShopInventoryId();
		for (FName Id : {FName("Rod"), FName("Bait"), FName("Tier1"), FName("Tier2")})
			Command.Lines.Add({Id, 1});
		if (FailureStep == 0)
		{
			auto Unaffordable = Command;
			Unaffordable.Context.RequestId = FGuid::NewGuid();
			for (auto& Line : Unaffordable.Lines) Line.CartCount = 3;
			TestEqual(TEXT("insufficient money has its own reason"), Trading->RunCartOrder(Unaffordable, Shelf, Rack).Delivery.FailureReason, FName(TEXT("InsufficientFunds")));
			Unaffordable.Context.RequestId = FGuid::NewGuid();
			Unaffordable.Lines[0].CartCount = 4;
			TestEqual(TEXT("stock shortage has its own reason"), Trading->RunCartOrder(Unaffordable, Shelf, Rack).Delivery.FailureReason, FName(TEXT("OutOfStock")));
		}
		Trading->FailDeliveryStepForTest = FailureStep < 4 ? FailureStep : INDEX_NONE;
		Shop->FailPaymentForTest = FailureStep == 4;
		const auto Result = Trading->RunCartOrder(Command, Shelf, Rack);
		const bool Success = FailureStep == 5;
		TestEqual(TEXT("cart commits only after every stage"), Result.CartTransaction.Command.bCommitted, Success);
		TestEqual(TEXT("wallet all or nothing"), Shop->GetWalletSnapshot().Balance, Success ? 982 : 2000);
		TestEqual(TEXT("rod all or nothing"), Rack->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(37), Success ? 1 : 0);
		TestEqual(TEXT("bait all or nothing"), Store->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), Success ? 1 : 0);
		TestEqual(TEXT("both upgrade subitems commit together"), Tank->GetCapacityTier(), Success ? 2 : 0);
		TestEqual(TEXT("actual fish capacity rolls back"), Tank->GetFishInventoryComponent()->GetInventorySlotCount(), Success ? 30 : 10);
		TestEqual(TEXT("exactly one success broadcast"), Broadcasts, Success ? 1 : 0);
		TestEqual(TEXT("no provisional inventory notification"), Observed, Success ? 3 : 0);
		for (const auto& Line : Command.Lines)
		{
			FCatShopStockSnapshot Stock;
			Shelf->TryGetStockSnapshot(Line.EntryId, Stock);
			TestEqual(TEXT("stock all or nothing"), Stock.RemainingStock, Success ? 2 : 3);
		}
		Trading->FailDeliveryStepForTest = INDEX_NONE;
		Shop->FailPaymentForTest = false;
		const auto Replay = Trading->RunCartOrder(Command, Shelf, Rack);
		TestFalse(TEXT("terminal replay never recommits"), Replay.CartTransaction.Command.bCommitted);
		TestEqual(TEXT("replay never recharges"), Shop->GetWalletSnapshot().Balance, Success ? 982 : 2000);
		TestEqual(TEXT("replay never broadcasts"), Broadcasts, Success ? 1 : 0);
		if (Success)
		{
			TestEqual(TEXT("all item names in one cart"), Last.Items.Num(), 4);
			TestTrue(TEXT("broadcast carries explicit CartId"), Last.CartId.IsValid() && Last.CartId == Result.CartTransaction.Transactions[0].CartId);
			TestEqual(TEXT("one public record per cart"), Shop->BuildPublicSnapshot().Transactions.Num(), 1);
			Command.Context.RequestId = FGuid::NewGuid();
			Command.Lines.SetNum(1);
			TestTrue(TEXT("adjacent same buyer second cart"), Trading->RunCartOrder(Command, Shelf, Rack).Delivery.bCommitted);
			TestEqual(TEXT("two explicit carts remain separate"), Shop->BuildPublicSnapshot().Transactions.Num(), 2);
			FCatShopFishSaleCommand Sale;
			Sale.Context.RequestId = FGuid::NewGuid();
			Sale.Context.StableNetId = TEXT("CartAtomicFixture");
			Sale.InventoryCommitId = FGuid::NewGuid();
			for (auto SoftDefinition : GetDefault<UCatFishCatalogSettings>()->Definitions)
			{
				auto* Fish = SoftDefinition.LoadSynchronous();
				if (!Fish || Fish->BodyClass != ECatFishBodyClass::Giant) continue;
				for (int32 Index = 0; Index < 2; ++Index)
				{
					auto& Line = Sale.Fish.AddDefaulted_GetRef();
					Line.FishInstanceId = FGuid::NewGuid();
					Line.ItemId = Fish->GetItemId();
					Line.WeightKilograms = 20.0 + Index;
				}
				break;
			}
			if (!TestEqual(TEXT("formal giant fish fixture available"), Sale.Fish.Num(), 2)) return false;
			const auto Sold = Shop->ApplyFishSale(Sale);
			TestTrue(TEXT("fish sale commits one income"), Sold.Command.bCommitted);
			TestTrue(TEXT("sale broadcast carries all fish and giant feedback"), Last.bFishSale && Last.bContainsGiantFish && Last.Items.Num() == 2);
			TestEqual(TEXT("sale broadcast shows actual income"), Last.WalletDelta, Sold.Transaction.WalletDelta);
			TestEqual(TEXT("one broadcast for a two-fish sale"), Broadcasts, 3);
			// 小鱼干使用普通道具定义，也必须按已定消耗品归入公库。
			auto* InventorySettings = GetMutableDefault<UCatInventorySettings>();
			FCatItemCatalogTestFixture Catalog;
			// 正式小鱼干尚缺定义；本用例自建有效数字身份验证交付，不依赖未配置的生产编号零。
			TGuardValue<int32> DriedIdGuard(GetMutableDefault<UCatShopEconomySettings>()->SettlementDriedItemId, 900001);
			TStrongObjectPtr<UCatInventoryItemDefinition> Dried(NewObject<UCatInventoryItemDefinition>());
			Dried->ItemId = GetDefault<UCatShopEconomySettings>()->SettlementDriedItemId;
			Catalog.Remove(Dried->ItemId);
			Catalog.Add(Dried.Get());
			Add(TEXT("Dried"), Dried->ItemId, 13);
			TestTrue(TEXT("refresh fixture shelf with dried fish"), Shelf->RebuildInitialInventoryFromCatalog());
			Command.Context.RequestId = FGuid::NewGuid();
			Command.Lines[0].EntryId = TEXT("Dried");
			TestTrue(TEXT("configured dried fish can be purchased"), Trading->RunCartOrder(Command, Shelf, Rack).Delivery.bCommitted);
			TestEqual(TEXT("dried fish goes to supply store"), Store->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(Dried->ItemId), 1);
			TestEqual(TEXT("dried fish never goes to equipment rack"), Rack->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(Dried->ItemId), 0);
			auto* Player = World->SpawnActor<APlayerController>();
			Player->SetAsLocalPlayerController();
			TestTrue(TEXT("open kiosk can interact"), Kiosk->CanInteract_Implementation(Player));
			Shop->CloseCommands();
			TestFalse(TEXT("closed kiosk cannot interact"), Kiosk->CanInteract_Implementation(Player));
			TestTrue(TEXT("closed kiosk hides its prompt"), Kiosk->GetInteractionPrompt_Implementation().IsEmpty());
			TestFalse(TEXT("closed page entry rejects before widget creation"), Kiosk->GetShopInteraction()->OpenShopForPlayer(Player));
			TestFalse(TEXT("closed state is included in public snapshot"), Shop->BuildPublicSnapshot().bCommandsOpen);
			Command.Context.RequestId = FGuid::NewGuid();
			TestEqual(TEXT("new cart after closing is rejected"), Trading->RunCartOrder(Command, Shelf, Rack).Delivery.Error, ECatDomainCommandError::CommandsClosed);
		}
		// Remove captured local references before World teardown sends change notifications.
		Shop->OnPublicTransactionCommitted.Clear();
		Rack->GetInventoryComponent()->OnInventoryObservedChanged.Clear();
		Store->GetInventoryComponent()->OnInventoryObservedChanged.Clear();
		Tank->GetFishInventoryComponent()->OnInventoryObservedChanged.Clear();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopSettlementTest,
	"Catfishing.Unit.ShopEconomy.GraduationOriginalPriceCatalogRateAndFailureCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatShopSettlementTest::RunTest(const FString&)
{
	auto* ShopSettings = GetMutableDefault<UCatShopEconomySettings>();
	auto* InventorySettings = GetMutableDefault<UCatInventorySettings>();
	const auto SavedCatalog = ShopSettings->DefaultShopCatalogTable;
	FCatItemCatalogTestFixture Catalog;
	struct FRestoreSettings
	{
		UCatShopEconomySettings* Shop;
		TSoftObjectPtr<UDataTable> Catalog;
		~FRestoreSettings() { Shop->DefaultShopCatalogTable = Catalog; }
	} Restore{ShopSettings, SavedCatalog};
	TStrongObjectPtr<UCatInventoryItemDefinition> Dried(NewObject<UCatInventoryItemDefinition>());
	TStrongObjectPtr<UCatInventoryItemDefinition> Gear(NewObject<UCatInventoryItemDefinition>());
	Dried->ItemId = 1322042;
	TGuardValue<int32> DriedId(ShopSettings->SettlementDriedItemId, Dried->ItemId);
	Dried->InventoryMaxStackCount = 100;
	Gear->ItemId = 1243500;
	Gear->InventoryMaxStackCount = 10;
	Catalog.Remove(Dried->ItemId);
	Catalog.Remove(Gear->ItemId);
	Catalog.Add(Dried.Get());
	Catalog.Add(Gear.Get());
	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
	Table->RowStruct = FCatShopCatalogTableRow::StaticStruct();
	for (auto Pair : {TPair<int32, int32>(Dried->ItemId, 13), TPair<int32, int32>(Gear->ItemId, 25)})
	{
		FCatShopCatalogTableRow Row;
		Row.ItemId = Pair.Key;
		Row.UnitPrice = Pair.Value;
		Row.bUnlimitedStock = true;
		Table->AddRow(FName(*FString::FromInt(Pair.Key)), Row);
	}
	FCatShopCatalogTableRow GuardRow;
	GuardRow.ItemId = 11;
	GuardRow.UnitPrice = 5;
	GuardRow.bUnlimitedStock = true;
	Table->AddRow(TEXT("11"), GuardRow);
	ShopSettings->DefaultShopCatalogTable = Table.Get();
	for (const bool Success : {true, false})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
		Wrapper.ForwardErrorMessages(this);
		auto* World = Wrapper.GetTestWorld();
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
		auto* Camp = World->SpawnActor<ACatCampInventoryActor>();
		auto* Inventory = Camp->GetInventoryComponent();
		if (!Mode || !Shop || !Inventory->AddItemDefinition(Gear.Get(), 2) || !Shop->RestoreWalletFromAuthority(27)) return false;
		auto* Guard = World->SpawnActor<ACatFishGuardActor>();
		auto* Fish = NewObject<UCatFishInventoryItemInstance>(Guard);
		Fish->SetItemDefinition(LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver")));
		const FGuid FishId = FGuid::NewGuid();
		if (!Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FishId, TEXT("SettlementFixture"), 3.75)
			|| !Guard->GetFishInventoryComponent()->AddItemInstance(Fish, 1)) return false;
		Shop->CloseCommands();
		TestEqual(TEXT("closing alone never converts wallet"), Shop->GetWalletSnapshot().Balance, 27);
		TestEqual(TEXT("closing alone keeps equipment"), Inventory->CountVisibleInventoryQuantityByItemId(Gear->ItemId), 2);
		Mode->RunPublicState.EndReason = Success ? ECatRunEndReason::Success : ECatRunEndReason::WorldProgressDepleted;
		if (Success)
		{
			TestEqual(TEXT("(27 + 2 * original 25 + guard 5) / configured 13 floors to 6"), Shop->ConvertSettlementLeftoversToDriedFish(), 6);
			TestEqual(TEXT("graduation replay has no second exchange"), Shop->ConvertSettlementLeftoversToDriedFish(), 0);
		}
		else
		{
			// Failure must clear resources even if no exchange price/asset is configured.
			ShopSettings->DefaultShopCatalogTable.Reset();
			TestEqual(TEXT("failure cannot enter graduation exchange"), Shop->ConvertSettlementLeftoversToDriedFish(), 0);
			Shop->ClearFailedRunResourcesFromAuthority();
			Shop->ClearFailedRunResourcesFromAuthority();
		}
		TestEqual(TEXT("terminal wallet including remainder is zero"), Shop->GetWalletSnapshot().Balance, 0);
		TestEqual(TEXT("equipment converted or cleared"), Inventory->CountVisibleInventoryQuantityByItemId(Gear->ItemId), 0);
		TestEqual(TEXT("dried fish graduation only"), Inventory->CountVisibleInventoryQuantityByItemId(Dried->ItemId), Success ? 6 : 0);
		TestTrue(TEXT("guard shell is converted or cleared"), Guard->IsActorBeingDestroyed());
		int32 RemainingFish = 0;
		for (TActorIterator<ACatFishPickupActor> It(World); It; ++It)
			if (It->GetPresentationState().FishInstanceId == FishId && !It->IsHidden()) ++RemainingFish;
		TestEqual(TEXT("graduation preserves guard fish; failure clears fish"), RemainingFish, Success ? 1 : 0);
	}
	return !HasAnyErrors();
}
#endif
