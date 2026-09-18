#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Camp/CatCampInventoryActor.h"
#include "Data/CatFishDefinition.h"
#include "FishContainers/CatFishTankActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatSaveSubsystem.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatShopWorldCheckpointTest,
	"Catfishing.Unit.Save.MultiStorageFishTierWalletWorldColdDiskRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 冷读档回归：源世界保存真实库存、道具余额与团队已购数，销毁后从磁盘恢复；同时核对原有鱼护、鱼重、公款和世界进度。
bool FCatShopWorldCheckpointTest::RunTest(const FString& Parameters)
{
	const FName SlotId(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString SlotName = UCatSaveSubsystem::MakeRunSlotFileName(SlotId);
	struct FCleanup { FString Slot; ~FCleanup() { UGameplayStatics::DeleteGameInSlot(Slot, 0); } } Cleanup{SlotName};
	FGuid FishId, GuardItemId, GuardFishId, WaterItemId;
	FName RackName, StoreName, TankName, GuardName;
	const FVector GuardSavedLocation(120, 230, 340);
	{
		FTestWorldWrapper Source;
		if (!Source.CreateTestWorld(EWorldType::Game)) return false;
		Source.ForwardErrorMessages(this);
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!Source.GetTestWorld()->SetGameMode(URL) || !Source.BeginPlayInTestWorld()) return false;
		UWorld* World = Source.GetTestWorld();
		auto* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
		auto* Rack = World->SpawnActor<ACatCampInventoryActor>();
		auto* Store = World->SpawnActor<ACatCampInventoryActor>();
		auto* Tank = World->SpawnActor<ACatFishTankActor>();
		if (!Save || !Mode || !Shop || !Rack || !Store || !Tank) return false;
		RackName = Rack->GetFName(); StoreName = Store->GetFName(); TankName = Tank->GetFName();
		TestTrue(TEXT("rack role"), Rack->GetInventoryComponent()->RestoreTeamStorageRoleFromAuthority(ECatTeamStorageRole::EquipmentRack));
		TestTrue(TEXT("store role"), Store->GetInventoryComponent()->RestoreTeamStorageRoleFromAuthority(ECatTeamStorageRole::SupplyStore));
		auto* Rod = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(37);
		auto* Bait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(4);
		if (!TestNotNull(TEXT("formal rod"), Rod) || !TestNotNull(TEXT("formal bait"), Bait)) return false;
		TestTrue(TEXT("rack owns rod"), Rack->GetInventoryComponent()->AddItemDefinition(Rod, 1));
		TestTrue(TEXT("store owns bait"), Store->GetInventoryComponent()->AddItemDefinition(Bait, 3));
		auto* Water = LoadObject<UCatInventoryItemDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Items/Item_WaterSprayer.Item_WaterSprayer"));
		auto* Net = LoadObject<UCatInventoryItemDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Items/Item_CastNet.Item_CastNet"));
		if (!Water || !Net || !Store->GetInventoryComponent()->AddItemDefinition(Water, 1)) return false;
		for (auto* Item : Store->GetInventoryComponent()->GetAllItems()) if (Item->GetItemDefinition() == Water)
		{ WaterItemId = Item->GetItemInstanceId(); TestTrue(TEXT("保存前剩余两次水量"), Item->SetRemainingResourceFromAuthority(2)); }
		TestTrue(TEXT("保存前团队已购买一件网"), Shop->RestoreRunPurchaseCountsFromAuthority({{Net->ItemId, 1}}));
		auto* Guard = World->SpawnActor<ACatFishGuardActor>();
		Guard->SetActorLocation(GuardSavedLocation);
		GuardName = Guard->GetFName();
		auto* GuardItem = NewObject<UCatFishGuardInventoryItemInstance>(Store);
		GuardItem->SetItemDefinition(GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(11));
		GuardItemId = GuardItem->GetItemInstanceId();
		if (!Guard->InitializeFromInventoryFromAuthority(GuardItem, 1)
			|| !Store->GetInventoryComponent()->AddItemInstance(GuardItem, 1)) return false;
		TestTrue(TEXT("buy first tier"), Tank->ApplyCapacityUpgradeFromAuthority(1, FGuid::NewGuid()));
		TestTrue(TEXT("buy second tier"), Tank->ApplyCapacityUpgradeFromAuthority(2, FGuid::NewGuid()));
		auto* Definition = LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
		auto* Fish = NewObject<UCatFishInventoryItemInstance>(Tank);
		Fish->SetItemDefinition(Definition);
		FishId = FGuid::NewGuid();
		if (!Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FishId, TEXT("CheckpointTest"), 3.75)
			|| !Tank->GetFishInventoryComponent()->AddItemInstance(Fish, 1)) return false;
		auto* GuardFish = NewObject<UCatFishInventoryItemInstance>(Guard);
		GuardFish->SetItemDefinition(Definition);
		GuardFishId = FGuid::NewGuid();
		if (!GuardFish->InitializeFishFromAuthority(FGuid::NewGuid(), GuardFishId, TEXT("CheckpointTest"), 2.5)
			|| !Guard->GetFishInventoryComponent()->AddItemInstance(GuardFish, 1)) return false;
		TestTrue(TEXT("set authority wallet"), Shop->RestoreWalletFromAuthority(1234));
		Mode->RunPublicState.WorldProgress = 47;
		Mode->RunPublicState.Phase.DayIndex = 3;
		Mode->RunPublicState.EndReason = ECatRunEndReason::None;
		Save->ActiveSlotId = SlotId;
		Save->PendingRestoreSaveGame = NewObject<UCatRunSaveGame>(Save);
		Save->PendingRestoreSaveGame->bHasPlayerSnapshot = true;
		Save->bWorldRestoreApplied = true;
		auto& Summary = Save->SlotSummaries.AddDefaulted_GetRef();
		Summary.SlotId = SlotId; Summary.DisplayName = TEXT("Batch7E isolated checkpoint");
		TStrongObjectPtr<UCatRunSaveGame> Candidate(NewObject<UCatRunSaveGame>());
		FText Failure;
		if (!TestTrue(TEXT("production capture accepts multiple storage roles"), Save->BuildActiveRunSaveGame(*Candidate, Failure)))
		{ AddError(Failure.ToString()); return false; }
		TestEqual(TEXT("captures current hosts including carried guard"), Candidate->WorldInventories.Num(), 4);
		if (!TestTrue(TEXT("write actual isolated file"), UGameplayStatics::SaveGameToSlot(Candidate.Get(), SlotName, 0))) return false;
		TestTrue(TEXT("release active run before world exit"), Save->ReleaseActiveRun());
	}
	// 原 World、服务与所有物品实例已销毁，下面只从真实磁盘文件重建。
	TStrongObjectPtr<UCatRunSaveGame> Disk(Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)));
	if (!TestNotNull(TEXT("cold disk load"), Disk.Get())) return false;
	FTestWorldWrapper Destination;
	if (!Destination.CreateTestWorld(EWorldType::Game)) return false;
	Destination.ForwardErrorMessages(this);
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!Destination.GetTestWorld()->SetGameMode(URL)) return false;
	UWorld* World = Destination.GetTestWorld();
	auto* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	Save->ActiveSlotId = SlotId;
	Save->PendingRestoreSaveGame = Disk.Get();
	Save->bLoadedRunForTravel = true;
	// 模拟地图已含同名鱼护，但其默认落点与保存位置不同。
	FActorSpawnParameters ExistingGuardSpawn;
	ExistingGuardSpawn.Name = GuardName;
	ExistingGuardSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!World->SpawnActor<ACatFishGuardActor>(FVector::ZeroVector, FRotator::ZeroRotator, ExistingGuardSpawn)) return false;
	if (!TestTrue(TEXT("production startup consumes cold checkpoint before StateTree"), Destination.BeginPlayInTestWorld())
		|| !TestTrue(TEXT("production world restore after hosts ready"), Save->bWorldRestoreApplied))
	{ AddError(Save->GetLastResultText().ToString()); return false; }
	ACatCampInventoryActor* Rack = nullptr;
	ACatCampInventoryActor* Store = nullptr;
	ACatFishTankActor* Tank = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetFName() == RackName) Rack = Cast<ACatCampInventoryActor>(*It);
		if (It->GetFName() == StoreName) Store = Cast<ACatCampInventoryActor>(*It);
		if (It->GetFName() == TankName) Tank = Cast<ACatFishTankActor>(*It);
	}
	if (!TestTrue(TEXT("all three hosts recreated"), Rack && Store && Tank)) return false;
	TestEqual(TEXT("rack role survives"), Rack->GetInventoryComponent()->GetTeamStorageRole(), ECatTeamStorageRole::EquipmentRack);
	TestEqual(TEXT("store role survives"), Store->GetInventoryComponent()->GetTeamStorageRole(), ECatTeamStorageRole::SupplyStore);
	TestEqual(TEXT("rod survives"), Rack->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(37), 1);
	TestEqual(TEXT("bait survives"), Store->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 3);
	const int32 WaterSlot = Store->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(WaterItemId);
	if (!TestTrue(TEXT("湿毛器身份冷读档保留"), WaterSlot != INDEX_NONE)) return false;
	TestEqual(TEXT("冷读档不把水量补满"), Store->GetInventoryComponent()->GetInventoryEntryAtSlot(WaterSlot)->Instance->GetRemainingResource(), 2);
	auto* Net = LoadObject<UCatInventoryItemDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Items/Item_CastNet.Item_CastNet"));
	TestEqual(TEXT("冷读档保留团队渔网已购数"), World->GetSubsystem<UCatShopEconomyService>()->GetRunPurchaseCounts().FindRef(Net->ItemId), 1);
	TestEqual(TEXT("tier survives"), Tank->GetCapacityTier(), 2);
	TestEqual(TEXT("thirty real slots survive"), Tank->GetFishInventoryComponent()->GetInventorySlotCount(), 30);
	const int32 FishSlot = Tank->GetFishInventoryComponent()->FindInventorySlotIndexFromInstanceId(FishId);
	if (!TestTrue(TEXT("fish stable identity survives"), FishSlot != INDEX_NONE)) return false;
	const auto* Fish = Cast<UCatFishInventoryItemInstance>(Tank->GetFishInventoryComponent()->GetInventoryEntryAtSlot(FishSlot)->Instance);
	TestTrue(TEXT("fish runtime type and kilograms survive"), Fish && Fish->GetFishWeightKilograms() == 3.75);
	TestEqual(TEXT("world progress survives"), Mode->GetRunPublicState().WorldProgress, 47);
	TestEqual(TEXT("wallet survives"), World->GetSubsystem<UCatShopEconomyService>()->GetWalletSnapshot().Balance, 1234);
	TestEqual(TEXT("saved day resumes instead of day one"), Mode->GetRunPublicState().Phase.DayIndex, 3);
	const int32 GuardSlot = Store->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(GuardItemId);
	if (!TestTrue(TEXT("guard item identity survives"), GuardSlot != INDEX_NONE)) return false;
	auto* GuardItem = Store->GetInventoryComponent()->GetInventoryEntryAtSlot(GuardSlot)->Instance.Get();
	auto* Guard = Cast<ACatFishGuardActor>(GuardItem->GetWorldActor());
	if (!TestTrue(TEXT("restored guard links original host"), Guard && Guard->GetFName() == GuardName && !Guard->IsGrounded())) return false;
	TestTrue(TEXT("existing map guard restores its saved location"), Guard->GetActorLocation().Equals(GuardSavedLocation));
	GuardItem->SetRuntimeOwnerActor(Guard);
	TestTrue(TEXT("placing restored guard reuses host"), Guard->IsGrounded() && GuardItem->GetWorldActor() == Guard);
	const int32 GuardFishSlot = Guard->GetFishInventoryComponent()->FindInventorySlotIndexFromInstanceId(GuardFishId);
	if (!TestTrue(TEXT("placed guard retains the same fish"), GuardFishSlot != INDEX_NONE)) return false;
	const auto* GuardFish = Cast<UCatFishInventoryItemInstance>(Guard->GetFishInventoryComponent()->GetInventoryEntryAtSlot(GuardFishSlot)->Instance);
	TestTrue(TEXT("guard fish weight survives cold load and placement"), GuardFish && GuardFish->GetFishWeightKilograms() == 2.5);
	FText Failure;
	for (auto& Host : Disk->WorldInventories)
	{
		const int32 EmptySlot = Host.InventorySlots.IndexOfByPredicate([](const auto& Slot) { return (Slot.ItemId == 0); });
		if (EmptySlot == INDEX_NONE) continue;
		Host.InventorySlots[EmptySlot].FishGuardHostName = GuardName;
		TestFalse(TEXT("empty slot cannot claim a guard host"), Save->ValidateLoadedRunSaveGame(*Disk, SlotId, Failure));
		Host.InventorySlots[EmptySlot].FishGuardHostName = NAME_None;
		break;
	}
	Disk->WorldInventories[1].HostName = Disk->WorldInventories[0].HostName;
	TestFalse(TEXT("duplicate hosts rejected before overwrite"), Save->ValidateLoadedRunSaveGame(*Disk, SlotId, Failure));
	return true;
}
#endif
