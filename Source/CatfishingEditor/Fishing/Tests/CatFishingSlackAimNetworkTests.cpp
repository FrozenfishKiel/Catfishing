#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Camera/CameraTypes.h"
#include "Character/CatCharacter.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/ReplicatedState.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace CatFishingSlackAimNetwork
{
	// 使用真实项目 Controller/Pawn 和网络组件，但不启动整场 Run 或平台准入。
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode);
			Settings->GetPlayNumberOfClients(Count);
			Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		~FRestore() override { FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeInitialized); }

		void ConfigureGameMode(FAutomationTestBase* Test)
		{
			// Native GameMode construction resets its class fields; changing the native CDO does not
			// supply this fixture's classes. InitGame broadcasts here before any player is logged in.
			GameModeInitialized = FGameModeEvents::OnGameModeInitializedEvent().AddLambda([Test](AGameModeBase* GameMode)
			{
				if (!GameMode || !GameMode->GetWorld() || GameMode->GetWorld()->WorldType != EWorldType::PIE
					|| GameMode->GetClass() != AGameModeBase::StaticClass()) return;
				GameMode->PlayerControllerClass = ACatfishingPlayerController::StaticClass();
				GameMode->DefaultPawnClass = ACatCharacter::StaticClass();
				GameMode->PlayerStateClass = ACatfishingPlayerState::StaticClass();
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_fixture_configured World=%s ControllerClass=%s PawnClass=%s PlayerStateClass=%s"),
					*GetNameSafe(GameMode->GetWorld()), *GetNameSafe(GameMode->PlayerControllerClass.Get()),
					*GetNameSafe(GameMode->DefaultPawnClass.Get()), *GetNameSafe(GameMode->PlayerStateClass.Get())));
			});
		}

		bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			Settings->SaveConfig();
			GEngine->NetDriverDefinitions = Drivers;
			FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeInitialized);
			GameModeInitialized.Reset();
			return true;
		}

	private:
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle GameModeInitialized;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest)
			: Test(InTest), Started(FPlatformTime::Seconds()) {}
		~FVerify() override { RestorePacketSimulation(); }

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 45.0)
			{
				RestorePacketSimulation();
				Test->AddError(FString::Printf(TEXT("Slack aim listen/client timeout Stage=%d LastWait={%s}; no network verdict"), Stage, *LastWaitReason));
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
			if (!Server || !Client) return WaitFor(FString::Printf(TEXT("Worlds Server=%s Client=%s"),
				*GetNameSafe(Server), *GetNameSafe(Client)));
			ACatfishingPlayerController* ClientController = Cast<ACatfishingPlayerController>(Client->GetFirstPlayerController());
			ACatfishingPlayerController* RemoteController = nullptr;
			for (TActorIterator<ACatfishingPlayerController> It(Server); It; ++It)
			{
				if (!It->IsLocalController()) RemoteController = *It;
			}
			ACatCharacter* ServerCharacter = RemoteController ? Cast<ACatCharacter>(RemoteController->GetPawn()) : nullptr;
			ACatCharacter* ClientCharacter = ClientController ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr;
			if (!RemoteController || !ClientController || !ServerCharacter || !ClientCharacter
				|| !RemoteController->PlayerState || !ClientController->PlayerState)
			{
				FString ServerControllers;
				for (TActorIterator<APlayerController> It(Server); It; ++It)
				{
					ServerControllers += FString::Printf(TEXT("[%s Class=%s Local=%d Pawn=%s PlayerState=%s]"),
						*GetNameSafe(*It), *GetNameSafe(It->GetClass()), It->IsLocalController(),
						*GetNameSafe(It->GetPawn()), *GetNameSafe(It->PlayerState));
				}
				APlayerController* RawClientController = Client->GetFirstPlayerController();
				return WaitFor(FString::Printf(TEXT("Players ServerControllers=%s ClientController=%s ClientClass=%s ClientPawn=%s ClientPlayerState=%s"),
					*ServerControllers, *GetNameSafe(RawClientController),
					*GetNameSafe(RawClientController ? RawClientController->GetClass() : nullptr),
					*GetNameSafe(RawClientController ? RawClientController->GetPawn() : nullptr),
					*GetNameSafe(RawClientController ? RawClientController->PlayerState.Get() : nullptr)));
			}
			UCatFishingCommandComponent* ClientCommand = ClientController->GetFishingCommandComponent();
			if (!Test->TestNotNull(TEXT("owning client has the native fishing command component"), ClientCommand)) return true;

			if (Stage == 0)
			{
				// 无地形夹具只冻结平移，保留真实 Controller、CMC、Rod Tick 与复制顺序。
				ServerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				ServerCharacter->GetCharacterMovement()->StopMovementImmediately();
				ClientCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				ClientController->SetViewTarget(ClientCharacter);
				RemoteController->SetControlRotation(FRotator::ZeroRotator);
				ClientController->SetControlRotation(FRotator(0, 120, 0));
				UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr,
					TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
				UCatFishingService* Fishing = Server->GetSubsystem<UCatFishingService>();
				if (!Test->TestNotNull(TEXT("formal rod Blueprint class"), RodClass)
					|| !Test->TestNotNull(TEXT("authority fishing service"), Fishing)) return true;
				const FTransform SpawnTransform(ServerCharacter->GetActorLocation());
				ServerRod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform);
				if (!Test->TestNotNull(TEXT("spawn formal rod on server"), ServerRod.Get())) return true;
				if (!Test->TestTrue(TEXT("canonical non-identity grip configured before initial replication"),
					ServerRod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(200, 0, 0)),
						FTransform::Identity, FTransform(FRotator(5, 20, 0), FVector(13, 7, 4))))
					|| !Test->TestTrue(TEXT("remote player holds the authority rod"), ServerRod->InitializeAuthoritativeIdentity(
						RodId, FGuid::NewGuid(), TEXT("SlackAimNetworkRod"), NAME_None,
						RemoteController->PlayerState, RemoteController->PlayerState, true, false))) return true;
				ServerRod->bAlwaysRelevant = true;
				ServerRod->FinishSpawning(SpawnTransform);
				if (!Test->TestTrue(TEXT("authority registers the production RPC lookup"),
					Fishing->RegisterDeployedRod(RemoteController->PlayerState, ServerRod.Get()))
					|| !Test->TestTrue(TEXT("initialize actual held direction"), ServerRod->RefreshHeldTransformFromAuthority())
					|| !Test->TestTrue(TEXT("establish a resisted fight"), ServerRod->SetCarrierConstraintFromAuthority(
						FVector::ForwardVector, 0, 0, 1, 0, true, 100, 50))) return true;
				RemoteController->SetControlRotation(FRotator(0, 120, 0));
				for (int32 Index = 0; Index < 360; ++Index) ServerRod->RefreshHeldTransformFromAuthority(1.0 / 60.0);
				ServerRod->ForceNetUpdate();
				Stage = 1;
				StageStarted = Now;
			}

			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
			{
				if (It->GetPresentationState().RodActorId == RodId) ClientRod = *It;
			}
			if (!ClientRod || ClientRod->GetPresentationState().HolderPlayerState != ClientController->PlayerState
				|| ClientRod->GetCarrierConstraintState().AimInputEpoch == 0
				|| UCatFishingCameraComponent::FindFightRodHeldBy(ClientController) != ClientRod)
				return WaitFor(FString::Printf(TEXT("ReplicatedRod Rod=%s Holder=%s LocalPlayerState=%s Epoch=%u"),
					*GetNameSafe(ClientRod), *GetNameSafe(ClientRod ? ClientRod->GetPresentationState().HolderPlayerState.Get() : nullptr),
					*GetNameSafe(ClientController->PlayerState), ClientRod ? ClientRod->GetCarrierConstraintState().AimInputEpoch : 0));
			UCatFishingCameraComponent* Camera = ClientCharacter->FindComponentByClass<UCatFishingCameraComponent>();
			FMinimalViewInfo View;
			if (!Test->TestTrue(TEXT("owning client uses actual replicated rod camera"),
				Camera && Camera->TryGetCameraView(1.0f / 60.0f, View))) return true;
			if (Stage >= 2)
			{
				MaximumCameraStepDegrees = FMath::Max(MaximumCameraStepDegrees,
					FMath::RadiansToDegrees(PreviousCamera.AngularDistance(View.Rotation.Quaternion())));
			}
			PreviousCamera = View.Rotation.Quaternion();
			FCatFishingRodRotationPrediction Prediction;
			if (!Test->TestTrue(TEXT("server can read the production rotation prediction"),
				ServerRod->GetRotationPredictionFromAuthority(1.0 / 60.0, Prediction))) return true;
			const FQuat ExpectedClientGrip = ExpectedReplicatedGripRotation(*ServerRod);
			const double ClientGripErrorDegrees = FMath::RadiansToDegrees(ExpectedClientGrip.AngularDistance(
				ClientRod->GetGripWorldTransform().GetRotation()));
			const double CameraTargetErrorDegrees = FMath::RadiansToDegrees(ExpectedClientGrip.AngularDistance(View.Rotation.Quaternion()));
			LastWaitReason = FString::Printf(TEXT("Pose RequestedYaw=%.3f ServerYaw=%.3f ExpectedReplicatedYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f GripErrorDeg=%.3f CameraErrorDeg=%.3f"),
				Prediction.Input.RequestedAim.Yaw, Prediction.Input.CurrentAim.Yaw, ExpectedClientGrip.Rotator().Yaw,
				ClientRod->GetGripWorldTransform().Rotator().Yaw, View.Rotation.Yaw, ClientGripErrorDegrees, CameraTargetErrorDegrees);

			if (Stage == 1 && Now - StageStarted > 0.5
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestTrue(TEXT("blocked actual rod remains far behind the old controller target"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(Prediction.Input.CurrentAim.Yaw, 120.0)) > 60.0);
				FCatFishingRodAimSample Edge;
				Edge.RodActorId = RodId;
				Edge.InputEpoch = ServerRod->GetCarrierConstraintState().AimInputEpoch;
				Edge.Sequence = 1;
				// The zero cumulative edge predates only stationary snapshots; no later mouse input is discarded.
				if (!Test->TestTrue(TEXT("authority accepts the controlled release boundary"),
					ServerRod->CanRebaseHeldAimFromAuthority(RemoteController->PlayerState, Edge))) return true;
				Test->TestFalse(TEXT("client cannot rebase authority rod state"),
					ClientRod->CanRebaseHeldAimFromAuthority(ClientController->PlayerState, Edge));
				Test->TestFalse(TEXT("client cannot submit an authority aim write locally"),
					ClientRod->AcceptHeldAimSampleFromAuthority(ClientController->PlayerState, Edge));
				const FTransform Before = ServerRod->GetActorTransform();
				const FTransform ClientBefore = ClientRod->GetActorTransform();
				RebasedAim = Prediction.Input.CurrentAim;
				ClientRod->RebaseHeldAimFromAuthority(ClientController->PlayerState, Edge, FGuid::NewGuid(), 1);
				Test->TestTrue(TEXT("rejected client rebase has no local pose side effect"),
					ClientRod->GetActorTransform().Equals(ClientBefore));
				// This network fixture starts at the authoritative rebase API. The complete right-button
				// Command -> Session permission/priority path belongs to the runtime regression fixture.
				ServerRod->RebaseHeldAimFromAuthority(RemoteController->PlayerState, Edge, FGuid::NewGuid(), 1);
				Test->TestTrue(TEXT("rebase never teleports the server rod"), ServerRod->GetActorTransform().Equals(Before));
				ServerRod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0, 0, 0, 0, true, 0, 50);
				ServerRod->ForceNetUpdate();
				Stage = 2;
				StageStarted = Now;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_rebased RodActorId=%s ServerNetMode=%d ClientNetMode=%d ActualYaw=%.3f OldControlYaw=120 Boundary=AuthorityRebaseAPI"),
					*RodId.ToString(), Server->GetNetMode(), Client->GetNetMode(), RebasedAim.Yaw));
			}
			else if (Stage == 2)
			{
				// Simulate CMC/controller rotation arriving after release. It remains an old absolute intent.
				RemoteController->SetControlRotation(FRotator(0, 120, 0));
				ClientController->SetControlRotation(FRotator(0, 120, 0));
				if (!Test->TestTrue(TEXT("stale absolute controller rotation cannot refill the rebased target"),
					Prediction.Input.RequestedAim.Equals(RebasedAim, 0.01))) return true;
				if (Now - StageStarted < 1.0 || !Prediction.Input.CurrentAim.Equals(RebasedAim, 0.2)) return false;
				ClientCommand->UpdateLocalRodAimInput(1.0 / 30.0, FRotator(0, 5, 0));
				Stage = 3;
				StageStarted = Now;
			}
			else if (Stage == 3 && IsAtYaw(Prediction.Input.RequestedAim, RebasedAim.Yaw + 5.0)
				&& IsAtYaw(Prediction.Input.CurrentAim, RebasedAim.Yaw + 5.0)
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestTrue(TEXT("new remote mouse motion reaches the existing quantized replicated pose"),
					ClientGripErrorDegrees < 0.05);
#if DO_ENABLE_NET_TEST
				PacketDriver = Client->GetNetDriver();
				if (!Test->TestNotNull(TEXT("client has a real NetDriver for packet-loss validation"), PacketDriver.Get())) return true;
				SavedPacketSimulation = PacketDriver->PacketSimulationSettings;
				FPacketSimulationSettings Drop = SavedPacketSimulation;
				Drop.PktLoss = 100;
				Drop.PktLossMinSize = 0;
				// NetDriver.h converts this byte limit to int32 bits. Zero means unlimited;
				// MAX_int32 overflows when multiplied by eight and would silently disable loss.
				Drop.PktLossMaxSize = 0;
				PacketDriver->SetPacketSimulationSettings(Drop);
				bPacketSimulationChanged = true;
				if (!Test->TestTrue(TEXT("packet loss is applied to the real client connection without a size exclusion"),
					PacketDriver->PacketSimulationSettings.PktLoss == 100 && PacketDriver->ServerConnection
					&& PacketDriver->ServerConnection->PacketSimulationSettings.PktLoss == 100
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(8)
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(65536))) return true;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_loss_started RodActorId=%s Connection=%s PktLoss=100 MaxSizeBytes=0 RequestedYaw=%.3f"),
					*RodId.ToString(), *GetNameSafe(PacketDriver->ServerConnection), Prediction.Input.RequestedAim.Yaw));
				ClientCommand->UpdateLocalRodAimInput(1.0 / 30.0, FRotator(0, 3, 0));
				Stage = 4;
				StageStarted = Now;
#else
				Test->AddError(TEXT("Net packet simulation is unavailable; stationary recovery remains unverified"));
				return true;
#endif
			}
			else if (Stage == 4 && Now - StageStarted > 0.3)
			{
				if (!Test->TestTrue(TEXT("100 percent packet loss withheld the final nonzero mouse sample"),
					IsAtYaw(Prediction.Input.RequestedAim, RebasedAim.Yaw + 5.0))) return true;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_loss_verified RodActorId=%s RequestedYaw=%.3f LostLookYaw=3 Result=InputWithheld"),
					*RodId.ToString(), Prediction.Input.RequestedAim.Yaw));
				RestorePacketSimulation();
				// No second movement: only the stationary cumulative snapshot may recover the dropped input.
				ClientCommand->UpdateLocalRodAimInput(1.0 / 30.0, FRotator::ZeroRotator);
				Stage = 5;
				StageStarted = Now;
			}
			else if (Stage == 5 && IsAtYaw(Prediction.Input.RequestedAim, RebasedAim.Yaw + 8.0)
				&& IsAtYaw(Prediction.Input.CurrentAim, RebasedAim.Yaw + 8.0)
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestTrue(TEXT("stationary snapshots recover the last lost mouse delta through the server RPC"),
					IsAtYaw(Prediction.Input.RequestedAim, RebasedAim.Yaw + 8.0) && ClientGripErrorDegrees < 0.05);
				Test->TestTrue(TEXT("client camera stays continuous across release and replicated small turns"),
					MaximumCameraStepDegrees < 8.0);
				Test->TestTrue(TEXT("replication did not restore the old absolute controller target"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(Prediction.Input.CurrentAim.Yaw, 120.0)) > 60.0);
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_recovered RodActorId=%s NewLookYaw=8 RequestedYaw=%.3f ServerYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f ExpectedReplicatedYaw=%.3f GripErrorDegrees=%.3f MaximumCameraStepDegrees=%.3f Result=StationarySnapshotRecovered"),
					*RodId.ToString(), Prediction.Input.RequestedAim.Yaw, Prediction.Input.CurrentAim.Yaw,
					ClientRod->GetGripWorldTransform().Rotator().Yaw, View.Rotation.Yaw,
					ExpectedClientGrip.Rotator().Yaw, ClientGripErrorDegrees, MaximumCameraStepDegrees));
				return true;
			}
			return false;
		}

	private:
		bool WaitFor(const FString& Reason)
		{
			if (Reason != LastWaitReason)
			{
				LastWaitReason = Reason;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_wait Stage=%d %s"), Stage, *Reason));
			}
			return false;
		}

		static bool IsAtYaw(const FRotator& Rotation, const double Yaw)
		{
			return FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, Yaw)) < 0.2;
		}

		static FQuat ExpectedReplicatedGripRotation(const ACatFishingRodActor& Rod)
		{
			// Preserve the shipped replication contract: Actor Euler axes are quantized first,
			// then the unchanged canonical grip is composed. A non-identity grip also mixes the
			// quantized Pitch/Roll into visible Yaw, so a wider raw-Yaw tolerance is insufficient.
			const ERotatorQuantization Quantization = Rod.GetReplicatedMovement().RotationQuantizationLevel;
			const auto QuantizeAxis = [Quantization](const double Degrees)
			{
				return Quantization == ERotatorQuantization::ShortComponents
					? FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(Degrees))
					: FRotator::DecompressAxisFromByte(FRotator::CompressAxisToByte(Degrees));
			};
			const FTransform GripLocal = Rod.GetGripWorldTransform().GetRelativeTransform(Rod.GetActorTransform());
			FTransform QuantizedActor = Rod.GetActorTransform();
			const FRotator Actual = Rod.GetActorRotation();
			QuantizedActor.SetRotation(FRotator(QuantizeAxis(Actual.Pitch), QuantizeAxis(Actual.Yaw), QuantizeAxis(Actual.Roll)).Quaternion());
			return (GripLocal * QuantizedActor).GetRotation();
		}

		void RestorePacketSimulation()
		{
#if DO_ENABLE_NET_TEST
			if (bPacketSimulationChanged && PacketDriver.IsValid())
				PacketDriver->SetPacketSimulationSettings(SavedPacketSimulation);
			bPacketSimulationChanged = false;
#endif
		}

		FAutomationTestBase* Test;
		double Started;
		double StageStarted = 0.0;
		int32 Stage = 0;
		FString LastWaitReason;
		FGuid RodId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishingRodActor> ServerRod;
		FRotator RebasedAim = FRotator::ZeroRotator;
		FQuat PreviousCamera = FQuat::Identity;
		double MaximumCameraStepDegrees = 0.0;
#if DO_ENABLE_NET_TEST
		TWeakObjectPtr<UNetDriver> PacketDriver;
		FPacketSimulationSettings SavedPacketSimulation;
		bool bPacketSimulationChanged = false;
#endif
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSlackAimNetworkTest,
	"Catfishing.Editor.Fishing.SlackAimListenClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSlackAimNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires an idle editor"), GEditor && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatFishingSlackAimNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("isolated unsaved map"), Map)) return false;
	Restore->ConfigureGameMode(this);
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>(FVector(0, 0, 200), FRotator::ZeroRotator);
	Map->SpawnActor<APlayerStart>(FVector(400, 0, 200), FRotator::ZeroRotator);
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishingSlackAimNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
