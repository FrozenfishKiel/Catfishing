#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Tests/AutomationEditorCommon.h"
#include "Animation/AnimInstance.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
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

struct FCatLightPropNetworkTestAccess
{
	static bool RetakeControl(ACatFishingRodActor* Rod, APlayerState* Owner)
	{
		return Rod->SetPrimaryOperatorFromAuthority(Owner, Rod->GetPresentationState().RodActorRevision);
	}
};

namespace CatLightPropNetwork
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
			if (FPlatformTime::Seconds() - Started > 80) { Test->AddError(FString::Printf(TEXT("Light prop network timeout Stage=%d"), Stage)); return true; }
			UWorld *Server = nullptr, *Client = nullptr;
			for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::PIE && C.World())
			{
				if (C.World()->GetNetMode() == NM_ListenServer) Server = C.World();
				if (C.World()->GetNetMode() == NM_Client) Client = C.World();
			}
			if (!Server || !Client) return false;
			auto* Local = Client->GetFirstPlayerController();
			auto* ClientCat = Local ? Cast<ACatCharacter>(Local->GetPawn()) : nullptr;
			if (!ClientCat || !Local->PlayerState) return false;
			ACatCharacter *Cat = nullptr, *Helper = nullptr;
			for (TActorIterator<ACatCharacter> It(Server); It; ++It)
				if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Local->PlayerState->GetPlayerId()) Cat = *It; else Helper = *It;
			if (!Cat || !Helper) return false;
			auto* Body = Cat->GetPhysicalBodyComponent();
			auto* ClientBody = ClientCat->GetPhysicalBodyComponent();
			auto* HelperBody = Helper->GetPhysicalBodyComponent();
			const double Now = Server->GetTimeSeconds();
			if (Stage == 0)
			{
				Local->SetActorTickEnabled(false);
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It) if (It->Get()) It->Get()->SetActorTickEnabled(false);
				Body->TeleportBodyFromAuthority(FTransform(FVector(0, 0, Body->GetStandRootHeightCm())), TEXT("LightPropFormalSetup"));
				HelperBody->TeleportBodyFromAuthority(FTransform(FVector(400, 0, HelperBody->GetStandRootHeightCm())), TEXT("LightPropHelperSetup"));
				if (FApp::CanEverRender())
				{
					auto* Camera = Client->SpawnActor<ACameraActor>();
					ObservationCamera = Camera;
					const FVector Position(-80, -450, 175), LookAt(80, 0, 60);
					Camera->SetActorLocationAndRotation(Position, (LookAt - Position).Rotation());
					Camera->GetCameraComponent()->SetFieldOfView(65);
					Local->SetViewTarget(Camera);
				}
				Next(1, Now);
			}
			if (ClientBody->GetControlEpoch() != Body->GetControlEpoch() || ClientBody->GetResetEpoch() != Body->GetResetEpoch()) return false;
			ClientBody->SetMoveIntent(Stage == 5 ? -FVector::ForwardVector : FVector::ZeroVector);
			ClientBody->SetViewIntent(FRotator(0, Stage == 2 ? 40 : 0, 0));
			HelperBody->SetMoveIntent(Stage == 5 ? FVector::ForwardVector : FVector::ZeroVector);
			HelperBody->SetViewIntent(Stage >= 5 && Rod ?
				(Rod->GetGripWorldTransform().GetLocation() + Rod->GetPhysicalRodBody()->GetForwardVector() * 12.0 - HelperBody->GetGrab()->GetShoulderWorldLocation(true)).Rotation()
				: FRotator(0, 180, 0));
			if (Stage == 1)
			{
				if (Now - StageStarted < 1 || !Body->IsGrounded() || !ClientBody->IsGrounded()) return false;
				const auto* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(TEXT("StarterRodT1"));
				if (!Test->TestNotNull(TEXT("formal rod definition"), Definition)) return true;
				const FTransform Pose(Cat->GetActorLocation());
				Rod = Server->SpawnActorDeferred<ACatFishingRodActor>(Definition->UseActorClass.LoadSynchronous(), Pose);
				if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(Definition->RodTipLocalTransform, Definition->StandLocalTransform, Definition->GripLocalTransform)
					|| !Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("StarterRodT1"), NAME_None, Cat->GetPlayerState(), Cat->GetPlayerState(), true, false))
				{ Test->AddError(TEXT("formal rod authority initialization failed")); return true; }
				Rod->FinishSpawning(Pose);
				if (!Test->TestTrue(TEXT("R hold receiver creates a real primary grip"), Rod->BeginPhysicalHoldFromAuthority(Cat->GetPlayerState(), true)
					&& Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Cat->GetPlayerState()))) return true;
				Next(2, Now);
			}
			if (!Rod) return false;
			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
				if (It->GetPresentationState().RodActorId == Rod->GetPresentationState().RodActorId) ClientRod = *It;
			if (!ClientRod) return false;
			auto* Light = Rod->FindComponentByClass<UCatLightPropComponent>();
			auto* ClientLight = ClientRod->FindComponentByClass<UCatLightPropComponent>();
			if (Stage >= 2 && Stage <= 4)
			{
				MinimumBodyZ = FMath::Min(MinimumBodyZ, Body->GetBody()->GetComponentLocation().Z);
				MinimumUp = FMath::Min(MinimumUp, Body->GetBody()->GetUpVector().Z);
				MaximumBodyZ = FMath::Max(MaximumBodyZ, Body->GetBody()->GetComponentLocation().Z);
				BareMinimumZ = FMath::Min(BareMinimumZ, HelperBody->GetBody()->GetComponentLocation().Z);
				BareMaximumZ = FMath::Max(BareMaximumZ, HelperBody->GetBody()->GetComponentLocation().Z);
				if (Stage != 3) SupportedMinimumZ = FMath::Min(SupportedMinimumZ, Body->GetBody()->GetComponentLocation().Z);
			}
			if (Stage == 2)
			{
				if (Now - StageStarted < 2 || ClientLight->GetState().GripCount != 1) return false;
				Test->TestEqual(TEXT("held state replicates the stable prop identity"), ClientLight->GetState().PropId, Light->GetState().PropId);
				Test->TestFalse(TEXT("client rod does not run a second physics solver"), ClientRod->GetPhysicalRodBody()->IsSimulatingPhysics());
				auto* Visual = ClientCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
				const auto* ClientGrab = ClientBody->GetGrab();
				Test->TestTrue(TEXT("client receives the primary controlled hand source"), ClientGrab->GetGripState(true).bControlledHold);
				if (Visual)
				{
					const double GripGap = FVector::Distance(Visual->GetVisualHandWorldLocation(true), ClientGrab->GetGripWorldLocation(true));
					Test->AddInfo(FString::Printf(TEXT("Event=light_prop_controlled_hand_pose NetMode=3 Controlled=1 GripGapWorldCm=%.3f Result=OriginalBoneLengths"), GripGap));
					Test->TestTrue(TEXT("controlled hand poses toward the held grip while preserving model bone lengths"), GripGap < 12);
				}
				Capture(Client, TEXT("formal-held-turn"));
				ClientBody->RequestJump();
				HelperBody->RequestJump();
				Next(3, Now);
			}
			else if (Stage == 3)
			{
				if (!bJumpCaptured && ClientCat->GetActorLocation().Z > 80) { Capture(Client, TEXT("formal-held-jump")); bJumpCaptured = true; }
				if (Now - StageStarted < 2.5 || !Body->IsGrounded() || !ClientBody->IsGrounded()) return false;
				Test->TestTrue(TEXT("holding prop preserves the configured physical jump"), MaximumBodyZ - Body->GetStandRootHeightCm() > 65);
				Rod->ReleasePhysicalPrimaryHoldFromAuthority(Cat->GetPlayerState(), TEXT("LightPropReleaseAbove"));
				Rod->RefreshPrimaryControlFromAuthority();
				// Fixture places the now-free formal shaft directly above the cat; only Chaos moves it afterward.
				Rod->GetPhysicalRodBody()->SetWorldLocation(Body->GetBody()->GetComponentLocation() + FVector(0, 0, 45), false, nullptr, ETeleportType::TeleportPhysics);
				Rod->GetPhysicalRodBody()->SetPhysicsLinearVelocity(FVector(0, 0, -80));
				Rod->GetPhysicalRodBody()->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
				Next(4, Now);
			}
			else if (Stage == 4)
			{
				if (Now - StageStarted < .4) return false;
				if (!bDropCaptured) { Capture(Client, TEXT("formal-released-over-cat")); bDropCaptured = true; }
				if (Now - StageStarted < 3 || ClientLight->GetState().GripCount != 0) return false;
				Test->AddInfo(FString::Printf(TEXT("Event=light_prop_formal_drop_measured MinZ=%.4f MinUp=%.6f CurrentZ=%.4f CurrentUp=%.6f MaximumZ=%.4f"),
					MinimumBodyZ, MinimumUp, Body->GetBody()->GetComponentLocation().Z, Body->GetBody()->GetUpVector().Z, MaximumBodyZ));
				Test->TestTrue(TEXT("holding, turning, jumping and dropping do not sink or overturn the cat"), MinimumUp > .9 && SupportedMinimumZ > Body->GetStandRootHeightCm() - 5
					&& MinimumBodyZ >= BareMinimumZ - 5 && FMath::Abs(MaximumBodyZ - BareMaximumZ) < 5);
				Test->AddInfo(FString::Printf(TEXT("Event=light_prop_jump_baseline BareMinZ=%.4f BareMaxZ=%.4f HeldMinZ=%.4f HeldMaxZ=%.4f SupportedMinZ=%.4f"), BareMinimumZ, BareMaximumZ, MinimumBodyZ, MaximumBodyZ, SupportedMinimumZ));
				Capture(Client, TEXT("formal-after-drop-standing"));
				Test->TestEqual(TEXT("no owner is automatically promoted on release"), Rod->GetOperatorCount(), 0);
				if (!Test->TestTrue(TEXT("retake uses the same physical hold receiver"), Rod->BeginPhysicalHoldFromAuthority(Cat->GetPlayerState(), true)
					&& FCatLightPropNetworkTestAccess::RetakeControl(Rod, Cat->GetPlayerState())
					&& Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Cat->GetPlayerState()))) return true;
				// The helper stands on the floor and reaches up to the handle through normal grab input.
				// Aim at exposed shaft, beyond the primary kinematic hand covering the handle.
				const FVector Point = Rod->GetGripWorldTransform().GetLocation() + Rod->GetPhysicalRodBody()->GetForwardVector() * 12.0;
				HelperBody->TeleportBodyFromAuthority(FTransform(FRotator(0, 180, 0), FVector(Point.X + 40, Point.Y - 6.8, HelperBody->GetStandRootHeightCm())), TEXT("LightPropHelperReachSetup"));
				HelperBody->GetGrab()->SetGrabInput(true, true);
				Next(8, Now);
			}
			else if (Stage == 8)
			{
				if (Now - StageStarted > 4) { Test->AddError(TEXT("grounded helper could not reach the controlled rod")); return true; }
				if (HelperBody->GetGrab()->GetGripTarget(true) != Rod) return false;
				HelperGrip = HelperBody->GetGrab()->GetGripState(true).GripId;
				HelperLocalContact = HelperBody->GetGrab()->GetGripState(true).TargetLocalPoint;
				PullStartPrimary = Body->GetBody()->GetComponentLocation();
				PullStartHelper = HelperBody->GetBody()->GetComponentLocation();
				Next(5, Now);
			}
			else if (Stage == 5)
			{
				auto* HelperGrab = HelperBody->GetGrab();
				Test->TestEqual(TEXT("helper rod grip applies traction to the primary carrier"), HelperGrab->GetTractionReceiverForDiagnostics(true), Body);
				MaximumGripForce = FMath::Max(MaximumGripForce, HelperGrab->GetLastTractionForceForDiagnostics(true).Size());
				MaximumTractionError = FMath::Max(MaximumTractionError, HelperGrab->GetTractionErrorForDiagnostics(true).Size());
				MaximumHandGap = FMath::Max(MaximumHandGap, FVector::Distance(HelperBody->GetHand(true)->GetComponentLocation(), HelperGrab->GetGripWorldLocation(true)));
				MaximumAnchorError = FMath::Max(MaximumAnchorError, FVector::Distance(HelperLocalContact, HelperGrab->GetGripState(true).TargetLocalPoint));
				if (ClientLight->GetState().GripCount == 2) bTwoGripsReplicated = true;
                if (ObservationCamera.IsValid())
                {
                    const FVector Centre = (Cat->GetActorLocation() + Helper->GetActorLocation()) * .5;
                    const FVector At = Centre + FVector(-80,-330,130);
                    ObservationCamera->SetActorLocationAndRotation(At, (Centre + FVector(0,0,20) - At).Rotation());
                }
				if (Now - StageStarted < 2.5) return false;
				Test->AddInfo(FString::Printf(TEXT("Event=light_prop_shared_traction_measured TractionErrorCm=%.3f StateAnchorErrorCm=%.3f ForceKgCmS2=%.3f HandGapCm=%.3f"), MaximumTractionError, MaximumAnchorError, MaximumGripForce, MaximumHandGap));
				Test->TestTrue(TEXT("both real grips remain during opposing movement"), Light->GetState().GripCount == 2 && bTwoGripsReplicated);
				const FVector PrimaryTravel = Body->GetBody()->GetComponentLocation() - PullStartPrimary;
				const FVector HelperTravel = HelperBody->GetBody()->GetComponentLocation() - PullStartHelper;
				Test->AddInfo(FString::Printf(TEXT("Event=light_prop_opposing_pull PrimaryTravel=%s HelperTravel=%s PrimaryIntent=%s HelperIntent=%s"), *PrimaryTravel.ToCompactString(), *HelperTravel.ToCompactString(), *Body->GetMoveIntent().ToCompactString(), *HelperBody->GetMoveIntent().ToCompactString()));
				Test->TestTrue(TEXT("actual traction pulls the helper against its move input without separating the chosen grip"),
					MaximumGripForce > 0 && MaximumHandGap < 5 && MaximumAnchorError < .1 && PrimaryTravel.X < -10 && HelperTravel.X < -10);
				Test->TestFalse(TEXT("pull test has no fish load"), Light->GetState().bExternalLoad);
				Test->TestEqual(TEXT("helper never becomes a fishing operator"), Rod->GetOperatorCount(), 1);
				Capture(Client, TEXT("formal-two-cats-pull-no-fish"));
				Rod->ReleasePhysicalPrimaryHoldFromAuthority(Cat->GetPlayerState(), TEXT("LightPropOwnerLeavesHelper"));
				Rod->RefreshPrimaryControlFromAuthority();
				Next(6, Now);
			}
			else if (Stage == 6)
			{
				if (Now - StageStarted > 3 && Light->GetState().GripCount != 1) { Test->AddError(TEXT("remaining helper grip was lost after owner release")); return true; }
				if (Now - StageStarted < 1 || ClientLight->GetState().GripCount != 1) return false;
				Test->TestEqual(TEXT("owner release keeps the exact helper grip"), HelperBody->GetGrab()->GetGripState(true).GripId, HelperGrip);
				Test->TestEqual(TEXT("remaining helper keeps prop weight-free"), Light->GetState().Mode, ECatLightPropMode::Held);
				Test->TestEqual(TEXT("remaining helper is not promoted"), Rod->GetOperatorCount(), 0);
				HelperBody->GetGrab()->ReleaseAllFromAuthority(TEXT("LightPropFinalRelease"));
				Next(7, Now);
			}
			else if (Stage == 7)
			{
				if (Now - StageStarted < 1 || ClientLight->GetState().GripCount != 0 || ClientLight->GetState().Revision != Light->GetState().Revision) return false;
				Test->TestEqual(TEXT("last-release gentle fall replicates"), ClientLight->GetState().Mode, ECatLightPropMode::Falling);
				Test->AddInfo(FString::Printf(TEXT("Event=light_prop_formal_network_verified PropId=%s MinBodyZ=%.3f MinUpZ=%.6f MaxBodyZ=%.3f MaxGripForceKgCmS2=%.3f ServerRevision=%u ClientRevision=%u Result=ObservedBothEndpoints"),
					*Light->GetState().PropId.ToString(), MinimumBodyZ, MinimumUp, MaximumBodyZ, MaximumGripForce, Light->GetState().Revision, ClientLight->GetState().Revision));
				Rod->Destroy();
				return true;
			}
			return false;
		}
	private:
		void Next(int32 Value, double Now) { Stage = Value; StageStarted = Now; }
		void Capture(UWorld* World, const TCHAR* Label)
		{
			if (!FApp::CanEverRender()) return;
			auto* VC = World->GetGameViewport(); FViewport* Viewport = VC ? VC->Viewport : nullptr;
			TArray<FColor> Pixels;
			if (!Test->TestTrue(TEXT("capture actual formal client viewport"), Viewport && GetViewportScreenShot(Viewport, Pixels))) return;
			const FIntPoint Size = Viewport->GetSizeXY();
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/LightProps/Images"));
			IFileManager::Get().MakeDirectory(*Directory, true);
			const FString File = Directory / FString::Printf(TEXT("%s-%s.png"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), Label);
			if (Test->TestTrue(TEXT("save formal light prop evidence"), FFileHelper::SaveArrayToFile(Png, *File))) Test->AddInfo(FString::Printf(TEXT("Event=light_prop_formal_viewport_captured File=%s"), *File));
		}
		FAutomationTestBase* Test;
		double Started, StageStarted = 0, MinimumBodyZ = 10000, MinimumUp = 1, MaximumBodyZ = 0, MaximumGripForce = 0;
		double MaximumTractionError = 0, MaximumAnchorError = 0;
		double BareMinimumZ = 10000, BareMaximumZ = 0, SupportedMinimumZ = 10000;
		int32 Stage = 0;
		ACatFishingRodActor* Rod = nullptr;
		TWeakObjectPtr<ACameraActor> ObservationCamera;
		FGuid HelperGrip;
		FVector HelperLocalContact = FVector::ZeroVector;
		double MaximumHandGap = 0.0;
		FVector PullStartPrimary, PullStartHelper;
		bool bJumpCaptured = false, bDropCaptured = false, bTwoGripsReplicated = false;
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatLightPropNetworkTest,
	"Catfishing.PhysicalGrab.Network.LightProps.FormalRodReleaseAndSharedPull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatLightPropNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle validation editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatLightPropNetwork::FRestore>();
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatLightPropNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
