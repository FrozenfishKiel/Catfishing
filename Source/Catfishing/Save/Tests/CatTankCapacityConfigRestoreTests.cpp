#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/GameInstance.h"
#include "Data/CatFishCatalogSettings.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "FishContainers/CatFishContainerSettings.h"
#include "FishContainers/CatFishTankActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatSaveSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatTankCapacityConfigRestoreTest,
	"Catfishing.Unit.Save.TankCapacityConfigChangeColdDiskRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatTankCapacityConfigRestoreTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<UCatFishContainerSettings>();
	TGuardValue<int32> CapacityGuard(Settings->SharedFishTankCapacity, 10);
	const FString SlotName = TEXT("CatBlockerCapacity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	struct FCleanup { FString Slot; ~FCleanup() { UGameplayStatics::DeleteGameInSlot(Slot, 0); } } Cleanup{SlotName};
	FGuid FishId;
	const FName SlotId(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	{
		FTestWorldWrapper Source;
		if (!Source.CreateTestWorld(EWorldType::Game) || !Source.BeginPlayInTestWorld()) return false;
		Source.ForwardErrorMessages(this);
		auto* World = Source.GetTestWorld();
		auto* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
		auto* Tank = World->SpawnActor<ACatFishTankActor>();
		TStrongObjectPtr<UCatRunSaveGame> Candidate(NewObject<UCatRunSaveGame>());
		auto* Fish = NewObject<UCatFishInventoryItemInstance>(Tank);
		Fish->SetItemDefinition(GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(22));
		FishId = FGuid::NewGuid();
		if (!Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FishId, TEXT("CapacityFixture"), 1.0)
			|| !Tank->GetFishInventoryComponent()->AddItemInstance(Fish, 1)) return false;
		FText Failure;
		if (!TestTrue(TEXT("capture with original capacity"), Save->CaptureWorldInventories(*World, Candidate->WorldInventories, Failure))) return false;
		Candidate->SlotId = SlotId;
		Candidate->DisplayName = TEXT("Capacity config regression");
		Candidate->DayIndex = 1;
		Candidate->bHasWorldSnapshot = Candidate->bHasPlayerSnapshot = Candidate->bHasInventoryCheckpoint = true;
		// 旧尾槽有鱼、前方有空位：缩容应搬入空位，不因旧槽号过大而丢鱼或拒绝。
		auto& Slots = Candidate->WorldInventories[0].InventorySlots;
		Swap(Slots[0], Slots[9]);
		if (!TestTrue(TEXT("write isolated disk file"), UGameplayStatics::SaveGameToSlot(Candidate.Get(), SlotName, 0))) return false;
	}
	for (const int32 ConfigCapacity : {12, 5, 0})
	{
		Settings->SharedFishTankCapacity = ConfigCapacity;
		TGuardValue<TArray<FCatSharedFishTankCapacityUpgrade>> Upgrades(Settings->SharedFishTankCapacityUpgrades,
			ConfigCapacity == 0 ? TArray<FCatSharedFishTankCapacityUpgrade>() : Settings->SharedFishTankCapacityUpgrades);
		TStrongObjectPtr<UCatRunSaveGame> Disk(Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)));
		if (!TestNotNull(TEXT("cold disk read after config changed"), Disk.Get())) return false;
		FTestWorldWrapper Destination;
		if (!Destination.CreateTestWorld(EWorldType::Game)) return false;
		Destination.ForwardErrorMessages(this);
		auto* World = Destination.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!World->SetGameMode(URL)) return false;
		auto* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
		Save->ActiveSlotId = SlotId;
		Save->PendingRestoreSaveGame = Disk.Get();
		Save->bLoadedRunForTravel = true;
		if (!TestTrue(TEXT("production startup restores changed-capacity save"), Destination.BeginPlayInTestWorld())
			|| !TestTrue(TEXT("world restore committed before gameplay"), Save->bWorldRestoreApplied)) return false;
		ACatFishTankActor* Tank = nullptr;
		for (TActorIterator<ACatFishTankActor> It(World); It; ++It) Tank = *It;
		if (!TestNotNull(TEXT("saved tank recreated"), Tank)) return false;
		auto* Inventory = Tank->GetFishInventoryComponent();
		const int32 Expected = ConfigCapacity > 0 ? ConfigCapacity : 20;
		TestEqual(TEXT("current config or actor fallback determines slots"), Inventory->GetInventorySlotCount(), Expected);
		TestTrue(TEXT("fish identity survives including tail-slot compaction"), Inventory->FindInventorySlotIndexFromInstanceId(FishId) != INDEX_NONE);
		FText Failure;
		TArray<FCatSavedWorldInventory> Captured;
		TestTrue(TEXT("periodic capture supports current or fallback capacity"), Save->CaptureWorldInventories(*World, Captured, Failure));
		AddInfo(FString::Printf(TEXT("Event=blocker_capacity_verified SavedCapacity=10 ConfigCapacity=%d TargetCapacity=%d WorldRestored=%d Fish=%s"),
			ConfigCapacity, Inventory->GetInventorySlotCount(), Save->bWorldRestoreApplied, *FishId.ToString()));
		// 结构错误仍拒绝；真正装不下走现有超限失败，不改库存或磁盘。
		Settings->SharedFishTankCapacity = 1;
		auto& Slots = Disk->WorldInventories[0].InventorySlots;
		Slots[0] = Slots[9];
		Slots[0].ItemInstanceId = FGuid::NewGuid();
		TestFalse(TEXT("actual occupied overflow rejected before modifying hosts"), Save->RestoreWorldInventories(*World, Disk->WorldInventories, Failure));
		TestEqual(TEXT("overflow does not resize live tank"), Inventory->GetInventorySlotCount(), Expected);
		TestTrue(TEXT("overflow preserves live fish"), Inventory->FindInventorySlotIndexFromInstanceId(FishId) != INDEX_NONE);
		Slots[0] = FCatSavedRunInventorySlot();
		Settings->SharedFishTankCapacity = ConfigCapacity;
		const int32 SavedCapacity = Disk->WorldInventories[0].Capacity;
		Disk->WorldInventories[0].Capacity = 0;
		TestFalse(TEXT("invalid disk structure still rejected"), Save->ValidateLoadedRunSaveGame(*Disk, SlotId, Failure));
		Disk->WorldInventories[0].Capacity = SavedCapacity;
	}
	return !HasAnyErrors();
}
#endif
