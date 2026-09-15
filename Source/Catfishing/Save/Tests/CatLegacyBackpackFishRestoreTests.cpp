#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/Tests/CatFishingEquipmentTestFixtures.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Save/CatSaveSubsystem.h"
#include "Data/CatFishCatalogSettings.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Camp/CatCampInventoryActor.h"

// 合并保留本地库存规则：旧存档鱼的原格、数量与身份均恢复，玩家身体恢复仍只提交一次。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLegacyBackpackFishRestoreTest,
	"Catfishing.Unit.Save.LegacyV6BackpackFishPreservesInventoryAndRestoresPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatLegacyBackpackFishRestoreTest::RunTest(const FString& Parameters)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.World.CreateTestWorld(EWorldType::Game)) return false;
	Scene.World.ForwardErrorMessages(this);
	UWorld* World = Scene.World.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	Scene.Floor = Scene.AddBox(FVector(0, 0, -10), FVector(2000, 2000, 10));
	auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
	auto* Player = World->SpawnActor<ACatfishingPlayerState>();
	auto* Cat = Scene.SpawnCat(FVector(0, 0, 20));
	UGameInstance* Instance = World->GetGameInstance();
	if (!Controller || !Player || !Cat || !Instance) return false;
	UCatSaveSubsystem* Save = Instance->GetSubsystem<UCatSaveSubsystem>();
	if (!Save) return false;
	Save->PendingRestoreSaveGame = NewObject<UCatRunSaveGame>(Save);
	Save->PendingRestoreSaveGame->bHasWorldSnapshot = true;
	Save->PendingRestoreSaveGame->bHasPlayerSnapshot = true;
	FCatSavedPlayerRunState& Saved = Save->PendingRestoreSaveGame->PlayerSnapshot;
	Saved.CharacterTransform = FTransform(FRotator(0, 30, 0), FVector(400, 100, 20));
	Saved.InventorySlots.SetNum(2);
	Saved.InventorySlots[0].DefinitionId = TEXT("BugBait");
	Saved.InventorySlots[0].ItemInstanceId = FGuid::NewGuid();
	Saved.InventorySlots[0].Quantity = 2;
	const FGuid BaitId = Saved.InventorySlots[0].ItemInstanceId;
	Saved.EquipmentSnapshot.BaitDefinitionId = TEXT("BugBait");
	Saved.EquipmentSnapshot.BaitItemInstanceId = BaitId;
	FText Failure;
	const int32 FishSlotIndex = Saved.InventorySlots.IndexOfByPredicate([](const auto& Slot) { return Slot.DefinitionId.IsNone(); });
	if (!TestTrue(TEXT("old backpack has a spare slot"), FishSlotIndex != INDEX_NONE)) return false;
	auto& FishSlot = Saved.InventorySlots[FishSlotIndex];
	FishSlot.DefinitionId = TEXT("LittleSilverFish");
	FishSlot.ItemInstanceId = FGuid::NewGuid();
	FishSlot.Quantity = 1;
	FishSlot.FishSessionId = FGuid::NewGuid();
	FishSlot.FishOwnerStableNetId = TEXT("LegacyFixture");
	FishSlot.FishWeightKilograms = 1.0;
	const FGuid FishId = FishSlot.ItemInstanceId;
	Save->PendingRestoreSaveGame->SlotId = FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Save->PendingRestoreSaveGame->DisplayName = TEXT("Legacy backpack fish");
	Save->ActiveSlotId = Save->PendingRestoreSaveGame->SlotId;
	Save->PendingRestoreSaveGame->FormatVersion = 6;
	auto* Version = FindFProperty<FIntProperty>(ULocalPlayerSaveGame::StaticClass(), TEXT("SavedDataVersion"));
	if (!Version) return false;
	Version->SetPropertyValue_InContainer(Save->PendingRestoreSaveGame, 6);
	const FString SlotName = TEXT("CatBlockerLegacy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	struct FCleanup { FString Slot; ~FCleanup() { UGameplayStatics::DeleteGameInSlot(Slot, 0); } } Cleanup{SlotName};
	if (!UGameplayStatics::SaveGameToSlot(Save->PendingRestoreSaveGame, SlotName, 0)) return false;
	Save->PendingRestoreSaveGame = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save->PendingRestoreSaveGame) return false;
	Save->PendingRestoreSaveGame->HandlePostLoad();
	TestEqual(TEXT("v6 schema migrates to v7"), Save->PendingRestoreSaveGame->FormatVersion, 7);
	TestEqual(TEXT("engine version migrates as well"), Save->PendingRestoreSaveGame->GetSavedDataVersion(), 7);
	const auto* OriginalDisk = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	TestTrue(TEXT("migration never rewrites source disk"), OriginalDisk && OriginalDisk->FormatVersion == 6 && OriginalDisk->GetSavedDataVersion() == 6);
	World->SpawnActor<ACatCampInventoryActor>();
	Save->bWorldRestoreApplied = false;
	Save->bLoadedRunForTravel = true;
	if (!TestTrue(TEXT("migrated old save passes coordinator validation"), Save->ValidateLoadedRunSaveGame(*Save->PendingRestoreSaveGame, Save->ActiveSlotId, Failure))) return false;
	if (!TestTrue(TEXT("old single-camp save starts the real game mode"), Scene.World.BeginPlayInTestWorld())
		|| !TestTrue(TEXT("old world restored before player"), Save->bWorldRestoreApplied)) return false;
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("LegacyFishConsumer"), TEXT("TEST"));
	Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = Player;
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Cat);
	Scene.Step(5);
	auto* Equipment = Cat->GetEquipmentComponent();
	auto* Physical = Cat->GetPhysicalBodyComponent();
	const uint32 PreviousEpoch = Physical->GetResetEpoch();
	if (!TestTrue(TEXT("GameMode使用的恢复入口成功"), Save->RestorePlayerAfterSpawn(*Controller, *Cat))) return false;
	TestTrue(TEXT("恢复推进身体失效域"), Physical->GetResetEpoch() > PreviousEpoch);
	TestTrue(TEXT("恢复指定身体Transform"), Physical->GetBody()->GetComponentTransform().Equals(Save->PendingRestoreSaveGame->PlayerSnapshot.CharacterTransform, .01));
	for (const bool bLeft : {true, false})
		TestTrue(TEXT("恢复同时移动两只独立手刚体"), Physical->GetHand(bLeft)->GetComponentLocation().Equals(
			Save->PendingRestoreSaveGame->PlayerSnapshot.CharacterTransform.TransformPosition(UCatPhysicsGrabComponent::RestHandLocal(bLeft)), .01));
	const TArray<FCatInventoryEntry> After = CatFishingTest::Entries(Equipment);
	TestEqual(TEXT("other item identity survives"), CatFishingTest::InstanceId(After[0]), BaitId);
	TestEqual(TEXT("other item quantity survives"), After[0].StackCount, 2);
	TestEqual(TEXT("legacy fish quantity preserved in original slot"), After[FishSlotIndex].StackCount, 1);
	TestEqual(TEXT("legacy fish identity preserved"), Cat->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(FishId), FishSlotIndex);
	auto* Fish = NewObject<UCatFishInventoryItemInstance>(Cat);
	Fish->SetItemDefinition(GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(TEXT("LittleSilverFish")));
	if (!Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("LegacyFixture"), 1.0)) return false;
	TestTrue(TEXT("local backpack intake continues accepting fish"), Cat->GetInventoryComponent()->AddItemInstance(Fish, 1));
	AddInfo(FString::Printf(TEXT("Event=blocker_legacy_backpack_verified FishDefinitionId=LittleSilverFish ItemInstanceId=%s SlotIndex=%d Result=PlayerRestoredFishPreserved"), *FishId.ToString(), FishSlotIndex));
	const FTransform Later(FVector(700, 100, 20));
	if (!Physical->TeleportBodyFromAuthority(Later, TEXT("AfterRestoreMovement"))) return false;
	const uint32 RestoredEpoch = Physical->GetResetEpoch();
	TestTrue(TEXT("重复恢复请求幂等成功"), Save->RestorePlayerAfterSpawn(*Controller, *Cat));
	TestEqual(TEXT("重复恢复不重置身体"), Physical->GetResetEpoch(), RestoredEpoch);
	TestTrue(TEXT("重复恢复不覆盖玩家新位置"), Cat->GetActorTransform().Equals(Later, .01));
	Save->PendingRestoreSaveGame = nullptr;
	Save->bWorldRestoreApplied = false;
	Save->bLocalPlayerRestoredInCurrentWorld = false;
	return !HasAnyErrors();
}

#endif
