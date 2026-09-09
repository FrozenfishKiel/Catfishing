#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"

namespace CatStarterScoopNetwork
{
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode);
			Settings->GetPlayNumberOfClients(Count);
			Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			Settings->SaveConfig(); // EndPIE 会保存临时参数，这里恢复原偏好。
			GEngine->NetDriverDefinitions = Drivers;
			return true;
		}
	private:
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		TArray<FNetDriverDefinition> Drivers;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45)
			{
				Test->AddError(FString::Printf(TEXT("Starter scoop network timeout Stage=%d"), Stage));
				return true;
			}
			UWorld* Server = nullptr;
			TArray<UWorld*> Clients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Clients.Add(Context.World());
			}
			if (!Server || Clients.Num() != 2) return false;
			TArray<APlayerController*> Controllers;
			for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
				if (It->Get() && It->Get()->PlayerState) Controllers.Add(It->Get());
			if (Controllers.Num() != 3) return false;
			if (Stage == 0)
			{
				for (APlayerController* Controller : Controllers)
					if (!ReplacePawn(Controller)) return true;
				Stage = 1;
			}
			// 对两台客户端检查全部三人的复制快照，既包含自己的 autonomous proxy，也包含其他玩家。
			for (UWorld* Client : Clients)
			{
				APlayerController* LocalController = Client->GetFirstPlayerController();
				ACatCharacter* LocalCharacter = LocalController ? Cast<ACatCharacter>(LocalController->GetPawn()) : nullptr;
				if (!LocalCharacter) return false;
				int32 Matched = 0;
				for (TActorIterator<ACatCharacter> It(Client); It; ++It)
				{
					if (!It->GetPlayerState()) continue;
					const FGuid* Expected = Instances.Find(It->GetPlayerState()->GetPlayerId());
					const auto& Snapshot = It->GetEquipmentComponent()->GetSnapshot();
					if (!Expected || Snapshot.ScoopNetItemInstanceId != *Expected) continue;
					int32 Quantity = 0;
					for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
						if (Slot.DefinitionId == TEXT("StarterScoopNet")) Quantity += Slot.Quantity;
					if (!Test->TestEqual(TEXT("客户端每个玩家只有一把抄网"), Quantity, 1)) return true;
					double Reach = 0;
					if (!Test->TestTrue(TEXT("客户端生产范围读取可用"), UCatFishingAimLibrary::TryResolveScoopReach(It->GetEquipmentComponent(), Reach))) return true;
					Test->TestEqual(TEXT("双端有效射程一致"), Reach, 200.0);
					++Matched;
				}
				if (Matched != 3) return false;
				UCatEquipmentComponent* Equipment = LocalCharacter->GetEquipmentComponent();
				const int64 Revision = Equipment->GetSnapshot().Revision;
				Equipment->GrantStarterScoopNetIfConfigured();
				Test->TestEqual(TEXT("客户端不能自行发网或推进版本"), Equipment->GetSnapshot().Revision, Revision);
			}
			if (Stage == 1)
			{
				Test->AddInfo(TEXT("Event=starter_scoop_network_verified Players=3 Clients=2 Result=AllInitialSnapshotsAgree"));
				for (APlayerController* Controller : Controllers)
				{
					if (Controller->IsLocalController()) continue;
					if (!ReplacePawn(Controller)) return true;
					Stage = 2;
					return false;
				}
			}
			Test->AddInfo(TEXT("Event=starter_scoop_network_verified Players=3 Clients=2 Result=RemoteRespawnSnapshotAgrees"));
			return true;
		}
	private:
		bool ReplacePawn(APlayerController* Controller)
		{
			APawn* OldPawn = Controller->GetPawn();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACatCharacter* Character = Controller->GetWorld()->SpawnActor<ACatCharacter>(
				FVector(0, 0, 200), FRotator::ZeroRotator, Params);
			if (!Test->TestNotNull(TEXT("服务器创建新角色"), Character)) return false;
			Character->bAlwaysRelevant = true;
			Character->GetCharacterMovement()->DisableMovement();
			Controller->Possess(Character); // 测试走正式占有入口，不手动调用发放函数。
			if (OldPawn) OldPawn->Destroy();
			const auto Snapshot = Character->GetEquipmentComponent()->GetSnapshot();
			if (!Test->TestTrue(TEXT("玩家占有后立即选中抄网"), Snapshot.ScoopNetItemInstanceId.IsValid())) return false;
			for (const auto& Entry : Instances)
				if (!Test->TestNotEqual(TEXT("新角色与其他实例相互独立"), Snapshot.ScoopNetItemInstanceId, Entry.Value)) return false;
			Instances.Add(Controller->PlayerState->GetPlayerId(), Snapshot.ScoopNetItemInstanceId);
			Character->ForceNetUpdate();
			return true;
		}
		FAutomationTestBase* Test;
		double Started;
		int32 Stage = 0;
		TMap<int32, FGuid> Instances;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatStarterScoopNetworkTest,
	"Catfishing.Editor.Equipment.StarterScoop.ListenServerTwoClientsAndRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatStarterScoopNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("需要空闲编辑器"), GEditor && !GEditor->PlayWorld)) return false;
	if (!TestTrue(TEXT("正式配置启用临时测试发网"), GetDefault<UCatEquipmentSettings>()->bAutoGrantStarterScoopNet)) return false;
	const auto Restore = MakeShared<CatStarterScoopNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("创建未保存的隔离测试地图"), Map)) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(3);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName != TEXT("GameNetDriver")) continue;
		Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
		Driver.DriverClassNameFallback = Driver.DriverClassName;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatStarterScoopNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
