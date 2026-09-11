#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Animation/AnimInstance.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
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

namespace CatPhysicalCharacterAnimationNetwork
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
			if (FPlatformTime::Seconds() - Started > 65.0)
			{
				Test->AddError(FString::Printf(TEXT("Formal animation network timed out Stage=%d ServerPhases=%u ClientPhases=%u ServerState=%s ClientState=%s"),
					Stage, ServerPhases, ClientPhases, *LastServerState.ToString(), *LastClientState.ToString()));
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
			if (!ClientCat || !Local->PlayerState || Local->AcknowledgedPawn != ClientCat) return false;
			ACatCharacter* ServerCat = nullptr;
			for (TActorIterator<ACatCharacter> It(Server); It; ++It)
				if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Local->PlayerState->GetPlayerId()) ServerCat = *It;
			if (!ServerCat) return false;
			auto* ServerBody = ServerCat->GetPhysicalBodyComponent();
			auto* ClientBody = ClientCat->GetPhysicalBodyComponent();
			auto* ServerVisual = ServerCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			auto* ClientVisual = ClientCat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
			UAnimInstance* ServerAnim = ServerCat->GetMesh()->GetAnimInstance();
			UAnimInstance* ClientAnim = ClientCat->GetMesh()->GetAnimInstance();
			if (!ServerBody || !ClientBody || !ServerVisual || !ClientVisual || !ServerAnim || !ClientAnim
				|| !ServerVisual->GetVisualMesh() || !ClientVisual->GetVisualMesh()) return false;
			const double WorldNow = Server->GetTimeSeconds();
			if (Stage == 0)
			{
				for (ACatCharacter* Cat : {ServerCat, ClientCat})
				{
					Test->TestEqual(TEXT("the subject is the formal cat Blueprint"), Cat->GetClass()->GetPathName(),
						FString(TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")));
					Test->TestEqual(TEXT("the original mesh still runs the formal animation Blueprint"), Cat->GetMesh()->GetAnimInstance()->GetClass()->GetPathName(),
						FString(TEXT("/Game/Animalia/Cat/ABP_Cat.ABP_Cat_C")));
					Test->TestTrue(TEXT("the shared visual copies the existing montage and ABP source"),
						Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetAnimationSource() == Cat->GetMesh());
					const auto* Body = Cat->GetPhysicalBodyComponent();
					const double MeshScale = Cat->GetMesh()->GetComponentTransform().GetRelativeTransform(Body->GetBody()->GetComponentTransform()).GetScale3D().GetAbsMax();
					Test->TestEqual(TEXT("the body preserves the authored formal mesh's geometry scale"), Body->GetGeometryScale(), MeshScale, 0.001);
					Test->TestTrue(TEXT("collision extent matches the authored body size"), Body->GetBody()->GetUnscaledBoxExtent().Equals(FVector(13,5,7) * MeshScale, 0.001));
					Test->TestEqual(TEXT("the hand sphere uses the same geometry scale"), double(Body->GetHand(true)->GetUnscaledSphereRadius()), 1.8 * MeshScale, 0.001);

				}
				if (Test->HasAnyErrors()) return true;
				Local->SetActorTickEnabled(false);
                for (TActorIterator<ACatCharacter> It(Server); It; ++It)
                    if (*It != ServerCat) It->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(
                        FTransform(FVector(-600,600,It->GetBodyStandRootHeightCm())), TEXT("AnimationObserverClearance"));
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
					if (It->Get()) It->Get()->SetActorTickEnabled(false);
				if (!Test->TestTrue(TEXT("initial placement resets the authority capsule and grip poses"),
					ServerBody->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, FVector(0, -150, ServerBody->GetStandRootHeightCm())), TEXT("FormalAnimationSetup")))) return true;
				if (FApp::CanEverRender())
				{
					ACameraActor* Camera = Client->SpawnActor<ACameraActor>();
					if (!Test->TestNotNull(TEXT("formal animation evidence has an independent side camera"), Camera)) return true;
					const FVector Position(-90, -500, 110), LookAt(0, -150, 75);
					Camera->SetActorLocationAndRotation(Position, (LookAt - Position).Rotation());
					Camera->GetCameraComponent()->SetFieldOfView(60.0f);
					Local->SetViewTarget(Camera);
				}
				Stage = 1;
				StageStarted = WorldNow;
			}
			if (ClientBody->GetResetEpoch() != ServerBody->GetResetEpoch() || ClientBody->GetControlEpoch() != ServerBody->GetControlEpoch()) return false;
			ClientBody->SetMoveIntent(FVector::ZeroVector);
			ClientBody->SetViewIntent(FRotator::ZeroRotator);
			if (Stage == 1)
			{
				if (WorldNow - StageStarted < 0.8 || !ServerBody->HasMovementSample() || !ClientBody->HasMovementSample()
					|| !ServerBody->IsGrounded() || !ClientBody->IsGrounded()
					|| FVector::Dist(ClientCat->GetActorLocation(), ServerCat->GetActorLocation()) > 2.0) return false;
				Test->TestTrue(TEXT("authority uses the upright CMC receiver"), ServerBody->UsesCharacterMovement());
				Test->TestFalse(TEXT("authority capsule does not freely tumble"), ServerBody->GetBody()->IsSimulatingPhysics());
				Test->TestFalse(TEXT("owning client predicts CMC without enabling a Chaos body"), ClientBody->GetBody()->IsSimulatingPhysics());
				for (ACatCharacter* Cat : {ServerCat, ClientCat})
				{
					const auto* Body = Cat->GetPhysicalBodyComponent();
					auto* Mesh = Cat->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()->GetVisualMesh();
					const FVector RootWorld = Mesh->GetBoneLocationByName(TEXT("RigRoot"), EBoneSpaces::WorldSpace);
					Test->TestTrue(TEXT("settled authored mesh root matches the supported foot plane"), FMath::Abs(RootWorld.Z - Body->GetSupportFootPointWorld().Z) < 1.0);
					Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_geometry_observed Endpoint=%s BodyZ=%.3f FootZ=%.3f MeshRootZ=%.3f MeshRelative=%s BoxExtent=%s Shoulder=%s Hand=%s VisualLeftAnkle=%s GeometryScale=%.3f"),
						Cat == ServerCat ? TEXT("Server") : TEXT("Client"), Cat->GetActorLocation().Z, Body->GetSupportFootPointWorld().Z, RootWorld.Z,
						*Mesh->GetRelativeTransform().ToString(), *Body->GetBody()->GetScaledBoxExtent().ToCompactString(), *Body->GetGrab()->GetShoulderWorldLocation(true).ToCompactString(),
						*Body->GetHand(true)->GetComponentLocation().ToCompactString(), *Mesh->GetBoneLocationByName(TEXT("RigLFLegAnkle"), EBoneSpaces::WorldSpace).ToCompactString(), Body->GetGeometryScale()));
				}
				if (FApp::CanEverRender()) Capture(Client, TEXT("formal-jump-before-takeoff"));
				InitialServerZ = MaximumServerZ = ServerCat->GetActorLocation().Z;
				InitialClientZ = MaximumClientZ = ClientCat->GetActorLocation().Z;
				ClientBody->RequestJump();
				Stage = 2;
				StageStarted = WorldNow;
			}
			if (Stage == 2)
			{
				MaximumServerZ = FMath::Max(MaximumServerZ, ServerCat->GetActorLocation().Z);
				MaximumClientZ = FMath::Max(MaximumClientZ, ClientCat->GetActorLocation().Z);
				if (!Observe(ServerCat, ServerVisual, ServerAnim, TEXT("Server"), ServerPhases, LastServerState)
					|| !Observe(ClientCat, ClientVisual, ClientAnim, TEXT("Client"), ClientPhases, LastClientState)) return true;
				const uint8 Phase = PhaseForState(LastClientState);
				if (FApp::CanEverRender() && Phase != 0 && (CapturedPhases & Phase) == 0)
				{
					Capture(Client, *FString::Printf(TEXT("formal-jump-%s"), *LastClientState.ToString().Replace(TEXT(" "), TEXT("-"))));
					CapturedPhases |= Phase;
				}
				if (FApp::CanEverRender() && !bCapturedLanding && WorldNow - StageStarted > 2.0
					&& ClientBody->IsGrounded() && LastClientState == TEXT("Locomotion"))
				{
					Capture(Client, TEXT("formal-jump-grounded-observation"));
					bCapturedLanding = true;
				}
				if (WorldNow - StageStarted < 2.0 || ServerPhases != 7 || ClientPhases != 7
					|| !ServerBody->IsGrounded() || !ClientBody->IsGrounded()
					|| LastServerState != TEXT("Locomotion") || LastClientState != TEXT("Locomotion"))
				{
					GroundedLocomotionSinceSeconds = 0.0;
					return false;
				}
				if (GroundedLocomotionSinceSeconds == 0.0) GroundedLocomotionSinceSeconds = WorldNow;
				if (WorldNow - GroundedLocomotionSinceSeconds < 0.3) return false;
				Test->TestTrue(TEXT("the actual formal cat physically jumps with its configured launch speed"), MaximumServerZ - InitialServerZ > 70.0);
				Test->TestTrue(TEXT("owning client observes the same physical rise"), MaximumClientZ - InitialClientZ > 65.0
					&& FMath::Abs((MaximumServerZ - InitialServerZ) - (MaximumClientZ - InitialClientZ)) < 8.0);
				Test->TestTrue(TEXT("the authored ABP source retains its original landing root motion"), MaximumSourceRootZ > 20.0);
				Test->TestTrue(TEXT("the visible pose never adds a second vertical jump during takeoff, landing or their transitions"), MaximumVisibleRootOffsetCm < 0.1);
				if (FApp::CanEverRender()) Capture(Client, TEXT("formal-jump-grounded-locomotion"));
				Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_animation_network_verified ServerRiseCm=%.3f ClientRiseCm=%.3f ServerPhases=%u ClientPhases=%u SourceRootMaxZ=%.3f VisibleRootMaxZ=%.3f MaximumVisibleRootOffsetCm=%.3f LandedVisibleRootZ=%.3f Evidence=runtime_behavior Presentation=NeedsScreenshotReview"),
					MaximumServerZ - InitialServerZ, MaximumClientZ - InitialClientZ, ServerPhases, ClientPhases,
					MaximumSourceRootZ, MaximumVisibleRootZ, MaximumVisibleRootOffsetCm, ClientVisual->GetVisualMesh()->BoneSpaceTransforms[0].GetTranslation().Z));
                StandingHeadZ = ClientVisual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigHead"), EBoneSpaces::WorldSpace).Z;
                SupportedHeightZ = ServerCat->GetActorLocation().Z;
                ServerCat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 115);
                Test->AddExpectedMessage(TEXT("Event=character_downed"), ELogVerbosity::Warning);
                if (!Test->TestTrue(TEXT("authority evaluates the real downed threshold"), CatIsAcceptedDomainCommandResult(
                    ServerCat->GetConditionComponent()->RequestFieldSelfRecovery(ServerCat->GetController(), FGuid::NewGuid())))) return true;
                Stage = 3;
                StageStarted = WorldNow;
            }
            if (Stage == 3)
            {
                auto* ServerPose = ServerCat->FindComponentByClass<UCatConditionPresentationComponent>();
                auto* ClientPose = ClientCat->FindComponentByClass<UCatConditionPresentationComponent>();
                if (WorldNow - StageStarted < 8.0 || !ClientCat->GetConditionComponent()->GetSnapshot().bDowned
                    || ServerPose->GetObservedPosePhase() != TEXT("DownedPose") || ClientPose->GetObservedPosePhase() != TEXT("DownedPose")) return false;
                const double HeadZ = ClientVisual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigHead"), EBoneSpaces::WorldSpace).Z;
                Test->TestTrue(TEXT("client sees the authored lying pose after downed replication"), HeadZ < StandingHeadZ - 3.0);
                Test->TestTrue(TEXT("both downed capsules stay upright and supported"), ServerCat->GetActorUpVector().Z > .99999
                    && ClientCat->GetActorUpVector().Z > .99999 && FMath::Abs(ServerCat->GetActorLocation().Z - SupportedHeightZ) < .5);
                if (FApp::CanEverRender()) Capture(Client, TEXT("formal-cmc-downed-animation"));
                Test->AddInfo(FString::Printf(TEXT("Event=cmc_condition_network_downed HeadStandingZ=%.3f HeadLyingZ=%.3f ServerZ=%.3f ClientZ=%.3f"),
                    StandingHeadZ, HeadZ, ServerCat->GetActorLocation().Z, ClientCat->GetActorLocation().Z));
                if (!Test->TestTrue(TEXT("authority recovery clears the actual downed state"), CatIsAcceptedDomainCommandResult(
                    ServerCat->GetConditionComponent()->RequestFieldSelfRecovery(ServerCat->GetController(), FGuid::NewGuid())))) return true;
                Stage = 4;
                StageStarted = WorldNow;
            }
            if (Stage == 4)
            {
                if (WorldNow - StageStarted < 8.0 || ClientCat->GetConditionComponent()->GetSnapshot().bDowned
                    || ServerCat->FindComponentByClass<UCatConditionPresentationComponent>()->GetObservedPosePhase() != TEXT("Locomotion")
                    || ClientCat->FindComponentByClass<UCatConditionPresentationComponent>()->GetObservedPosePhase() != TEXT("Locomotion")) return false;
                Test->TestTrue(TEXT("client and authority restore locomotion after the get-up animation"),
                    ServerBody->IsLocomotionEnabled() && ClientBody->IsLocomotionEnabled());
                const double HeadZ = ClientVisual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigHead"), EBoneSpaces::WorldSpace).Z;
                Test->TestTrue(TEXT("visible client returns to standing height"), FMath::Abs(HeadZ - StandingHeadZ) < 3.0);
                if (FApp::CanEverRender()) Capture(Client, TEXT("formal-cmc-recovered-animation"));
                Test->AddInfo(FString::Printf(TEXT("Event=cmc_condition_network_recovered HeadZ=%.3f ServerZ=%.3f ClientZ=%.3f"),
                    HeadZ, ServerCat->GetActorLocation().Z, ClientCat->GetActorLocation().Z));
                return true;
            }
            return false;
        }
    private:
		static uint8 PhaseForState(FName State)
		{
			return State == TEXT("Jump") ? 1 : State == TEXT("Fall Loop") ? 2 : State == TEXT("Land") ? 4 : 0;
		}
		bool Observe(ACatCharacter* Cat, UCatPhysicsPrototypeVisualComponent* Visual, UAnimInstance* Animation,
			const TCHAR* Endpoint, uint8& Phases, FName& PreviousState)
		{
			const int32 MachineIndex = Animation->GetStateMachineIndex(TEXT("Main States"));
			if (!Test->TestTrue(TEXT("formal ABP exposes its actual main state machine"), MachineIndex != INDEX_NONE)) return false;
			const FName State = Animation->GetCurrentStateName(MachineIndex);
			const auto& SourcePose = Cat->GetMesh()->GetBoneSpaceTransforms();
			const auto& VisiblePose = Visual->GetVisualMesh()->BoneSpaceTransforms;
			if (!Test->TestTrue(TEXT("formal source and final visible pose both contain a finite root"), !SourcePose.IsEmpty() && !VisiblePose.IsEmpty()
				&& !SourcePose[0].ContainsNaN() && !VisiblePose[0].ContainsNaN())) return false;
			// Observe both poses throughout the jump, including the full landing-to-locomotion blend.
			// Authored root translation stays in the ABP source while visible vertical motion belongs to the authority movement receiver.
			MaximumSourceRootZ = FMath::Max(MaximumSourceRootZ, SourcePose[0].GetTranslation().Z);
			MaximumVisibleRootZ = FMath::Max(MaximumVisibleRootZ, VisiblePose[0].GetTranslation().Z);
			const double ReferenceRootZ = Cat->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose()[0].GetTranslation().Z;
			MaximumVisibleRootOffsetCm = FMath::Max(MaximumVisibleRootOffsetCm, FMath::Abs(VisiblePose[0].GetTranslation().Z - ReferenceRootZ));
			Phases |= PhaseForState(State);
			if (State != PreviousState)
			{
				Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_animation_state Endpoint=%s State=%s BodyZ=%.3f Velocity=%s SourceRoot=%s VisibleRoot=%s VisibleRootWorld=%s AnimClass=%s"),
					Endpoint, *State.ToString(), Cat->GetActorLocation().Z, *Cat->GetVelocity().ToCompactString(),
					*SourcePose[0].ToString(), *VisiblePose[0].ToString(),
					*Visual->GetVisualMesh()->GetBoneLocationByName(TEXT("RigRoot"), EBoneSpaces::WorldSpace).ToCompactString(),
					*Animation->GetClass()->GetPathName()));
				PreviousState = State;
			}
			return true;
		}
		void Capture(UWorld* World, const TCHAR* Label)
		{
			UGameViewportClient* Client = World->GetGameViewport();
			FViewport* Viewport = Client ? Client->Viewport : nullptr;
			if (!Test->TestNotNull(TEXT("rendering formal animation has its client viewport"), Viewport)) return;
			TArray<FColor> Pixels;
			if (!Test->TestTrue(TEXT("capture the actual formal cat client viewport"), GetViewportScreenShot(Viewport, Pixels))) return;
			const FIntPoint Size = Viewport->GetSizeXY();
			if (!Test->TestTrue(TEXT("formal capture has valid dimensions"), Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y)) return;
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/PhysicalGrab/Images"));
			IFileManager::Get().MakeDirectory(*Directory, true);
			const FString File = Directory / FString::Printf(TEXT("%s-%s.png"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), Label);
			if (Test->TestTrue(TEXT("save formal animation screenshot"), FFileHelper::SaveArrayToFile(Png, *File)))
				Test->AddInfo(FString::Printf(TEXT("Event=physical_formal_animation_viewport_captured File=%s Width=%d Height=%d"), *File, Size.X, Size.Y));
		}
		FAutomationTestBase* Test;
		double Started, StageStarted = 0.0;
		int32 Stage = 0;
		uint8 ServerPhases = 0, ClientPhases = 0, CapturedPhases = 0;
		bool bCapturedLanding = false;
		FName LastServerState, LastClientState;
		double InitialServerZ = 0.0, InitialClientZ = 0.0, MaximumServerZ = 0.0, MaximumClientZ = 0.0;
		double MaximumSourceRootZ = 0.0, MaximumVisibleRootZ = 0.0;
		double StandingHeadZ = 0.0, SupportedHeightZ = 0.0;
		double MaximumVisibleRootOffsetCm = 0.0, GroundedLocomotionSinceSeconds = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPhysicalCharacterAnimationNetworkTest,
	"Catfishing.PhysicalBody.Network.FormalBlueprintJumpUsesAuthoredAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPhysicalCharacterAnimationNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle validation editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatPhysicalCharacterAnimationNetwork::FRestore>();
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatPhysicalCharacterAnimationNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
