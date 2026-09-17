#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Environment/CatFishGatheringActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace CatGatheringNetwork
{
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Count); Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
			Handle = FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues)
			{ if (World && World->WorldType == EWorldType::PIE) World->bIsNameStableForNetworking = true; });
		}
		~FRestore() override { Restore(); }
		bool Update() override { if (GEditor && GEditor->PlayWorld) return false; Restore(); return true; }
	private:
		void Restore()
		{
			if (bRestored) return;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Count); Settings->SetRunUnderOneProcess(OneProcess);
			if (GEngine) GEngine->NetDriverDefinitions = Drivers;
			FWorldDelegates::OnPreWorldInitialization.Remove(Handle); bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true, bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle Handle;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45)
			{ Test->AddError(FString::Printf(TEXT("gathering replication timeout stage=%d"), Stage)); return true; }
			UWorld* Server = nullptr; UWorld* Client = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
			}
			if (!Server || !Client || !Server->HasBegunPlay() || !Client->HasBegunPlay()) return false;
			if (Stage == 0)
			{
				FCatFishGatheringState State;
				State.EventId = EventId = FGuid::NewGuid(); State.RequestId = FGuid::NewGuid();
				State.WaterRegion.RegionId = TEXT("NetworkLake"); State.WaterRegion.GeometryRevision = 1;
				State.Center = FVector(200,300,20); State.RadiusCentimeters = 850;
				State.StartedServerTime = Server->GetTimeSeconds(); State.EndsServerTime = State.StartedServerTime + 4;
				State.BiteSpeedMultiplier = 2.5;
				auto* Actor = Server->SpawnActor<ACatFishGatheringActor>();
				if (!Test->TestTrue(TEXT("authority creates initialized event"), Actor && Actor->InitializeFromAuthority(State))) return true;
				Stage = 1;
			}
			if (Stage == 1)
			{
				for (TActorIterator<ACatFishGatheringActor> It(Client); It; ++It)
				{
					const auto& State = It->GetPublicState();
					if (State.EventId != EventId) continue;
					Test->TestFalse(TEXT("client does not own gameplay authority"), It->HasAuthority());
					Test->TestEqual(TEXT("client gets exact event radius"), State.RadiusCentimeters, 850.0);
					Test->TestEqual(TEXT("client gets exact speed"), State.BiteSpeedMultiplier, 2.5);
					Test->TestEqual(TEXT("client gets exact duration"), State.EndsServerTime - State.StartedServerTime, 4.0);
					Test->TestEqual(TEXT("OnRep positions client presentation"), It->GetActorLocation(), FVector(200,300,20));
					TArray<UInstancedStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
					Test->TestTrue(TEXT("OnRep creates client range and fish shadows"), Meshes.Num()==2 && Meshes[0]->GetInstanceCount()>0 && Meshes[1]->GetInstanceCount()>0);
					Stage = 2;
				}
			}
			if (Stage == 2)
			{
				for (TActorIterator<ACatFishGatheringActor> It(Server); It; ++It) if (IsValid(*It)) return false;
				for (TActorIterator<ACatFishGatheringActor> It(Client); It; ++It) if (IsValid(*It)) return false;
				Test->AddInfo(FString::Printf(TEXT("Event=fish_gathering_network_verified EventId=%s Server=Listen Clients=1 Creation=Replicated Presentation=Native Expiration=Replicated Evidence=runtime_behavior"), *EventId.ToString()));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		int32 Stage = 0;
		FGuid EventId;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGatheringNetworkTest,
	"Catfishing.Editor.Environment.Gathering.ListenClientReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishGatheringNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatGatheringNetwork::FRestore>();
	auto* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!Map) return false;
	Map->bIsNameStableForNetworking = true;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	for (auto& Driver : GEngine->NetDriverDefinitions)
		if (Driver.DefName == TEXT("GameNetDriver")) Driver.DriverClassName = Driver.DriverClassNameFallback = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatGatheringNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
