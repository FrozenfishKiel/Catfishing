#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatRunSaveGame.h"
#include "UObject/UnrealType.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatSaveVersionMigrationTest,
	"Catfishing.Unit.Save.V8MigratesBothPublishedV6ShapesWithoutDiscardingPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 用实际旧文件验证两个 v6 形状及备份字节，再检查未知版本和缺失原文件不会被标成已迁移。
bool FCatSaveVersionMigrationTest::RunTest(const FString&)
{
	auto* Version = FindFProperty<FIntProperty>(ULocalPlayerSaveGame::StaticClass(), TEXT("SavedDataVersion"));
	if (!Version) return false;
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Player->SetControllerId(0);
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
		const FString Slot = TEXT("NumericMigration_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); UGameplayStatics::DeleteGameInSlot(Slot + TEXT("_BeforeNumericIds"), 0); };
		TArray<uint8> OriginalBytes;
		if (!UGameplayStatics::SaveGameToMemory(Legacy, OriginalBytes)) return false;
		auto* Loaded = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
		if (!Loaded) return false;
		if (!UGameplayStatics::SaveDataToSlot(OriginalBytes, Slot, 0)) return false;
		Loaded->InitializeSaveGame(Player, Slot, true);
		TArray<uint8> Backup, OriginalAfter;
		TestTrue(TEXT("原始字节备份存在"), UGameplayStatics::LoadDataFromSlot(Backup, Slot + TEXT("_BeforeNumericIds"), 0));
		UGameplayStatics::LoadDataFromSlot(OriginalAfter, Slot, 0);
		TestTrue(TEXT("备份和原文件逐字节不变"), Backup == OriginalBytes && OriginalAfter == OriginalBytes);
		TestEqual(TEXT("format upgrades"), Loaded->FormatVersion, 8);
		TestEqual(TEXT("engine data version upgrades"), Loaded->GetSavedDataVersion(), 8);
		TestEqual(TEXT("published payload discriminator is preserved"), Loaded->bHasInventoryCheckpoint, Checkpoint);
		TestEqual(TEXT("wallet is preserved without inventing legacy balance"), Loaded->TeamWalletBalance, Checkpoint ? 1234 : 0);
		TestEqual(TEXT("original item identity is preserved"), Checkpoint ? Loaded->WorldInventories[0].InventorySlots[0].ItemInstanceId
			: Loaded->CampInventory.InventorySlots[0].ItemInstanceId, ItemId);
		if (!Checkpoint) TestEqual(TEXT("old tank payload remains for deferred world restore"), Loaded->WorldFishContainers[0].PersistentKey, FString(TEXT("OldTank")));
		Loaded->HandlePreSave();
		TArray<uint8> NewBytes;
		if (!UGameplayStatics::SaveGameToMemory(Loaded, NewBytes)) return false;
		const auto* Written = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromMemory(NewBytes));
		TestTrue(TEXT("new save is distinguishable by old v6 readers"), Written && Written->FormatVersion == 8 && Written->GetSavedDataVersion() == 8);
	}
	auto* V5 = NewObject<UCatRunSaveGame>();
	V5->FormatVersion = 5;
	AddExpectedError(TEXT("Event=persistence_item_migration_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	V5->HandlePostLoad();
	TestEqual(TEXT("没有可备份原文件时不能假装迁移成功"), V5->FormatVersion, 5);
	auto* Unknown = NewObject<UCatRunSaveGame>();
	Unknown->FormatVersion = 99;
	Version->SetPropertyValue_InContainer(Unknown, 99);
	Unknown->HandlePostLoad();
	TestEqual(TEXT("future format is not silently accepted"), Unknown->FormatVersion, 99);
	return !HasAnyErrors();
}
#endif
