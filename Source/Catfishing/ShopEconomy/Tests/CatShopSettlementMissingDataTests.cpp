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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopSettlementMissingDataTest,
	"Catfishing.Unit.ShopEconomy.GraduationMissingDefinitionOrPriceStillRetiresResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatShopSettlementMissingDataTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<UCatShopEconomySettings>();
	FCatItemCatalogTestFixture Definitions;
	TGuardValue<TSoftObjectPtr<UDataTable>> TableGuard(Settings->DefaultShopCatalogTable, Settings->DefaultShopCatalogTable);
	TGuardValue<int32> DriedId(Settings->SettlementDriedItemId, 1667860);
	TStrongObjectPtr<UCatInventoryItemDefinition> Dried(NewObject<UCatInventoryItemDefinition>());
	Dried->ItemId = Settings->SettlementDriedItemId;
	Dried->InventoryMaxStackCount = 100;
	// 0 缺定义；1 缺兑换价；2 单件缺价；3 真正交付失败仍回滚。
	for (int32 Scenario = 0; Scenario < 4; ++Scenario)
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
		Wrapper.ForwardErrorMessages(this);
		auto* World = Wrapper.GetTestWorld();
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
		auto* Camp = World->SpawnActor<ACatCampInventoryActor>();
		auto* Guard = World->SpawnActor<ACatFishGuardActor>();
		auto* Inventory = Camp->GetInventoryComponent();
		if (!Mode || !Shop || !Guard || !Inventory->AddItemDefinition(Definitions.Settings->FindRuntimeDefinition(4), 2)
			|| !Shop->RestoreWalletFromAuthority(27)) return false;
		Definitions.Remove(Dried->ItemId);
		if (Scenario > 0) Definitions.Add(Dried.Get());
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
		Table->RowStruct = FCatShopCatalogTableRow::StaticStruct();
		if (Scenario != 1)
		{
			FCatShopCatalogTableRow Row;
			Row.ItemId = Dried->ItemId;
			Row.UnitPrice = 13;
			Table->AddRow(TEXT("Dried"), Row);
		}
		Settings->DefaultShopCatalogTable = Table.Get();
		if (Scenario == 3 && !Shop->RestoreWalletFromAuthority(16777216)) return false;
		Mode->RunPublicState.EndReason = ECatRunEndReason::Success;
		const int32 Exchanged = Shop->ConvertSettlementLeftoversToDriedFish();
		TestEqual(TEXT("missing data only skips exchange or unpriced equipment"), Exchanged, Scenario == 2 ? 2 : 0);
		TestEqual(TEXT("wallet cleared unless actual delivery fails"), Shop->GetWalletSnapshot().Balance, Scenario == 3 ? 16777216 : 0);
		if (Scenario != 3)
		{
			TestEqual(TEXT("unpriced inventory retired"), Inventory->CountVisibleInventoryQuantityByItemId(4), 0);
			TestTrue(TEXT("unpriced world guard retired"), Guard->IsActorBeingDestroyed());
			TestEqual(TEXT("repeat graduation is idempotent"), Shop->ConvertSettlementLeftoversToDriedFish(), 0);
		}
		else TestFalse(TEXT("failed delivery retains world resources"), Guard->IsActorBeingDestroyed());
		AddInfo(FString::Printf(TEXT("Event=blocker_settlement_verified Scenario=%d DriedFish=%d Wallet=%d Retired=%d"),
			Scenario, Exchanged, Shop->GetWalletSnapshot().Balance, Guard->IsActorBeingDestroyed()));
	}
	return !HasAnyErrors();
}
#endif
