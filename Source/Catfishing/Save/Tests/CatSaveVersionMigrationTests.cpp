#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatRunSaveGame.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatSaveVersionMigrationTest,
	"Catfishing.Unit.Save.V7MigratesBothPublishedV6ShapesWithoutDiscardingPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatSaveVersionMigrationTest::RunTest(const FString&)
{
	auto* Version = FindFProperty<FIntProperty>(ULocalPlayerSaveGame::StaticClass(), TEXT("SavedDataVersion"));
	if (!Version) return false;
	for (const bool Checkpoint : {false, true})
	{
		auto* Legacy = NewObject<UCatRunSaveGame>();
		Legacy->FormatVersion = 6;
		Version->SetPropertyValue_InContainer(Legacy, 6);
		Legacy->bHasInventoryCheckpoint = Checkpoint;
		Legacy->TeamWalletBalance = Checkpoint ? 1234 : 0;
		const FGuid ItemId = FGuid::NewGuid();
		if (Checkpoint)
		{
			auto& Host = Legacy->WorldInventories.AddDefaulted_GetRef();
			Host.HostName = TEXT("OldPublicRack");
			Host.InventorySlots.AddDefaulted_GetRef().ItemInstanceId = ItemId;
		}
		else
		{
			Legacy->CampInventory.InventorySlots.AddDefaulted_GetRef().ItemInstanceId = ItemId;
			Legacy->WorldFishContainers.AddDefaulted_GetRef().PersistentKey = TEXT("OldTank");
		}
		TArray<uint8> OriginalBytes;
		if (!UGameplayStatics::SaveGameToMemory(Legacy, OriginalBytes)) return false;
		auto* Loaded = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
		if (!Loaded) return false;
		Loaded->HandlePostLoad();
		TestEqual(TEXT("format upgrades"), Loaded->FormatVersion, 7);
		TestEqual(TEXT("engine data version upgrades"), Loaded->GetSavedDataVersion(), 7);
		TestEqual(TEXT("published payload discriminator is preserved"), Loaded->bHasInventoryCheckpoint, Checkpoint);
		TestEqual(TEXT("wallet is preserved without inventing legacy balance"), Loaded->TeamWalletBalance, Checkpoint ? 1234 : 0);
		TestEqual(TEXT("original item identity is preserved"), Checkpoint ? Loaded->WorldInventories[0].InventorySlots[0].ItemInstanceId
			: Loaded->CampInventory.InventorySlots[0].ItemInstanceId, ItemId);
		if (!Checkpoint) TestEqual(TEXT("old tank payload remains for deferred world restore"), Loaded->WorldFishContainers[0].PersistentKey, FString(TEXT("OldTank")));
		Loaded->HandlePreSave();
		TArray<uint8> NewBytes;
		if (!UGameplayStatics::SaveGameToMemory(Loaded, NewBytes)) return false;
		const auto* Written = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromMemory(NewBytes));
		TestTrue(TEXT("new save is distinguishable by old v6 readers"), Written && Written->FormatVersion == 7 && Written->GetSavedDataVersion() == 7);
	}
	auto* V5 = NewObject<UCatRunSaveGame>();
	V5->FormatVersion = 5;
	V5->HandlePostLoad();
	TestEqual(TEXT("known v5 still migrates"), V5->FormatVersion, 7);
	auto* Unknown = NewObject<UCatRunSaveGame>();
	Unknown->FormatVersion = 8;
	Version->SetPropertyValue_InContainer(Unknown, 8);
	Unknown->HandlePostLoad();
	TestEqual(TEXT("future format is not silently accepted"), Unknown->FormatVersion, 8);
	return !HasAnyErrors();
}
#endif
