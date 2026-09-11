#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Profile/CatProfileSaveGame.h"
#include "Save/CatSaveSubsystem.h"
#include "UObject/GarbageCollection.h"

namespace CatSaveRoundTrip
{
	/** 正式地图中的磁盘往返命令；只操作本用例创建的随机槽，并在结束时删除该槽。 */
	class FDiskRoundTrip final : public IAutomationLatentCommand
	{
	public:
		/** 记录断言接收者与本次唯一显示名；磁盘槽由正式创建入口分配。 */
		explicit FDiskRoundTrip(FAutomationTestBase* InTest) : Test(InTest)
		{
			DisplayName = TEXT("SaveRoundTrip_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		}

		/** 每轮等待真实异步回调，再推进创建、采集、释放、冷读、正式重启、跨 World 自动恢复、坏档拒绝和旧档只读迁移；失败或超时清理本用例槽。 */
		bool Update() override
		{
			if (StartedAt == 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Save round trip timeout Stage=%d"), Stage));
				return Cleanup();
			}
			UWorld* World = GEditor->PlayWorld;
			APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
			ACatfishingGameModeBase* GameMode = World ? Cast<ACatfishingGameModeBase>(World->GetAuthGameMode()) : nullptr;
			if (!Character || !GameMode || !Controller->GetLocalPlayer()) return false;
			UCatSaveSubsystem* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
			if (!Save) return false;
			if (Stage == 0)
			{
				Save->RefreshSlotSummaries();
				Stage = 1;
				return false;
			}
			if (Save->IsBusy()) return false;
			FText Failure;
			if (Stage == 1)
			{
				if (!Test->TestTrue(TEXT("create accepted"), Save->RequestCreateSlot(DisplayName).bAccepted)) return Cleanup();
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				for (const FCatSaveSlotSummary& Summary : Save->GetSlotSummaries())
				{
					if (Summary.DisplayName == DisplayName) SlotId = Summary.SlotId;
				}
				if (!Test->TestFalse(TEXT("new slot exists"), SlotId.IsNone())
					|| !Test->TestTrue(TEXT("load accepted"), Save->RequestLoadSlot(SlotId).bAccepted)) return Cleanup();
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				if (!Test->TestTrue(TEXT("new world permit"), Save->HasLoadedRunForTravel())
					|| !Test->TestTrue(TEXT("new world ready"), Save->RestoreWorldAfterHostsReady(*GameMode))) return Cleanup();
				UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
				const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
				UCatInventoryItemDefinition* Bait = Settings->FindRuntimeDefinition(TEXT("BugBait"));
				UCatEquipmentDefinition* Rod = nullptr;
				for (const FCatInventoryCatalogDefinition& Entry : Settings->Definitions)
				{
					UCatEquipmentDefinition* Candidate = Cast<UCatEquipmentDefinition>(Entry.ItemDefinition.LoadSynchronous());
					if (Candidate && Candidate->CanServeFishingRod()) { Rod = Candidate; break; }
				}
				UCatFishDefinition* FishDefinition = nullptr;
				for (const TSoftObjectPtr<UCatFishDefinition>& Entry : GetDefault<UCatFishCatalogSettings>()->Definitions)
				{
					UCatFishDefinition* Candidate = Entry.LoadSynchronous();
					if (Candidate && Candidate->IsInventoryRuntimeDefinitionReady()) { FishDefinition = Candidate; break; }
				}
				if (!Test->TestNotNull(TEXT("formal bait"), Bait) || !Test->TestNotNull(TEXT("formal rod"), Rod)
					|| !Test->TestNotNull(TEXT("formal fish"), FishDefinition)) return Cleanup();
				TArray<FCatInventoryEntry> Entries;
				Entries.SetNum(4);
				UCatInventoryItemInstance* BaitInstance = NewObject<UCatInventoryItemInstance>(Character,
					UCatInventoryItemDefinition::ResolveItemInstanceClass(Bait));
				BaitInstance->SetRuntimeOwnerActor(Character);
				BaitInstance->SetItemDefinition(Bait);
				Entries[0].Instance = BaitInstance;
				Entries[0].StackCount = 3;
				UCatEquipmentInventoryItemInstance* RodInstance = NewObject<UCatEquipmentInventoryItemInstance>(Character);
				RodInstance->SetRuntimeOwnerActor(Character);
				RodInstance->SetItemDefinition(Rod);
				RodInstance->SetRodRuntimeStateFromAuthority(0.0, true);
				Entries[2].Instance = RodInstance;
				Entries[2].StackCount = 1;
				UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(Character);
				Fish->SetRuntimeOwnerActor(Character);
				Fish->SetItemDefinition(FishDefinition);
				FishSessionId = FGuid::NewGuid();
				FishId = FGuid::NewGuid();
				if (!Test->TestTrue(TEXT("fish initialized"), Fish->InitializeFishFromAuthority(FishSessionId, FishId,
					TEXT("RoundTripOwner"), 2.75))) return Cleanup();
				Entries[3].Instance = Fish;
				Entries[3].StackCount = 1;
				BaitId = BaitInstance->GetItemInstanceId();
				RodId = RodInstance->GetItemInstanceId();
				if (!Test->TestTrue(TEXT("clear equipment selection"), Character->GetEquipmentComponent()->RestoreSnapshotFromAuthority({}, Failure))
					|| !Test->TestTrue(TEXT("seed player inventory"), Inventory->RestoreInventorySlotsFromAuthority(Entries,
						Settings->GetPlayerInventorySlotCapacity(), Failure))) return Cleanup();
				SavedTransform = Character->GetActorTransform();
				SavedTransform.AddToTranslation(FVector(210.0, 130.0, 10.0));
				Character->SetActorTransform(SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
				ACatCampInventoryActor* Camp = nullptr;
				for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It) { Camp = *It; break; }
				if (!Test->TestNotNull(TEXT("formal camp inventory"), Camp)) return Cleanup();
				UCatFishInventoryItemInstance* CampFish = NewObject<UCatFishInventoryItemInstance>(Camp);
				CampFish->SetRuntimeOwnerActor(Camp);
				CampFish->SetItemDefinition(FishDefinition);
				CampFish->InitializeFishFromAuthority(FGuid::NewGuid(), FishId, TEXT("CampRoundTripOwner"), 1.25);
				TArray<FCatInventoryEntry> CampEntries;
				CampEntries.SetNum(1);
				CampEntries[0].Instance = CampFish;
				CampEntries[0].StackCount = 1;
				if (!Test->TestTrue(TEXT("seed camp with duplicated identity"), Camp->RestoreInventorySlotsFromAuthority(CampEntries, Failure))) return Cleanup();
				Test->TestFalse(TEXT("duplicate backpack and camp identity cannot overwrite disk"), Save->RequestSaveActiveRun().bAccepted);
				CampFishId = FGuid::NewGuid();
				CampFish->SetItemInstanceIdFromAuthority(CampFishId);
				const FCatSaveResult Request = Save->RequestSaveActiveRun();
				if (!Test->TestTrue(*Save->GetLastResultText().ToString(), Request.bAccepted)) return Cleanup();
				Test->TestFalse(TEXT("busy prevents releasing run before disk completion"), Save->ReleaseActiveRun());
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				if (!Test->TestTrue(TEXT("release saved object"), Save->ReleaseActiveRun())) return Cleanup();
				Character->GetInventoryComponent()->RestoreInventorySlotsFromAuthority({},
					GetDefault<UCatInventorySettings>()->GetPlayerInventorySlotCapacity(), Failure);
				Character->SetActorLocation(SavedTransform.GetLocation() + FVector(900.0, 0.0, 0.0));
				CollectGarbage(RF_NoFlags);
				if (!Test->TestTrue(TEXT("cold disk load accepted"), Save->RequestLoadSlot(SlotId).bAccepted)) return Cleanup();
				Stage = 5;
				return false;
			}
			if (Stage == 5)
			{
				if (!Test->TestTrue(TEXT("cold loaded world permit"), Save->HasLoadedRunForTravel())
					|| !Test->TestTrue(TEXT("restore world consumers"), Save->RestoreWorldAfterHostsReady(*GameMode))) return Cleanup();
				// 正式 RestartPlayer 先应用出生点再触发恢复；立即检查，排除之后重力或玩家输入对位置的影响。
				GameMode->RestartPlayer(Controller);
				Character = Cast<ACatCharacter>(Controller->GetPawn());
				if (!Test->TestNotNull(TEXT("restarted pawn"), Character)) return Cleanup();
				Test->TestTrue(TEXT("saved position survives RestartPlayer"), Character->GetActorTransform().Equals(SavedTransform, 0.1));
				const TArray<FCatInventoryEntry> Entries = Character->GetInventoryComponent()->GetInventoryEntries();
				if (!Test->TestTrue(TEXT("restored slot count"), Entries.Num() >= 4)) return Cleanup();
				Test->TestTrue(TEXT("bait identity and stack"), Entries[0].Instance && Entries[0].Instance->GetItemInstanceId() == BaitId && Entries[0].StackCount == 3);
				Test->TestTrue(TEXT("empty slot preserved"), !Entries[1].Instance && Entries[1].StackCount == 0);
				const UCatEquipmentInventoryItemInstance* Rod = Cast<UCatEquipmentInventoryItemInstance>(Entries[2].Instance);
				Test->TestTrue(TEXT("broken rod identity and state"), Rod && Rod->GetItemInstanceId() == RodId && Rod->IsRodBroken() && Rod->GetRodDurability() == 0.0);
				const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entries[3].Instance);
				Test->TestTrue(TEXT("fish identity weight and provenance"), Fish && Fish->GetItemInstanceId() == FishId
					&& Fish->GetSourceFishingSessionId() == FishSessionId && Fish->GetFishWeightKilograms() == 2.75
					&& Fish->GetFishOwnerStableNetId() == TEXT("RoundTripOwner"));
				for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
				{
					const TArray<FCatInventoryEntry> CampEntries = It->GetInventoryComponent()->GetInventoryEntries();
					const UCatFishInventoryItemInstance* CampFish = CampEntries.Num() > 0
						? Cast<UCatFishInventoryItemInstance>(CampEntries[0].Instance) : nullptr;
					Test->TestTrue(TEXT("camp fish restores its independent identity and weight"), CampFish
						&& CampFish->GetItemInstanceId() == CampFishId && CampFish->GetFishWeightKilograms() == 1.25);
				}
				Save->ReleaseActiveRun();
				Test->TestTrue(TEXT("load before real world travel"), Save->RequestLoadSlot(SlotId).bAccepted);
				Stage = 6;
				return false;
			}
			if (Stage == 6)
			{
				if (!Test->TestTrue(TEXT("travel requires loaded disk object"), Save->HasLoadedRunForTravel())) return Cleanup();
				DepartingWorld = World;
				UGameplayStatics::OpenLevel(World, TEXT("/Game/Catfishing/Maps/TestMap"));
				Stage = 7;
				return false;
			}
			if (Stage == 7)
			{
				if (World == DepartingWorld.Get()) return false;
				// 新 World 由 GameMode 自动消费存档，不手工调用恢复；重力可以改变 Z，但水平落点与实例必须一致。
				const FVector RestoredLocation = Character->GetActorLocation();
				Test->TestTrue(TEXT("new world restores saved horizontal position"),
					FVector2D(RestoredLocation.X, RestoredLocation.Y).Equals(FVector2D(SavedTransform.GetLocation()), 0.1));
				const TArray<FCatInventoryEntry> TravelEntries = Character->GetInventoryComponent()->GetInventoryEntries();
				Test->TestTrue(TEXT("new world automatically restores inventory"), TravelEntries.Num() >= 4
					&& TravelEntries[0].Instance && TravelEntries[0].Instance->GetItemInstanceId() == BaitId
					&& TravelEntries[0].StackCount == 3 && TravelEntries[3].Instance
					&& TravelEntries[3].Instance->GetItemInstanceId() == FishId);
				Save->ReleaseActiveRun();
				// 只损坏本测试自建的槽，保留目录缓存，以验证异步默认对象不会获得旅行许可。
				const TArray<uint8> BrokenBytes = { 1, 2, 3, 4 };
				Test->TestTrue(TEXT("write corrupt fixture"), FFileHelper::SaveArrayToFile(BrokenBytes,
					*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("CatRun_") + SlotId.ToString() + TEXT(".sav"))));
				Test->TestFalse(TEXT("invalid header rejected before engine deserialization"), Save->RequestLoadSlot(SlotId).bAccepted);
				// 有合法引擎头但类错误的文件会使原生 API 生成默认对象，必须由 WasLoaded 拒绝其旅行许可。
				Test->TestTrue(TEXT("write wrong class fixture"), UGameplayStatics::SaveGameToSlot(NewObject<UCatProfileSaveGame>(),
					TEXT("CatRun_") + SlotId.ToString(), Controller->GetLocalPlayer()->GetPlatformUserIndex()));
				Test->TestTrue(TEXT("wrong class read accepted for async validation"), Save->RequestLoadSlot(SlotId).bAccepted);
				Stage = 8;
				return false;
			}
			Test->TestFalse(TEXT("corrupt file cannot become new world"), Save->HasLoadedRunForTravel());
			// 旧 USaveGame 尚无引擎版本字段，默认值为 0；直接写旧 DTO 形状来验证只读迁移不改文件。
			UCatRunSaveGame* Legacy = NewObject<UCatRunSaveGame>();
			Legacy->FormatVersion = 5;
			Legacy->SlotId = SlotId;
			Legacy->DisplayName = DisplayName;
			const FString FileName = TEXT("CatRun_") + SlotId.ToString();
			const int32 UserIndex = Controller->GetLocalPlayer()->GetPlatformUserIndex();
			Test->TestTrue(TEXT("write legacy fixture"), UGameplayStatics::SaveGameToSlot(Legacy, FileName, UserIndex));
			TArray<uint8> BeforeMigration, AfterMigration;
			const FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FileName + TEXT(".sav"));
			FFileHelper::LoadFileToArray(BeforeMigration, *FilePath);
			const UCatRunSaveGame* Migrated = Cast<UCatRunSaveGame>(ULocalPlayerSaveGame::LoadOrCreateSaveGameForLocalPlayer(
				UCatRunSaveGame::StaticClass(), Controller->GetLocalPlayer(), FileName));
			Test->TestTrue(TEXT("legacy v5 migrated in memory"), Migrated && Migrated->WasLoaded()
				&& Migrated->FormatVersion == 6 && Migrated->GetSavedDataVersion() == 0);
			FFileHelper::LoadFileToArray(AfterMigration, *FilePath);
			Test->TestTrue(TEXT("legacy migration leaves disk bytes unchanged"), BeforeMigration == AfterMigration && !BeforeMigration.IsEmpty());
			Test->AddInfo(TEXT("Event=save_disk_roundtrip_verified Position=RestartPlayerAndNewWorld Inventory=StackEmptyRodFish CampFish=Restored DuplicateIdentity=Rejected BadHeader=Rejected WrongClass=Rejected LegacyV5=ReadOnlyMigration"));
			return Cleanup();
		}

	private:
		/** 清理流程：等待磁盘请求结束后释放本局，只删除本命令记录的随机槽；未创建槽时不碰磁盘。 */
		bool Cleanup()
		{
			if (UWorld* World = GEditor->PlayWorld)
			{
				UCatSaveSubsystem* Save = World->GetGameInstance()->GetSubsystem<UCatSaveSubsystem>();
				if (Save->IsBusy()) return false;
				Save->ReleaseActiveRun();
				if (!SlotId.IsNone()) UGameplayStatics::DeleteGameInSlot(TEXT("CatRun_") + SlotId.ToString(),
					World->GetGameInstance()->GetFirstGamePlayer()->GetPlatformUserIndex());
			}
			return true;
		}
		/** Automation 断言目标；框架保证其生命周期覆盖潜伏命令。 */
		FAutomationTestBase* Test;
		/** 此次自建槽的可识别显示名；只用来从创建完成目录取得随机 SlotId。 */
		FString DisplayName;
		/** 本用例唯一磁盘槽；收尾只删除这个槽。 */
		FName SlotId;
		/** 异步阶段编号；回调退出 busy 后才进入下一步。 */
		int32 Stage = 0;
		/** 首轮 PIE 轮询的墙钟秒数；磁盘或角色未就绪超时会使测试失败。 */
		double StartedAt = 0.0;
		/** 保存瞬间的权威角色 Transform；恢复后以厘米容差比较。 */
		FTransform SavedTransform;
		/** 真实旅行前的 World；仅用于等待正式 OpenLevel 创建下一张地图，不延长旧 World 生命周期。 */
		TWeakObjectPtr<UWorld> DepartingWorld;
		/** 写盘前各实例的稳定身份；释放原对象之后用来检查没有重新生成物品。 */
		FGuid BaitId, RodId, FishId, FishSessionId, CampFishId;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatSaveDiskRoundTripTest, "Catfishing.Editor.Save.LocalPlayerDiskRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试入口流程：加载正式地图并启动单机 PIE，排入真实磁盘往返和结束命令；不修改正式资产与用户存档。
bool FCatSaveDiskRoundTripTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor && !GEditor->PlayWorld)) return false;
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	EPlayNetMode PreviousNetMode;
	int32 PreviousClientCount = 1;
	Settings->GetPlayNetMode(PreviousNetMode);
	Settings->GetPlayNumberOfClients(PreviousClientCount);
	Settings->SetPlayNetMode(PIE_Standalone);
	Settings->SetPlayNumberOfClients(1);
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatSaveRoundTrip::FDiskRoundTrip>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	// PIE 完全退出后恢复共享编辑器设置，避免结束逻辑覆盖还原值或影响其他测试。
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([PreviousNetMode, PreviousClientCount]()
	{
		if (GEditor->PlayWorld) return false;
		ULevelEditorPlaySettings* RestoredSettings = GetMutableDefault<ULevelEditorPlaySettings>();
		RestoredSettings->SetPlayNetMode(PreviousNetMode);
		RestoredSettings->SetPlayNumberOfClients(PreviousClientCount);
		return true;
	}));
	return true;
}

#endif
