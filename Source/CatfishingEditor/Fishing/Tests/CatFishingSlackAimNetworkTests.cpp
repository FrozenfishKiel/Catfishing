#include "Inventory/CatInventoryComponent.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Camera/CameraTypes.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Camp/CatCampHubActor.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Character/CatCharacter.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/CatUISettings.h"

namespace CatFishingSlackAimNetwork
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
			StableWorldHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda(
				[](UWorld* World, const UWorld::InitializationValues)
				{
					// This test duplicates an unsaved map into PIE; its World must be addressable along with the floor's native component.
					if (World && World->WorldType == EWorldType::PIE) World->bIsNameStableForNetworking = true;
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
			FWorldDelegates::OnPreWorldInitialization.Remove(StableWorldHandle);
			bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		bool bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle StableWorldHandle;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		using FGate = TFunction<void(ACatfishingGameModeBase*)>;
		using FGrant = TFunction<bool(ACatFishingRodActor*, APlayerState*)>;
		FVerify(FAutomationTestBase* InTest, FGate InGate, FGrant InGrant)
			: Test(InTest), Gate(MoveTemp(InGate)), Grant(MoveTemp(InGrant)), Started(FPlatformTime::Seconds())
		{
			// Feed each real Controller tick before its input pass. A one-shot direct call would be
			// followed by the next automatic zero-input frame, which must now send a mouse stop.
			InputTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda(
				[this](UWorld* World, ELevelTick, float DeltaSeconds)
				{
					ACatfishingPlayerController* Controller = InputController.Get();
					if (!Controller || Controller->GetWorld() != World || World->GetNetMode() != NM_Client) return;
					++ClientInputFrames;
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
			if (Now - Started > 75.0)
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
			auto* Mode = Server->GetAuthGameMode<ACatfishingGameModeBase>();
			if (!Mode) return WaitFor(TEXT("waiting for the production admission authority"));
			Gate(Mode);
			const double PhysicsNow = Server->GetTimeSeconds();
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

			if (!Mode->CanAcceptFishingCommand(RemoteController))
				return WaitFor(TEXT("waiting for the remote player's production fishing admission"));
			if (Stage == 0)
			{
				if (!bInputReady)
				{
					ULocalPlayer* LocalPlayer = ClientController->GetLocalPlayer();
					UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
					// Observe the real controller's UI setup. Refreshing it here rebuilds its
					// menu binding every frame and would prevent this readiness gate from settling.
					UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(ClientController->InputComponent);
					const UCatUISettings* UISettings = GetDefault<UCatUISettings>();
					const UInputAction* MenuAction = UISettings ? UISettings->LoadMainMenuToggleAction() : nullptr;
					uint32 MenuBinding = 0;
					if (Input && MenuAction)
						for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
							if (Binding && Binding->GetAction() == MenuAction) { MenuBinding = Binding->GetHandle(); break; }
					if (ClientController->AcknowledgedPawn != ClientCharacter || !UI || !ClientCharacter->GetInventoryComponent() || !ClientCharacter->GetInventoryComponent()->GetInventoryModel()
						|| !UI->GetInventoryPageController() || MenuBinding == 0)
					{
						ReadyInputComponent.Reset();
						return WaitFor(TEXT("waiting for acknowledged pawn, formal inventory UI and bound menu input"));
					}
					if (ReadyInputComponent.Get() != Input || ReadyMenuBinding != MenuBinding)
					{
						ReadyInputComponent = Input;
						ReadyMenuBinding = MenuBinding;
						ReadyInputFrame = ClientInputFrames;
						return WaitFor(TEXT("formal UI input is bound; observing real input passes before physical setup"));
					}
					// GameOnly setup may queue an input flush for the next real input pass. Do not
					// create a grip before those passes, or suppress the production flush to keep it.
					if (ClientInputFrames - ReadyInputFrame < 3) return false;
					bInputReady = true;
					Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_input_ready Controller=%s Pawn=%s MenuBinding=%u InputFrames=%u Result=FormalUIBoundAndProcessed"),
						*GetNameSafe(ClientController), *GetNameSafe(ClientCharacter), MenuBinding, ClientInputFrames - ReadyInputFrame));
				}
				if (!bBodyPlaced)
				{
					if (!Test->TestTrue(TEXT("place all three authority cat bodies on real ground"),
						ServerCharacter->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(
							FTransform(FRotator::ZeroRotator, FVector(0, 0, ServerCharacter->GetBodyStandRootHeightCm())), TEXT("SlackAimNetworkSetup")))) return true;
					ServerCharacter->GetCatAbilitySystemComponent()->SetNumericAttributeBase(
						UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
					if (!Test->TestTrue(TEXT("initialize the actual holder's available motor budget"),
						ServerCharacter->GetCatAbilitySystemComponent()->InitializeFishingStaminaForSession())) return true;
					ServerCharacter->GetPhysicalBodyComponent()->SetViewIntent(FRotator::ZeroRotator);
					RemoteController->SetControlRotation(FRotator::ZeroRotator);
					ClientController->SetControlRotation(FRotator::ZeroRotator);
					ClientController->SetViewTarget(ClientCharacter);
					bBodyPlaced = true;
					StageStarted = PhysicsNow;
					return false;
				}
				if (PhysicsNow - StageStarted < 0.6 || !ServerCharacter->GetPhysicalBodyComponent()->IsGrounded()
					|| ClientCharacter->GetPhysicalBodyComponent()->GetControlEpoch() != ServerCharacter->GetPhysicalBodyComponent()->GetControlEpoch()
					|| FVector::Distance(ClientCharacter->GetActorLocation(), ServerCharacter->GetActorLocation()) > 3.0) return false;
				UClass* RodClass = LoadClass<ACatFishingRodActor>(nullptr,
					TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"));
				auto* Fishing = Server->GetSubsystem<UCatFishingService>();
				if (!Test->TestNotNull(TEXT("formal rod Blueprint class"), RodClass)
					|| !Test->TestNotNull(TEXT("authority fishing service"), Fishing)) return true;
				const FTransform SpawnTransform(ServerCharacter->GetActorLocation());
				ServerRod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform);
				if (!Test->TestNotNull(TEXT("spawn formal rod on server"), ServerRod.Get())) return true;
				if (!Test->TestTrue(TEXT("canonical non-identity grip configured before initial replication"),
					ServerRod->ConfigureCanonicalAnchorsFromAuthority(FTransform(FVector(110, 0, 0)),
						FTransform::Identity, FTransform(FRotator(5, 20, 0), FVector(13, 7, 4))))
					|| !Test->TestTrue(TEXT("initialize deployed identity before establishing the physical holder"),
						ServerRod->InitializeAuthoritativeIdentity(RodId, FGuid::NewGuid(), TEXT("MouseDriveNetworkRod"), NAME_None,
							RemoteController->PlayerState, nullptr, true, false))) return true;
				ServerRod->bAlwaysRelevant = true;
				ServerRod->FinishSpawning(SpawnTransform);
				if (!Test->TestTrue(TEXT("register the production RPC lookup"),
					Fishing->RegisterDeployedRod(RemoteController->PlayerState, ServerRod.Get()))
					|| !Test->TestTrue(TEXT("new rod is positioned at the real hand and passes production contact validation"),
						ServerRod->BeginPhysicalHoldFromAuthority(RemoteController->PlayerState, true))
					|| !Test->TestTrue(TEXT("owner control is granted explicitly after the real physical hold"), Grant(ServerRod.Get(), RemoteController->PlayerState))
					// This protocol fixture has no fish Session; finish the same physical-source commit
					// only after its explicit owner grant, as the successful Service transaction does.
					|| !Test->TestTrue(TEXT("successful owner grant adopts the existing physical hold"),
						ServerRod->GetPhysicalRodComponent()->CommitPrimaryHold(RemoteController->PlayerState))
					|| !Test->TestTrue(TEXT("initialize actual held direction"), ServerRod->RefreshHeldTransformFromAuthority())
					|| !Test->TestTrue(TEXT("start only the mouse-input fight domain without a virtual force"),
						ServerRod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 0, 0, true, 0, 50))) return true;
				ServerRod->ForceNetUpdate();
				Stage = 1;
				StageStarted = PhysicsNow;
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
			FCatFishingRodControlObservation Observation;
			if (!Test->TestTrue(TEXT("server reads production rotation state"),
				ServerRod->GetControlObservationFromAuthority(Observation))) return true;
			const FQuat ServerGrip = ServerRod->GetGripWorldTransform().GetRotation();
			const double ClientGripErrorDegrees = FMath::RadiansToDegrees(ServerGrip.AngularDistance(
				ClientRod->GetGripWorldTransform().GetRotation()));
			FRotator CameraTarget = ClientRod->GetGripWorldTransform().Rotator();
			CameraTarget.Roll = 0.0;
			const double CameraTargetErrorDegrees = FMath::RadiansToDegrees(CameraTarget.Quaternion().AngularDistance(View.Rotation.Quaternion()));
			if (!Test->TestTrue(TEXT("actual server, replicated client and camera poses remain finite"),
				!Observation.ActualAim.ContainsNaN() && !ClientRod->GetActorTransform().ContainsNaN()
				&& !View.Location.ContainsNaN() && !View.Rotation.ContainsNaN())) return true;
			if (!Test->TestTrue(TEXT("physical hand connection survives the entire mouse protocol"),
				ServerCharacter->GetPhysicalBodyComponent()->GetGrab()->GetGripTarget(true) == ServerRod.Get())) return true;
			LastWaitReason = FString::Printf(TEXT("Active=%d RequestedYaw=%.3f ServerYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f GripErrorDeg=%.3f CameraErrorDeg=%.3f AppliedLook=%.3f"),
				Observation.bMouseDriveActive, Observation.RequestedAim.Yaw, Observation.ActualAim.Yaw,
				ClientRod->GetGripWorldTransform().Rotator().Yaw, View.Rotation.Yaw, ClientGripErrorDegrees, CameraTargetErrorDegrees, StrokeAppliedYaw);

			if (Stage == 1 && PhysicsNow - StageStarted > 0.8 && ClientGripErrorDegrees < 3.0 && CameraTargetErrorDegrees < 3.0)
			{
				Test->TestFalse(TEXT("no mouse input produces no active turning force before the first stroke"), Observation.bMouseDriveActive);
				StrokeAnchor = Observation.ActualAim;
				StartMouse(120.0, 1.0);
				Stage = 2;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 2 && PhysicsNow - StageStarted > 0.6 && Observation.bMouseDriveActive)
			{
				Test->TestTrue(TEXT("real sustained remote mouse input rotates the physical rod"),
					FMath::RadiansToDegrees(StrokeAnchor.Quaternion().AngularDistance(ServerGrip)) > 2.0);
				Test->TestTrue(TEXT("the real mouse motor records active physical effort"),
					ServerRod->GetAuthoritativeRotationEffortSnapshot().ExertionSquaredSeconds > 0.0);
				StopMouse();
				Stage = 3;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 3 && !Observation.bMouseDriveActive)
			{
				if (StoppedAt == 0.0)
				{
					StoppedAt = PhysicsNow;
					StoppedAim = Observation.ActualAim;
					StoppedEffort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
					Test->TestTrue(TEXT("remote reliable stop immediately discards the authority target"),
						Observation.RequestedAim.Equals(Observation.ActualAim, 1e-8));
					// This protocol fixture supplies the same normalized rotational load observation as
					// the Runner. Real Session force/impulse delivery is covered by the formal Runner test.
					ServerRod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, .5, 0, true,
						30, 50, ServerGrip.GetRightVector());
				}
				if (PhysicsNow - StoppedAt < 0.6) return false;
				const auto Effort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				Test->TestEqual(TEXT("passive motion after mouse stop has no cat intent fee"), Effort.ExertionSquaredSeconds, StoppedEffort.ExertionSquaredSeconds);
				Test->TestEqual(TEXT("passive motion after mouse stop has no cat motion fee"), Effort.PositiveWorkRadians, StoppedEffort.PositiveWorkRadians);
				Test->TestTrue(TEXT("a stopped player's controlled rod still responds to the original fish resistance"),
					FMath::RadiansToDegrees(StoppedAim.Quaternion().AngularDistance(ServerGrip)) > 1.0);
				Stage = 4;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 4)
			{
				RemoteController->SetControlRotation(FRotator(0, 120, 0));
				ClientController->SetControlRotation(FRotator(0, 120, 0));
				if (!Test->TestTrue(TEXT("stale control rotation cannot activate or restore a stopped mouse target"),
					!Observation.bMouseDriveActive && Observation.RequestedAim.Equals(Observation.ActualAim, 1e-8))) return true;
				if (PhysicsNow - StageStarted < 0.8 || ClientGripErrorDegrees >= 3.0 || CameraTargetErrorDegrees >= 3.0) return false;
				FCatFishingRodAimSample ClientWrite;
				ClientWrite.RodActorId = RodId;
				ClientWrite.InputEpoch = ClientRod->GetCarrierConstraintState().AimInputEpoch;
				ClientWrite.Sequence = 1;
				const FTransform ClientBefore = ClientRod->GetActorTransform();
				Test->TestFalse(TEXT("client cannot rebase authority rod state"), ClientRod->CanRebaseHeldAimFromAuthority(ClientController->PlayerState, ClientWrite));
				Test->TestFalse(TEXT("client cannot submit an authority aim write locally"), ClientRod->AcceptHeldAimSampleFromAuthority(ClientController->PlayerState, ClientWrite));
				ClientRod->RebaseHeldAimFromAuthority(ClientController->PlayerState, ClientWrite, FGuid::NewGuid(), 1);
				Test->TestTrue(TEXT("rejected client writes have no local pose side effect"), ClientRod->GetActorTransform().Equals(ClientBefore));
				StrokeAnchor = Observation.ActualAim;
				StartMouse(4.0, 1.0);
				Stage = 5;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 5 && PhysicsNow - StageStarted > 0.3 && Observation.bMouseDriveActive)
			{
				const double NewTargetDelta = FMath::FindDeltaAngleDegrees(StrokeAnchor.Yaw, Observation.RequestedAim.Yaw);
				if (!Test->TestTrue(TEXT("new remote stroke starts near the actual rod pose and excludes the old large target"),
					NewTargetDelta > 1.0 && FMath::Abs(NewTargetDelta - StrokeAppliedYaw) < 3.0)) return true;
#if DO_ENABLE_NET_TEST
				PacketDriver = Client->GetNetDriver();
				if (!Test->TestNotNull(TEXT("client has a real NetDriver for packet-loss validation"), PacketDriver.Get())) return true;
				SavedPacketSimulation = PacketDriver->PacketSimulationSettings;
				FPacketSimulationSettings Drop = SavedPacketSimulation;
				Drop.PktLoss = 100;
				Drop.PktLossMinSize = 0;
				Drop.PktLossMaxSize = 0;
				PacketDriver->SetPacketSimulationSettings(Drop);
				bPacketSimulationChanged = true;
				if (!Test->TestTrue(TEXT("packet loss applies to the real connection without size exclusions"),
					PacketDriver->ServerConnection && PacketDriver->ServerConnection->PacketSimulationSettings.PktLoss == 100
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(8)
					&& PacketDriver->ServerConnection->PacketSimulationSettings.ShouldDropPacketOfSize(65536))) return true;
				PendingMouseYaw += 3.0;
				Stage = 6;
				StageStarted = PhysicsNow;
#else
				Test->AddError(TEXT("Net packet simulation is unavailable; mouse timeout and new-stroke recovery remain unverified"));
				return true;
#endif
			}
			else if (Stage == 6 && PhysicsNow - StageStarted > 0.22)
			{
				if (!Test->TestTrue(TEXT("missing ongoing samples times out active drive and discards its target"),
					!Observation.bMouseDriveActive && Observation.RequestedAim.Equals(Observation.ActualAim, 1e-8))) return true;
				StoppedEffort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				StopMouse(); // The reliable stop is queued during loss and delivered after restoration.
				Stage = 7;
				StageStarted = PhysicsNow;
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_loss_verified RodActorId=%s ServerYaw=%.3f Result=MouseDriveTimedOutGripRetained"),
					*RodId.ToString(), Observation.ActualAim.Yaw));
			}
			else if (Stage == 7 && PhysicsNow - StageStarted > 0.05)
			{
				RestorePacketSimulation();
				Stage = 8;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 8 && PhysicsNow - StageStarted > 0.6 && ClientGripErrorDegrees < 3.0 && CameraTargetErrorDegrees < 3.0)
			{
				if (!Test->TestTrue(TEXT("idle snapshots and delayed stop never recover discarded mouse debt"),
					!Observation.bMouseDriveActive && Observation.RequestedAim.Equals(Observation.ActualAim, 1e-8))) return true;
				const auto Effort = ServerRod->GetAuthoritativeRotationEffortSnapshot();
				Test->TestEqual(TEXT("idle recovery after packet loss does not charge active support"), Effort.ExertionSquaredSeconds, StoppedEffort.ExertionSquaredSeconds);
				Test->TestEqual(TEXT("idle recovery after packet loss does not charge active movement"), Effort.PositiveWorkRadians, StoppedEffort.PositiveWorkRadians);
				StrokeAnchor = Observation.ActualAim;
				StartMouse(2.0, 0.5);
				Stage = 9;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 9 && PhysicsNow - StageStarted > 0.3 && Observation.bMouseDriveActive)
			{
				const double NewTargetDelta = FMath::FindDeltaAngleDegrees(StrokeAnchor.Yaw, Observation.RequestedAim.Yaw);
				if (!Test->TestTrue(TEXT("first stroke after loss uses only its own input from current actual pose"),
					NewTargetDelta > 0.5 && FMath::Abs(NewTargetDelta - StrokeAppliedYaw) < 2.0)) return true;
				StopMouse();
				Stage = 10;
				StageStarted = PhysicsNow;
			}
			else if (Stage == 10 && PhysicsNow - StageStarted > 0.8 && !Observation.bMouseDriveActive
				&& ClientGripErrorDegrees < 3.0 && CameraTargetErrorDegrees < 3.0)
			{
				Test->TestTrue(TEXT("owning client receives the actual physical grip pose after mouse start and stop"), ClientGripErrorDegrees < 3.0);
				Test->TestTrue(TEXT("client camera stays continuous across stop, passive load and new strokes"), MaximumCameraStepDegrees < 8.0);
				Test->TestTrue(TEXT("the fight camera horizon remains level"), FMath::Abs(View.Rotation.Roll) < 0.01);
				Test->TestTrue(TEXT("final stop retains actual pose as its target"), Observation.RequestedAim.Equals(Observation.ActualAim, 1e-8));
				Test->AddInfo(FString::Printf(TEXT("Event=slack_aim_network_recovered RodActorId=%s RequestedYaw=%.3f ServerYaw=%.3f ClientYaw=%.3f CameraYaw=%.3f GripErrorDegrees=%.3f MaximumCameraStepDegrees=%.3f Result=StoppedDebtDiscardedAndNewStrokeApplied Evidence=runtime_behavior"),
					*RodId.ToString(), Observation.RequestedAim.Yaw, Observation.ActualAim.Yaw,
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
		void RestorePacketSimulation()
		{
#if DO_ENABLE_NET_TEST
			if (bPacketSimulationChanged && PacketDriver.IsValid()) PacketDriver->SetPacketSimulationSettings(SavedPacketSimulation);
			bPacketSimulationChanged = false;
#endif
		}
		FAutomationTestBase* Test;
		FGate Gate;
		FGrant Grant;
		bool bBodyPlaced = false;
		bool bInputReady = false;
		uint32 ClientInputFrames = 0;
		uint32 ReadyInputFrame = 0;
		uint32 ReadyMenuBinding = 0;
		TWeakObjectPtr<UEnhancedInputComponent> ReadyInputComponent;
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
	Map->bIsNameStableForNetworking = true;
	Map->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("engine cube supplies actual ground collision"), Cube)) return false;
	AStaticMeshActor* Ground = Map->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("native floor actor exists"), Ground)) return false;
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Ground->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, -10), FVector(100, 100, 0.2)));
	Map->SpawnActor<ACatCampHubActor>(FVector(-600, 0, 100), FRotator::ZeroRotator);
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishingSlackAimNetwork::FVerify>(this,
		[](ACatfishingGameModeBase* Mode)
		{
			Mode->bRunCommandsOpen = true;
			Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
			Mode->RunPublicState.Phase.bFishingAllowed = true;
		},
		[](ACatFishingRodActor* Rod, APlayerState* Player)
		{ return Rod->SetPrimaryOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision); }));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
