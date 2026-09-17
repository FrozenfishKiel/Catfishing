#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"

namespace CatRodEscapeNetwork
{
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* S = GetDefault<ULevelEditorPlaySettings>();
			S->GetPlayNetMode(Mode); S->GetPlayNumberOfClients(Count); S->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
			S->SetPlayNetMode(Mode); S->SetPlayNumberOfClients(Count); S->SetRunUnderOneProcess(OneProcess);
			GEngine->NetDriverDefinitions = Drivers;
			return true;
		}
	private:
		EPlayNetMode Mode = PIE_Standalone; int32 Count = 1; bool OneProcess = true;
		TArray<FNetDriverDefinition> Drivers;
	};
	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45) { Test->AddError(FString::Printf(TEXT("Rod escape network timeout stage=%d"), Stage)); return true; }
			UWorld* Server = nullptr; UWorld* Client = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
				if (Context.WorldType == EWorldType::PIE && Context.World())
				{
					if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
					if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
				}
			if (!Server || !Client || !Server->GetFirstPlayerController() || !Server->GetFirstPlayerController()->PlayerState) return false;
			if (Stage == 0)
			{
				auto* Ground = Server->SpawnActor<AActor>();
				auto* Box = NewObject<UBoxComponent>(Ground);
				Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
				Box->InitBoxExtent(FVector(2000, 2000, 10)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
				Ground->SetActorLocation(FVector(0, 0, -10));
				UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
				UClass* HookClass = LoadClass<ACatFishingHookActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingHookActor.BP_CatFishingHookActor_C"));
				if (!Test->TestNotNull(TEXT("formal rod"), RodClass) || !Test->TestNotNull(TEXT("formal hook"), HookClass)) return true;
				const FTransform Pose(FVector(0, 0, 2));
				Rod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, Pose);
				Rod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(161.52, -1.30, 151.89)), FTransform::Identity, FTransform::Identity);
				Rod->InitializeAuthoritativeIdentity(RodId, FGuid::NewGuid(), 37, NAME_None, Server->GetFirstPlayerController()->PlayerState, nullptr, true, false);
				Rod->bAlwaysRelevant = true; Rod->FinishSpawning(Pose);
				Hook = Server->SpawnActorDeferred<ACatFishingHookActor>(HookClass, FTransform(FVector(1200, 0, 0)), Rod.Get());
				Hook->InitializeAuthoritativeIdentity(SessionId, FGuid::NewGuid()); Hook->bAlwaysRelevant = true;
				Hook->FinishSpawning(FTransform(FVector(1200, 0, 0)));
				Hook->FinalizeAuthoritativeLandingOnce(true, FVector(1200, 0, 0));
				Stage = 1;
			}
			ACatFishingRodActor* Remote = nullptr;
			ACatFishingHookActor* RemoteHook = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It) if (It->GetPresentationState().RodActorId == RodId) Remote = *It;
			for (TActorIterator<ACatFishingHookActor> It(Client); It; ++It) if (It->GetPresentationState().FishingSessionId == SessionId) RemoteHook = *It;
			if (!Remote) return false;
			if (Stage == 1 && RemoteHook)
			{
				Test->TestFalse(TEXT("client cannot start escape"), Remote->GetPhysicalRodComponent()->BeginBiteTimeoutEscape(SessionId, FVector(1200, 0, 0), RemoteHook));
				if (!Test->TestTrue(TEXT("server starts escape"), Rod->GetPhysicalRodComponent()->BeginBiteTimeoutEscape(SessionId, FVector(1200, 0, 0), Hook.Get()))) return true;
				Stage = 2;
			}
			if (Stage == 2)
			{
				bSawDrag |= Remote->GetPresentationState().EscapePhase == ECatFishingRodEscapePhase::Dragging;
				if (Remote->GetPresentationState().EscapePhase != ECatFishingRodEscapePhase::Stopped || RemoteHook
					|| !Remote->GetActorLocation().Equals(Rod->GetActorLocation(), 2.0)) return false;
				Test->TestTrue(TEXT("remote observes drag phase"), bSawDrag);
				Test->TestEqual(TEXT("same escape correlation"), Remote->GetPresentationState().EscapeSessionId, SessionId);
				Test->TestEqual(TEXT("remote remains dropped"), Remote->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Dropped);
				Test->TestTrue(TEXT("remote has pickup prompt"), Remote->GetInteractionPrompt_Implementation().EqualTo(FText::FromString(TEXT("拿起鱼竿"))));
				Test->TestFalse(TEXT("remote does not simulate a competing body"), Remote->GetPhysicalRodBody()->IsSimulatingPhysics());
				Test->AddInfo(FString::Printf(TEXT("Event=rod_escape_network_verified SessionId=%s RodActorId=%s Host=%s Client=%s DragObserved=%d HookRemoved=1"),
					*SessionId.ToString(), *RodId.ToString(), *Rod->GetActorLocation().ToCompactString(), *Remote->GetActorLocation().ToCompactString(), bSawDrag));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test; double Started; int32 Stage = 0; bool bSawDrag = false;
		FGuid RodId = FGuid::NewGuid(), SessionId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishingRodActor> Rod;
		TWeakObjectPtr<ACatFishingHookActor> Hook;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodEscapeNetworkTest, "Catfishing.Editor.Fishing.RodEscapeListenClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatRodEscapeNetworkTest::RunTest(const FString& Parameters)
{
	if (!GEditor || GEditor->PlayWorld) return false;
	const auto Restore = MakeShared<CatRodEscapeNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!Map) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
	S->SetPlayNetMode(PIE_ListenServer); S->SetPlayNumberOfClients(2); S->SetRunUnderOneProcess(true);
	for (auto& Driver : GEngine->NetDriverDefinitions) if (Driver.DefName == TEXT("GameNetDriver"))
	{
		Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatRodEscapeNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
