#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Animation/AnimInstance.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace CatLocomotionNetwork
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
			WorldHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues)
			{
				if (World && World->WorldType == EWorldType::PIE) World->bIsNameStableForNetworking = true;
			});
			ModeHandle = FGameModeEvents::OnGameModeInitializedEvent().AddLambda([](AGameModeBase* GameMode)
			{
				if (!GameMode || !GameMode->GetWorld() || GameMode->GetWorld()->WorldType != EWorldType::PIE
					|| GameMode->GetClass() != AGameModeBase::StaticClass()) return;
				GameMode->DefaultPawnClass = LoadClass<ACatCharacter>(nullptr,
					TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
				GameMode->PlayerControllerClass = APlayerController::StaticClass();
				GameMode->PlayerStateClass = APlayerState::StaticClass();
			});
		}
		~FRestore() override { Restore(); }
		bool Update() override
		{
			if (GEditor && GEditor->PlayWorld) return false;
			Restore();
			return true;
		}
	private:
		void Restore()
		{
			if (bRestored) return;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			if (GEngine) GEngine->NetDriverDefinitions = Drivers;
			FWorldDelegates::OnPreWorldInitialization.Remove(WorldHandle);
			FGameModeEvents::OnGameModeInitializedEvent().Remove(ModeHandle);
			bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true, bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle WorldHandle, ModeHandle;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Locomotion network timed out Stage=%d ServerFrames=%d ClientFrames=%d"), Stage, ServerFrames, ClientFrames));
				return true;
			}
			UWorld* Server = nullptr;
			UWorld* Client = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
			}
			if (!Server || !Client) return false;
			APlayerController* Local = Client->GetFirstPlayerController();
			auto* ClientCat = Local ? Cast<ACatCharacter>(Local->GetPawn()) : nullptr;
			if (!ClientCat || !Local->PlayerState) return false;
			ACatCharacter* ServerCat = nullptr;
			for (TActorIterator<ACatCharacter> It(Server); It; ++It)
				if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Local->PlayerState->GetPlayerId()) ServerCat = *It;
			if (!ServerCat) return false;
			auto* ServerBody = ServerCat->GetPhysicalBodyComponent();
			auto* ClientBody = ClientCat->GetPhysicalBodyComponent();
			auto* ServerVisual = ServerCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			auto* ClientVisual = ClientCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			if (!ServerVisual || !ClientVisual || !ServerVisual->GetVisualMesh() || !ClientVisual->GetVisualMesh()) return false;
			const double Now = Server->GetTimeSeconds();
			if (Stage == 0)
			{
				Local->SetActorTickEnabled(false);
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
					if (It->Get()) It->Get()->SetActorTickEnabled(false);
				ServerBody->SetMovementSpeed(100.0);
				for (TActorIterator<ACatCharacter> It(Server); It; ++It)
					if (*It != ServerCat) It->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(
						FTransform(FRotator::ZeroRotator,FVector(-300,300,It->GetPhysicalBodyComponent()->GetStandRootHeightCm())),TEXT("LocomotionNetworkClearLane"));
				ServerBody->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, FVector(0,-150,ServerBody->GetStandRootHeightCm())), TEXT("LocomotionNetworkSetup"));
				if (FApp::CanEverRender())
				{
					Camera = Client->SpawnActor<ACameraActor>();
					if (!Test->TestNotNull(TEXT("foot-placement evidence has a client camera"), Camera.Get())) return true;
					Camera->GetCameraComponent()->SetFieldOfView(50.0f);
					Local->SetViewTarget(Camera.Get());
				}
				Stage = 1;
				StageStarted = Now;
			}
			if (Camera.IsValid())
			{
				const double Scale = ClientVisual->GetVisualMesh()->GetComponentScale().GetAbsMax();
				const FVector LookAt = ClientCat->GetActorLocation() + FVector(0,0,8.0*Scale - ClientBody->GetStandRootHeightCm());
				const FVector Position = LookAt + FVector(-45,-95,26) * Scale;
				Camera->SetActorLocationAndRotation(Position,(LookAt-Position).Rotation());
			}
			if (ClientBody->GetResetEpoch() != ServerBody->GetResetEpoch() || ClientBody->GetControlEpoch() != ServerBody->GetControlEpoch()) return false;
			ClientBody->SetViewIntent(FRotator::ZeroRotator);
			ClientBody->SetMoveIntent(Stage == 2 ? FVector::ForwardVector : FVector::ZeroVector);
			if (Stage == 1)
			{
				if (Now - StageStarted < 1.0 || !ServerBody->IsGrounded() || !ClientBody->IsGrounded()
					|| ServerBody->GetVelocity().Size2D() > 2.0 || ClientBody->GetVelocity().Size2D() > 2.0
					|| FVector::Dist(ClientCat->GetActorLocation(),ServerCat->GetActorLocation()) > 2.0
					|| ServerVisual->GetLocomotionObservation().Alpha < 0.98 || ClientVisual->GetLocomotionObservation().Alpha < 0.98) return false;
				for (auto* Visual : {ServerVisual, ClientVisual})
					Test->TestTrue(TEXT("formal ABP contributes standing foot placement on each endpoint"), Visual->GetLocomotionObservation().GroundMask == 15);
				Test->TestTrue(TEXT("server retains sole physical simulation authority"), ServerBody->GetBody()->IsSimulatingPhysics());
				Test->TestFalse(TEXT("client foot IK consumes snapshots without enabling local body simulation"), ClientBody->GetBody()->IsSimulatingPhysics());
				if (Test->HasAnyErrors()) return true;
				if (FApp::CanEverRender()) Capture(Client, TEXT("FormalStandingIK"));
				Stage = 2;
				StageStarted = Now;
			}
			else if (Stage == 2)
			{
				const auto& Host = ServerVisual->GetLocomotionObservation();
				const auto& Remote = ClientVisual->GetLocomotionObservation();
				ServerFrames += Host.Mode == TEXT("Walking") && Host.Alpha > 0.95 && Host.AnimationSpeedCmS > 1.0;
				ClientFrames += Remote.Mode == TEXT("Walking") && Remote.Alpha > 0.95 && Remote.AnimationSpeedCmS > 1.0;
				const FName Ankles[] = {TEXT("RigLFLegAnkle"),TEXT("RigRFLegAnkle"),TEXT("RigLBLegAnkle"),TEXT("RigRBLegAnkle")};
				for (int32 Foot = 0; Foot < 4; ++Foot)
				{
					const FVector Solved = ClientVisual->GetVisualMesh()->GetBoneLocationByName(Ankles[Foot],EBoneSpaces::WorldSpace);
					const FVector Base = ClientVisual->GetAnimationSource()->GetBoneLocation(Ankles[Foot]);
					if ((PreviousPlanted & Remote.PlantMask & (1 << Foot)) && Now - StageStarted > 1.0 && Remote.Alpha > 0.99)
					{
						AuthoredSlide += FVector::Dist2D(Base,PreviousBase[Foot]);
						SolvedSlide += FVector::Dist2D(Solved,PreviousSolved[Foot]);
						++PlantSamples;
					}
					PreviousBase[Foot] = Base;
					PreviousSolved[Foot] = Solved;
				}
				PreviousPlanted = Remote.PlantMask;
				if (FApp::CanEverRender() && Now-StageStarted > 2.0 && !bCapturedWalking)
				{
					Capture(Client,TEXT("FormalWalkingIK"));
					bCapturedWalking = true;
				}
				if (Now - StageStarted < 5.0) return false;
				Test->TestTrue(TEXT("server evaluates calibrated stride through the formal ABP"), ServerFrames > 10);
				Test->TestTrue(TEXT("client evaluates calibrated stride through its formal ABP"), ClientFrames > 10);
				Test->TestTrue(TEXT("client supplies consecutive planted-foot measurements"), PlantSamples > 20);
				Test->TestTrue(TEXT("client foot planting reduces visible sliding compared with original animation"), AuthoredSlide > 1.0 && SolvedSlide < AuthoredSlide * 0.8);
				Test->AddInfo(FString::Printf(TEXT("Event=locomotion_network_measured BodyId=%s ServerFrames=%d ClientFrames=%d ClientPlantSamples=%d AuthoredSlideCm=%.3f SolvedSlideCm=%.3f ServerStride=%.3f ClientStride=%.3f"),
					*ServerBody->GetBodyId().ToString(),ServerFrames,ClientFrames,PlantSamples,AuthoredSlide,SolvedSlide,Host.StrideScale,Remote.StrideScale));
				Stage = 3;
				StageStarted = Now;
			}
			else if (Stage == 3 && Now-StageStarted > 1.0)
			{
				Test->TestTrue(TEXT("both endpoints return to standing correction after stop"), ServerVisual->GetLocomotionObservation().Mode == TEXT("Standing")
					&& ClientVisual->GetLocomotionObservation().Mode == TEXT("Standing"));
				return true;
			}
			return false;
		}

	private:
		void Capture(UWorld* World, const TCHAR* Label)
		{
			UGameViewportClient* Client = World->GetGameViewport();
			FViewport* Viewport = Client ? Client->Viewport : nullptr;
			if (!Test->TestNotNull(TEXT("client has an actual rendering viewport"), Viewport)) return;
			TArray<FColor> Pixels;
			if (!Test->TestTrue(TEXT("capture client foot-placement viewport"), GetViewportScreenShot(Viewport,Pixels))) return;
			const FIntPoint Size = Viewport->GetSizeXY();
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X,Size.Y,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),Png);
			const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Locomotion/Images"));
			IFileManager::Get().MakeDirectory(*Directory,true);
			const FString File = Directory/FString::Printf(TEXT("%s-%s.png"),*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),Label);
			if (Test->TestTrue(TEXT("save real client viewport image"),FFileHelper::SaveArrayToFile(Png,*File)))
				Test->AddInfo(FString::Printf(TEXT("Event=locomotion_viewport_captured File=%s"),*File));
		}
		FAutomationTestBase* Test;
		double Started, StageStarted = 0.0;
		int32 Stage = 0, ServerFrames = 0, ClientFrames = 0, PlantSamples = 0;
		double AuthoredSlide = 0.0, SolvedSlide = 0.0;
		FVector PreviousBase[4],PreviousSolved[4];
		uint8 PreviousPlanted = 0;
		bool bCapturedWalking = false;
		TWeakObjectPtr<ACameraActor> Camera;
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLocomotionNetworkTest,
	"Catfishing.Locomotion.Network.FormalBlueprintStrideOnServerAndClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatLocomotionNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle validation editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatLocomotionNetwork::FRestore>();
	UWorld* Map = nullptr;
	if (FApp::CanEverRender())
	{
		if (!TestTrue(TEXT("load the existing lit observation arena without saving changes"),
			FEditorFileUtils::LoadMap(FPaths::ProjectContentDir() / TEXT("Catfishing/Prototypes/PhysicsGrabPrototype.umap"), false, false))) return false;
		Map = GEditor->GetEditorWorldContext().World();
	}
	else
	{
		Map = FAutomationEditorCommonUtils::CreateNewMap();
	}
	if (!Map) return false;
	// The saved arena supplies lights; its prototype GameMode normally builds the floor.
	// This test uses the formal cat's base GameMode, so give both render modes the same real ground.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* Ground = Map->SpawnActor<AStaticMeshActor>();
	if (!Cube || !Ground) return false;
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Ground->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, -10), FVector(20, 20, 0.2)));
	Map->bIsNameStableForNetworking = true;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	UClass* FormalCatClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
	if (!TestNotNull(TEXT("formal cat supplies its pre-initialization standing height"), FormalCatClass)) return false;
	const double SpawnHeight = FormalCatClass->GetDefaultObject<ACatCharacter>()->GetDefaultHalfHeight();
	Map->SpawnActor<APlayerStart>(FVector(-150, 100, SpawnHeight), FRotator::ZeroRotator);
	Map->SpawnActor<APlayerStart>(FVector(0, -150, SpawnHeight), FRotator::ZeroRotator);
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (auto& Driver : GEngine->NetDriverDefinitions)
		if (Driver.DefName == TEXT("GameNetDriver"))
			Driver.DriverClassName = Driver.DriverClassNameFallback = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatLocomotionNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
