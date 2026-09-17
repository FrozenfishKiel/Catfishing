#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Save/CatRunSaveGame.h"
#include "UObject/UnrealType.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatSaveVersionMigrationTest,
	"Catfishing.Unit.Save.V9PreservesAndRejectsOldDevelopmentFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 实际旧文件读盘回归：版本和物品编号不被自动改写，读盘前后文件字节相同；新对象明确使用 v9。
bool FCatSaveVersionMigrationTest::RunTest(const FString&)
{
	auto* Version = FindFProperty<FIntProperty>(ULocalPlayerSaveGame::StaticClass(), TEXT("SavedDataVersion"));
	if (!Version) return false;
	auto* Player = NewObject<ULocalPlayer>(GEngine); Player->SetControllerId(0);
	for (const int32 OldVersion : {6, 8, 99})
	{
		auto* Legacy = NewObject<UCatRunSaveGame>(); Legacy->FormatVersion = OldVersion;
		Version->SetPropertyValue_InContainer(Legacy, OldVersion);
		auto& Item = Legacy->PlayerSnapshot.InventorySlots.AddDefaulted_GetRef();
		Item.ItemId = 1183317; Item.ItemInstanceId = FGuid::NewGuid(); Item.Quantity = 1;
		const FGuid OriginalIdentity = Item.ItemInstanceId;
		const FString Slot = TEXT("ItemSchemaRejection_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
		TArray<uint8> OriginalBytes;
		if (!UGameplayStatics::SaveGameToMemory(Legacy, OriginalBytes) || !UGameplayStatics::SaveDataToSlot(OriginalBytes, Slot, 0)) return false;
		auto* Loaded = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
		if (!Loaded) return false;
		Loaded->InitializeSaveGame(Player, Slot, true);
		TArray<uint8> AfterBytes;
		TestTrue(TEXT("旧文件仍可原样读取"), UGameplayStatics::LoadDataFromSlot(AfterBytes, Slot, 0) && AfterBytes == OriginalBytes);
		TestEqual(TEXT("文件格式没有伪装升级"), Loaded->FormatVersion, OldVersion);
		TestEqual(TEXT("引擎数据版本没有伪装升级"), Loaded->GetSavedDataVersion(), OldVersion);
		TestEqual(TEXT("稳定物品编号保留"), Loaded->PlayerSnapshot.InventorySlots[0].ItemId, 1183317);
		TestEqual(TEXT("实物身份保留"), Loaded->PlayerSnapshot.InventorySlots[0].ItemInstanceId, OriginalIdentity);
	}
	auto* Current = NewObject<UCatRunSaveGame>(); Current->HandlePreSave();
	TestEqual(TEXT("新载荷格式"), Current->FormatVersion, 9);
	TestEqual(TEXT("新引擎数据版本"), Current->GetSavedDataVersion(), 9);
	return !HasAnyErrors();
}
#endif
