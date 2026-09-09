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
#include "Engine/World.h"
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
			: Test(InTest), Started(FPlatformTime::Seconds())
		{
			// Feed each real Controller tick before its input pass. A one-shot direct call would be
			// followed by the next automatic zero-input frame, which must now send a mouse stop.
			InputTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda(
				[this](UWorld* World, ELevelTick, float DeltaSeconds)
				{
					ACatfishingPlayerController* Controller = InputController.Get();
					if (!Controller || Controller->GetWorld() != World || World->GetNetMode() != NM_Client) return;
					const double DeltaYaw = bInjectMouse ? PendingMouseYaw + MouseYawRate * DeltaSeconds : 0.0;
					PendingMouseYaw = 0.0;
					StrokeAppliedYaw += DeltaYaw;
					Controller->RotationInput = FRotator(0.0, DeltaYaw, 0.0);
				});
		}
		~FVerify() override
		{
			FWorldDelegates::OnWorldPreActorTick.Remove(InputTickHandle);
			RestorePacketSimulation();
		}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 45.0)
			{
				RestorePacketSimulation();
				Test->AddError(FString::Printf(TEXT("Mouse drive listen/client timeout Stage=%d LastWait={%s}; no network verdict"), Stage, *LastWaitReason));
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
			if (!Server || !Client) return WaitFor(TEXT("waiting for real listen and client worlds"));
			auto* ClientController = Cast<ACatfishingPlayerController>(Client->GetFirstPlayerController());
			ACatfishingPlayerController* RemoteController = nullptr;
			for (TActorIterator<ACatfishingPlayerController> It(Server); It; ++It)
				if (!It->IsLocalController()) RemoteController = *It;
			auto* ServerCharacter = RemoteController ? Cast<ACatCharacter>(RemoteController->GetPawn()) : nullptr;
			auto* ClientCharacter = ClientController ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr;
			if (!RemoteController || !ClientController || !ServerCharacter || !ClientCharacter
				|| !RemoteController->PlayerState || !ClientController->PlayerState)
				return WaitFor(TEXT("waiting for real project controllers, characters and player states"));
			if (!Test->TestNotNull(TEXT("owning client has the native fishing command component"),
				ClientController->GetFishingCommandComponent())) return true;
			InputController = ClientController;

			if (Stage == 0)
			{
				ServerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				ServerCharacter->GetCharacterMovement()->StopMovementImmediately();
				ClientCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				ClientController->SetViewTarget(ClientCharacter);
				RemoteController->SetControlRotation(FRotator::ZeroRotator);
				ClientController->SetControlRotation(FRotator::ZeroRotator);
				UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr,
					TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
				auto* Fishing = Server->GetSubsystem<UCatFishingService>();
				if (!Test->TestNotNull(TEXT("formal rod Blueprint class"), RodClass)
					|| !Test->TestNotNull(TEXT("authority fishing service"), Fishing)) return true;
				const FTransform SpawnTransform(ServerCharacter->GetActorLocation());
				ServerRod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform);
				if (!Test->TestNotNull(TEXT("spawn formal rod on server"), ServerRod.Get())) return true;
				if (!Test->TestTrue(TEXT("canonical non-identity grip configured before initial replication"),
					ServerRod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(200, 0, 0)),
						FTransform::Identity, FTransform(FRotator(5, 20, 0), FVector(13, 7, 4))))
					|| !Test->TestTrue(TEXT("remote player holds the authority rod"), ServerRod->InitializeAuthoritativeIdentity(
						RodId, FGuid::NewGuid(), TEXT("MouseDriveNetworkRod"), NAME_None,
						RemoteController->PlayerState, RemoteController->PlayerState, true, false))) return true;
				ServerRod->bAlwaysRelevant = true;
				ServerRod->FinishSpawning(SpawnTransform);
				if (!Test->TestTrue(TEXT("authority registers the production RPC lookup"),
					Fishing->RegisterDeployedRod(RemoteController->PlayerState, ServerRod.Get()))
					|| !Test->TestTrue(TEXT("initialize actual held direction"), ServerRod->RefreshHeldTransformFromAuthority())
					|| !Test->TestTrue(TEXT("establish a resisted fight"), ServerRod->SetCarrierConstraintFromAuthority(
						FVector::ForwardVector, 0, 0, 1, 0, true, 100, 50))) return true;
				ServerRod->ForceNetUpdate();
				Stage = 1;
				StageStarted = Now;
			}

			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
				if (It->GetPresentationState().RodActorId == RodId) ClientRod = *It;
			if (!ClientRod || ClientRod->GetPresentationState().HolderPlayerState != ClientController->PlayerState
				|| ClientRod->GetCarrierConstraintState().AimInputEpoch == 0
				|| UCatFishingCameraComponent::FindFightRodHeldBy(ClientController) != ClientRod)
				return WaitFor(TEXT("waiting for the owning client's replicated held rod and nonzero aim epoch"));
			auto* Camera = ClientCharacter->FindComponentByClass<UCatFishingCameraComponent>();
			FMinimalViewInfo View;
			if (!Test->TestTrue(TEXT("owning client uses actual replicated rod camera"),
				Camera && Camera->TryGetCameraView(1.0f / 60.0f, View))) return true;
			if (Stage >= 3)
				MaximumCameraStepDegrees = FMath::Max(MaximumCameraStepDegrees,
					FMath::RadiansToDegrees(PreviousCamera.AngularDistance(View.Rotation.Quaternion())));
			PreviousCamera = View.Rotation.Quaternion();
			FCatFishingRodRotationPrediction Prediction;
			if (!Test->TestTrue(TEXT("server reads production rotation state"),
				ServerRod->GetRotationPredictionFromAuthority(1.0 / 60.0, Prediction))) return true;
			const FQuat ExpectedClientGrip = ExpectedReplicatedGripRotation(*ServerRod);
			const double ClientGripErrorDegrees = FMath::RadiansToDegrees(ExpectedClientGrip.AngularDistance(
				ClientRod->GetGripWorldTransform().GetRotation()));
			const double CameraTargetErrorDegrees = FMath::RadiansToDegrees(ExpectedClientGrip.AngularDistance(View.Rotation.Quaternion()));
			LastWaitReason = FString::Printf(TEXT("Active=%d RequestedYaw=%.3f ServerYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f GripErrorDeg=%.3f CameraErrorDeg=%.3f AppliedLook=%.3f"),
				Prediction.Input.bCatDriveActive, Prediction.Input.RequestedAim.Yaw, Prediction.Input.CurrentAim.Yaw,
				ClientRod->GetGripWorldTransform().Rotator().Yaw, View.Rotation.Yaw, ClientGripErrorDegrees, CameraTargetErrorDegrees, StrokeAppliedYaw);

			if (Stage == 1 && ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestFalse(TEXT("no mouse input produces no active turning force before the first stroke"), Prediction.Input.bCatDriveActive);
				StartMouse(120.0, 1.0);
				Stage = 2;
				StageStarted = Now;
			}
			else if (Stage == 2 && Now - StageStarted > 2.0 && Prediction.Input.bCatDriveActive
				&& FMath::Abs(Prediction.Input.CurrentAim.Yaw - 30.0) < 0.2
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestTrue(TEXT("real sustained remote mouse input develops a resisted target gap"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(Prediction.Input.CurrentAim.Yaw, Prediction.Input.RequestedAim.Yaw)) > 60.0);
				StopMouse();
				Stage = 3;
				StageStarted = Now;
			}
			else if (Stage == 3 && !Prediction.Input.bCatDriveActive)
			{
				if (StoppedAt == 0.0)
				{
					StoppedAt = Now;
					StoppedAim = Prediction.Input.CurrentAim;
					StoppedEffort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
					Test->TestTrue(TEXT("remote stop clears the target immediately on authority"),
						Prediction.Input.RequestedAim.Equals(Prediction.Input.CurrentAim, 1e-8));
				}
				if (Now - StoppedAt < 0.6) return false;
				const auto Effort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				Test->TestEqual(TEXT("fish-driven motion after mouse stop has no cat intent fee"), Effort.ExertionSquaredSeconds, StoppedEffort.ExertionSquaredSeconds);
				Test->TestEqual(TEXT("fish-driven motion after mouse stop has no cat motion fee"), Effort.PositiveWorkRadians, StoppedEffort.PositiveWorkRadians);
				Test->TestTrue(TEXT("the fish can still rotate the stopped player's rod"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(StoppedAim.Yaw, Prediction.Input.CurrentAim.Yaw)) > 1.0);
				ServerRod->SetCarrierConstraintFromAuthority(FVector::ForwardVector, 0, 0, 0, 0, true, 0, 50);
				ServerRod->ForceNetUpdate();
				Stage = 4;
				StageStarted = Now;
			}
			else if (Stage == 4)
			{
				RemoteController->SetControlRotation(FRotator(0, 120, 0));
				ClientController->SetControlRotation(FRotator(0, 120, 0));
				if (!Test->TestTrue(TEXT("stale control rotation cannot activate or restore a stopped mouse target"),
					!Prediction.Input.bCatDriveActive && Prediction.Input.RequestedAim.Equals(Prediction.Input.CurrentAim, 1e-8))) return true;
				if (Now - StageStarted < 1.0 || Prediction.Input.PreviousAngularVelocityRadiansPerSecond.Size() > 0.003
					|| ClientGripErrorDegrees >= 0.05 || CameraTargetErrorDegrees >= 0.2) return false;
				FCatFishingRodAimSample ClientWrite;
				ClientWrite.RodActorId = RodId;
				ClientWrite.InputEpoch = ClientRod->GetCarrierConstraintState().AimInputEpoch;
				ClientWrite.Sequence = 1;
				const FTransform ClientBefore = ClientRod->GetActorTransform();
				Test->TestFalse(TEXT("client cannot rebase authority rod state"), ClientRod->CanRebaseHeldAimFromAuthority(ClientController->PlayerState, ClientWrite));
				Test->TestFalse(TEXT("client cannot submit an authority aim write locally"), ClientRod->AcceptHeldAimSampleFromAuthority(ClientController->PlayerState, ClientWrite));
				ClientRod->RebaseHeldAimFromAuthority(ClientController->PlayerState, ClientWrite, FGuid::NewGuid(), 1);
				Test->TestTrue(TEXT("rejected client writes have no local pose side effect"), ClientRod->GetActorTransform().Equals(ClientBefore));
				StrokeAnchor = Prediction.Input.CurrentAim;
				StartMouse(4.0, 1.0);
				Stage = 5;
				StageStarted = Now;
			}
			else if (Stage == 5 && Now - StageStarted > 0.4 && Prediction.Input.bCatDriveActive)
			{
				const double NewTargetDelta = FMath::FindDeltaAngleDegrees(StrokeAnchor.Yaw, Prediction.Input.RequestedAim.Yaw);
				if (!Test->TestTrue(TEXT("new remote stroke starts at actual rod pose and excludes the old large target"),
					NewTargetDelta > 3.8 && FMath::Abs(NewTargetDelta - StrokeAppliedYaw) < 0.3)) return true;
#if DO_ENABLE_NET_TEST
				PacketDriver = Client->GetNetDriver();
				if (!Test->TestNotNull(TEXT("client has a real NetDriver for packet-loss validation"), PacketDriver.Get())) return true;
				SavedPacketSimulation = PacketDriver->PacketSimulationSettings;
				FPacketSimulationSettings Drop = SavedPacketSimulation;
				Drop.PktLoss = 100;
				Drop.PktLossMinSize = 0;
				Drop.PktLossMaxSize = 0; // Zero means unlimited; MAX_int32 would overflow when converted to bits.
				PacketDriver->SetPacketSimulationSettings(Drop);
				bPacketSimulationChanged = true;
				if (!Test->TestTrue(TEXT("packet loss applies to the real connection without size exclusions"),
					PacketDriver->ServerConnection && PacketDriver->ServerConnection->PacketSimulationSettings.PktLoss == 100
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(8)
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(65536))) return true;
				PendingMouseYaw += 3.0;
				Stage = 6;
				StageStarted = Now;
#else
				Test->AddError(TEXT("Net packet simulation is unavailable; mouse timeout and new-stroke recovery remain unverified"));
				return true;
#endif
			}
			else if (Stage == 6 && Now - StageStarted > 0.35)
			{
				if (!Test->TestTrue(TEXT("missing ongoing samples times out active drive and discards its target"),
					!Prediction.Input.bCatDriveActive && Prediction.Input.RequestedAim.Equals(Prediction.Input.CurrentAim, 1e-8))) return true;
				StoppedEffort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				StopMouse(); // Its reliable stop is initially withheld too, then delivered after loss ends.
				Stage = 7;
				StageStarted = Now;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_loss_verified RodActorId=%s ServerYaw=%.3f Result=MouseDriveTimedOut"),
					*RodId.ToString(), Prediction.Input.CurrentAim.Yaw));
			}
			else if (Stage == 7 && Now - StageStarted > 0.2)
			{
				RestorePacketSimulation();
				Stage = 8;
				StageStarted = Now;
			}
			else if (Stage == 8 && Now - StageStarted > 0.6
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				if (!Test->TestTrue(TEXT("idle snapshots and delayed stop never recover discarded mouse debt"),
					!Prediction.Input.bCatDriveActive && Prediction.Input.RequestedAim.Equals(Prediction.Input.CurrentAim, 1e-8))) return true;
				const auto Effort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				Test->TestEqual(TEXT("idle recovery after packet loss does not charge active support"), Effort.ExertionSquaredSeconds, StoppedEffort.ExertionSquaredSeconds);
				Test->TestEqual(TEXT("idle recovery after packet loss does not charge active movement"), Effort.PositiveWorkRadians, StoppedEffort.PositiveWorkRadians);
				StrokeAnchor = Prediction.Input.CurrentAim;
				StartMouse(2.0, 0.5);
				Stage = 9;
				StageStarted = Now;
			}
			else if (Stage == 9 && Now - StageStarted > 0.3 && Prediction.Input.bCatDriveActive)
			{
				const double NewTargetDelta = FMath::FindDeltaAngleDegrees(StrokeAnchor.Yaw, Prediction.Input.RequestedAim.Yaw);
				if (!Test->TestTrue(TEXT("first stroke after loss uses only its own input from current actual pose"),
					NewTargetDelta > 1.8 && FMath::Abs(NewTargetDelta - StrokeAppliedYaw) < 0.3)) return true;
				StopMouse();
				Stage = 10;
				StageStarted = Now;
			}
			else if (Stage == 10 && Now - StageStarted > 0.8 && !Prediction.Input.bCatDriveActive
				&& ClientGripErrorDegrees < 0.05 && CameraTargetErrorDegrees < 0.2)
			{
				Test->TestTrue(TEXT("owning client receives the existing quantized grip pose after mouse start and stop"), ClientGripErrorDegrees < 0.05);
				Test->TestTrue(TEXT("client camera stays continuous across mouse stop, passive fish pull and new strokes"), MaximumCameraStepDegrees < 8.0);
				Test->TestTrue(TEXT("replication never restores the obsolete absolute controller target"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(Prediction.Input.CurrentAim.Yaw, 120.0)) > 60.0);
				Test->TestTrue(TEXT("final stop retains actual pose as its target"), Prediction.Input.RequestedAim.Equals(Prediction.Input.CurrentAim, 1e-8));
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_recovered RodActorId=%s RequestedYaw=%.3f ServerYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f GripErrorDegrees=%.3f MaximumCameraStepDegrees=%.3f Result=StoppedDebtDiscardedAndNewStrokeApplied"),
					*RodId.ToString(), Prediction.Input.RequestedAim.Yaw, Prediction.Input.CurrentAim.Yaw,
					ClientRod->GetGripWorldTransform().Rotator().Yaw, View.Rotation.Yaw, ClientGripErrorDegrees, MaximumCameraStepDegrees));
				return true;
			}
			return false;
		}

	private:
		void StartMouse(double InitialYaw, double Rate)
		{
			bInjectMouse = true;
			PendingMouseYaw = InitialYaw;
			MouseYawRate = Rate;
			StrokeAppliedYaw = 0.0;
		}
		void StopMouse() { bInjectMouse = false; PendingMouseYaw = 0.0; }
		bool WaitFor(const FString& Reason)
		{
			if (Reason != LastWaitReason)
			{
				LastWaitReason = Reason;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_wait Stage=%d %s"), Stage, *Reason));
			}
			return false;
		}
		static FQuat ExpectedReplicatedGripRotation(const ACatFishingRodActor& Rod)
		{
			// Quantize Actor axes first, then compose the unchanged non-identity canonical grip.
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
			if (bPacketSimulationChanged && PacketDriver.IsValid()) PacketDriver->SetPacketSimulationSettings(SavedPacketSimulation);
			bPacketSimulationChanged = false;
#endif
		}
		FAutomationTestBase* Test;
		double Started;
		double StageStarted = 0.0;
		double StoppedAt = 0.0;
		int32 Stage = 0;
		FString LastWaitReason;
		FGuid RodId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishingRodActor> ServerRod;
		TWeakObjectPtr<ACatfishingPlayerController> InputController;
		FDelegateHandle InputTickHandle;
		bool bInjectMouse = false;
		double PendingMouseYaw = 0.0;
		double MouseYawRate = 0.0;
		double StrokeAppliedYaw = 0.0;
		FRotator StoppedAim = FRotator::ZeroRotator;
		FRotator StrokeAnchor = FRotator::ZeroRotator;
		FCatFishingRodRotationEffortSnapshot StoppedEffort;
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
