#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Presentation/CatRodBendComponent.h"
#include "Fishing/Presentation/CatFishingLineCurveComponent.h"

namespace CatRodBendNetwork
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
			Settings->SaveConfig();
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
				Test->AddError(FString::Printf(TEXT("RodBend network timeout at stage %d"), Stage));
				return true;
			}
			UWorld* Server = nullptr;
			UWorld* Client = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
			}
			if (!Server || !Client || !Server->GetFirstPlayerController() || !Server->GetFirstPlayerController()->PlayerState) return false;
			if (Stage == 0)
			{
				UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
				UClass* HookClass = LoadClass<ACatFishingHookActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingHookActor.BP_CatFishingHookActor_C"));
				if (!Test->TestNotNull(TEXT("formal rod class"), RodClass) || !Test->TestNotNull(TEXT("formal hook class"), HookClass)) return true;
				const FTransform Transform(FVector(0, 0, 200));
				ServerRod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, Transform);
				ServerRod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(161.52, -1.30, 151.89)), FTransform::Identity, FTransform::Identity);
				ServerRod->InitializeAuthoritativeIdentity(RodId, FGuid::NewGuid(), TEXT("Rod_Basic"), NAME_None,
					Server->GetFirstPlayerController()->PlayerState, nullptr, true, false);
				ServerRod->bAlwaysRelevant = true;
				ServerRod->FinishSpawning(Transform);
				UCatRodBendComponent* Bend = ServerRod->FindComponentByClass<UCatRodBendComponent>();
				if (!Test->TestTrue(TEXT("server builds existing mesh"), Bend && Bend->IsVisualReady())) return true;
				ServerHook = Server->SpawnActorDeferred<ACatFishingHookActor>(HookClass, Transform, ServerRod.Get());
				ServerHook->InitializeAuthoritativeIdentity(SessionId, FGuid::NewGuid());
				ServerHook->bAlwaysRelevant = true;
				ServerHook->FinishSpawning(Transform);
				ServerHook->FinalizeAuthoritativeLandingOnce(true, Bend->GetRestTipWorld() + FVector(0, 700, 0));
				ServerHook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, 100);
				ServerRod->ForceNetUpdate();
				Stage = 1;
			}
			ACatFishingRodActor* ClientRod = nullptr;
			ACatFishingHookActor* ClientHook = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
				if (It->GetPresentationState().RodActorId == RodId) ClientRod = *It;
			for (TActorIterator<ACatFishingHookActor> It(Client); It; ++It)
				if (It->GetPresentationState().FishingSessionId == SessionId) ClientHook = *It;
			if (!ClientRod) return false;
			UCatRodBendComponent* ClientBend = ClientRod->FindComponentByClass<UCatRodBendComponent>();
			UCatRodBendComponent* ServerBend = ServerRod->FindComponentByClass<UCatRodBendComponent>();
			if (!ClientBend || !ClientBend->IsVisualReady()) return false;
			if (Stage == 1 && ClientHook && ClientHook->GetOwner() == ClientRod
				&& ClientHook->GetPresentationState().LineTensionNewtons == 100 && ClientBend->GetBendRadians().Size() > 0.86)
			{
				Test->TestTrue(TEXT("host and remote derive the same loaded bend"), ClientBend->GetBendRadians().Equals(ServerBend->GetBendRadians(), 0.02));
				Test->TestFalse(TEXT("remote client cannot publish force"), ClientHook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, 500));
				const UCatFishingLineCurveComponent* Line = ClientHook->FindComponentByClass<UCatFishingLineCurveComponent>();
				Test->TestTrue(TEXT("remote line has visible geometry"), Line && Line->GetCurveWorldPoints().Num() > 1);
				Test->AddInfo(FString::Printf(TEXT("Event=rod_bend_network_loaded RodActorId=%s SessionId=%s ServerNetMode=%d ClientNetMode=%d ServerBend=%s ClientBend=%s RawTensionN=100"),
					*RodId.ToString(), *SessionId.ToString(), Server->GetNetMode(), Client->GetNetMode(), *ServerBend->GetBendRadians().ToString(), *ClientBend->GetBendRadians().ToString()));
				ServerHook->SetFishingLinePresentationFromAuthority(800, 700, 100, 0, false, 0);
				Stage = 2;
			}
			else if (Stage == 2 && ClientHook && ClientHook->GetPresentationState().LineTensionNewtons == 0 && ClientBend->GetBendRadians().Size() < 0.001)
			{
				Test->AddInfo(TEXT("Event=rod_bend_network_slack Result=RemoteReleased"));
				ServerHook->SetFishingLinePresentationFromAuthority(700, 700, 0, 1, true, 100);
				Stage = 3;
			}
			else if (Stage == 3 && ClientBend->GetBendRadians().Size() > 0.86)
			{
				ServerHook->Destroy();
				Stage = 4;
			}
			else if (Stage == 4 && !ClientHook && ClientBend->GetBendRadians().Size() < 0.001)
			{
				Test->AddInfo(TEXT("Event=rod_bend_network_destroyed Result=RemoteReleased"));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		int32 Stage = 0;
		FGuid RodId = FGuid::NewGuid(), SessionId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishingRodActor> ServerRod;
		TWeakObjectPtr<ACatFishingHookActor> ServerHook;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodBendNetworkTest, "Catfishing.Editor.Fishing.RodBendListenClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodBendNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatRodBendNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("isolated unsaved map"), Map)) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatRodBendNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
