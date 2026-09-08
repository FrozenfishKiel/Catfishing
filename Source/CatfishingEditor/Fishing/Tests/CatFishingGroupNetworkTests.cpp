#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Environment/CatWaterBoundarySplineActor.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/CatFishingViewTypes.h"

namespace CatFishingGroupNetwork
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
		using FSend = TFunction<void(UCatFishingCommandComponent*, FCatFishingInputEdge)>;
		FVerify(FAutomationTestBase* InTest, FGate InGate, FSend InSend)
			: Test(InTest), Gate(MoveTemp(InGate)), SendStale(MoveTemp(InSend)), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Four-player fishing network timeout Stage=%d Join=%d; no network verdict"), Stage, JoinIndex));
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
			Clients.Sort([](const UWorld& A, const UWorld& B) { return A.GetName() < B.GetName(); });
			if (!Server || Clients.Num() != 3) return false;
			ACatfishingGameModeBase* Mode = Server->GetAuthGameMode<ACatfishingGameModeBase>();
			if (!Mode) return false;
			Gate(Mode);
			ACatfishingPlayerController* Primary = Cast<ACatfishingPlayerController>(Server->GetFirstPlayerController());
			ACatCharacter* PrimaryCat = Primary ? Cast<ACatCharacter>(Primary->GetPawn()) : nullptr;
			TArray<ACatfishingPlayerController*> LocalClients, RemoteControllers;
			for (UWorld* Client : Clients)
			{
				auto* Local = Cast<ACatfishingPlayerController>(Client->GetFirstPlayerController());
				if (!Local || !Local->PlayerState || !Local->GetPawn()) return false;
				ACatfishingPlayerController* Remote = nullptr;
				for (TActorIterator<ACatfishingPlayerController> It(Server); It; ++It)
					if (!It->IsLocalController() && It->PlayerState
						&& It->PlayerState->GetPlayerId() == Local->PlayerState->GetPlayerId()) Remote = *It;
				if (!Remote || !Remote->GetPawn()) return false;
				LocalClients.Add(Local);
				RemoteControllers.Add(Remote);
			}
			if (!PrimaryCat || !Primary->PlayerState || !Mode->CanAcceptFishingCommand(Primary)) return false;
			UCatFishingService* Fishing = Server->GetSubsystem<UCatFishingService>();
			if (!Fishing) return false;
			if (Stage == 0)
			{
				PrimaryCat->SetActorLocation(FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
				PrimaryCat->GetCharacterMovement()->StopMovementImmediately();
				PrimaryCat->ForceNetUpdate();
				Primary->SetControlRotation(FRotator::ZeroRotator);
				TArray<ACatfishingPlayerController*> Everyone = RemoteControllers;
				Everyone.Add(Primary);
				double MaximumCapsuleRadius = 0.0;
				for (auto* Controller : Everyone)
				{
					auto* Cat = Cast<ACatCharacter>(Controller->GetPawn());
					MaximumCapsuleRadius = FMath::Max(MaximumCapsuleRadius, static_cast<double>(Cat->GetCapsuleComponent()->GetScaledCapsuleRadius()));
					Cat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
					if (!Test->TestTrue(TEXT("each network participant receives their formal stamina baseline"),
						Cat->GetCatAbilitySystemComponent()->InitializeFishingStaminaForSession())) return true;
				}
				Equipment = PrimaryCat->GetEquipmentComponent();
				if (!Equipment.IsValid()) return true;
				for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
					Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id);
				Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 4);
				FCatPlaceRodCommand Place;
				Place.RequestId = FGuid::NewGuid();
				Place.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
				const auto Placed = Fishing->PlaceRod(Primary, Place);
				if (!Test->TestTrue(TEXT("real Service places the formal rod"), Placed.bCommitted)) return true;
				RodId = Placed.RodActorId;
				Rod = Fishing->FindDeployedRodById(RodId);
				if (!Rod.IsValid()) return true;
				RodItemId = Rod->GetPresentationState().ItemInstanceId;
				const double Spacing = 2.0 * MaximumCapsuleRadius + 32.0;
				const FVector Offsets[] = {FVector(-Spacing, -Spacing, 0), FVector(-Spacing, Spacing, 0), FVector(0, 2.0 * Spacing, 0)};
				for (int32 Index = 0; Index < RemoteControllers.Num(); ++Index)
				{
					auto* Cat = Cast<ACatCharacter>(RemoteControllers[Index]->GetPawn());
					FVector Position = PrimaryCat->GetActorLocation() + Offsets[Index];
					Position.Z = Cat->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.2;
					if (!Test->TestTrue(TEXT("non-overlapping helper formation remains inside the real rod interaction range"),
						FVector::DistSquared(Position, Rod->GetOperatorInteractionWorldTransform().GetLocation()) < FMath::Square(250.0))) return true;
					Cat->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
					Cat->GetCharacterMovement()->StopMovementImmediately();
					Cat->ForceNetUpdate();
				}
				Stage = 1;
				StageStarted = Now;
			}
			TArray<ACatFishingRodActor*> ClientRods;
			for (UWorld* Client : Clients)
			{
				ACatFishingRodActor* Found = nullptr;
				for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
					if (It->GetPresentationState().RodActorId == RodId) Found = *It;
				if (!Found || Found->GetControlEpoch() == 0) return false;
				ClientRods.Add(Found);
			}
			if (Stage == 1)
			{
				if (!bFormationReady)
				{
					FString Pending;
					for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
					{
						auto* LocalCat = Cast<ACatCharacter>(LocalClients[Index]->GetPawn());
						auto* AuthorityCat = Cast<ACatCharacter>(RemoteControllers[Index]->GetPawn());
						const double PositionError = FVector::Distance(LocalCat->GetActorLocation(), AuthorityCat->GetActorLocation());
						const bool bLocalGrounded = LocalCat->GetCharacterMovement()->IsMovingOnGround() && LocalCat->GetMovementBase();
						const bool bAuthorityGrounded = AuthorityCat->GetCharacterMovement()->IsMovingOnGround() && AuthorityCat->GetMovementBase();
						if (PositionError > 5.0 || !bLocalGrounded || !bAuthorityGrounded)
							Pending += FString::Printf(TEXT(" PlayerId=%d Authority=%s Client=%s ErrorCm=%.3f AuthorityGrounded=%d ClientGrounded=%d;"),
								RemoteControllers[Index]->PlayerState->GetPlayerId(), *AuthorityCat->GetActorLocation().ToCompactString(),
								*LocalCat->GetActorLocation().ToCompactString(), PositionError, bAuthorityGrounded, bLocalGrounded);
					}
					if (!Pending.IsEmpty())
					{
						if (Now - StageStarted < 10.0) return false;
						Test->AddError(TEXT("Network formation failed to settle and converge within 10 seconds:") + Pending);
						return true;
					}
					bFormationReady = true;
					Test->AddInfo(TEXT("Event=fishing_group_network_formation_converged Clients=3 MaximumPositionErrorCm=5 Grounded=1"));
				}
				if (JoinRequest.IsValid())
				{
					FCatFishingCommandResult Result;
					if (!LocalClients[JoinIndex]->GetFishingCommandComponent()->TryGetResult(JoinRequest, Result)) return false;
					if (!Test->TestTrue(TEXT("remote helper receives real join RPC receipt"), Result.bCommitted)) return true;
					JoinRequest.Invalidate();
					++JoinIndex;
				}
				if (JoinIndex < 3)
				{
					if (ClientRods[JoinIndex]->GetPresentationState().RodActorRevision != Rod->GetPresentationState().RodActorRevision) return false;
					FCatOperateRodCommand Join;
					Join.Context.RequestId = JoinRequest = FGuid::NewGuid();
					Join.Context.RodActorId = RodId;
					Join.Context.ExpectedRodActorRevision = ClientRods[JoinIndex]->GetPresentationState().RodActorRevision;
					LocalClients[JoinIndex]->GetFishingCommandComponent()->SubmitOperateRod(Join);
					return false;
				}
				for (ACatFishingRodActor* ClientRod : ClientRods) if (ClientRod->GetOperatorCount() != 4) return false;
				auto* Region = Server->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
				auto* Boundary = Server->SpawnActor<ACatWaterBoundarySplineActor>();
				USplineComponent* Spline = Boundary ? Boundary->FindComponentByClass<USplineComponent>() : nullptr;
				if (!Test->TestTrue(TEXT("real region and boundary authoring actors exist"), Region && Spline)) return true;
				const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
				for (const auto& Reference : Catalog->Definitions)
				{
					const UCatFishDefinition* Definition = Reference.LoadSynchronous();
					if (Definition && Catalog->FindRuntimeDefinition(Definition->FishDefinitionId) == Definition
						&& !Definition->RegionIds.IsEmpty())
					{
						Region->RegionId = Definition->RegionIds[0];
						break;
					}
				}
				if (!Test->TestFalse(TEXT("formal catalog supplies an eligible water region"), Region->RegionId.IsNone())) return true;
				Region->WaterPointVerticalToleranceCm = 100;
				Region->BankHeightToleranceCm = 50;
				Region->BoundaryToleranceCm = 1;
				Region->MaxLandingCorrectionCm = 100;
				Region->MinimumWaterInsetCm = 1;
				Region->BoundaryActors.Add(Boundary);
				Boundary->BoundaryId = TEXT("GroupNetworkShore");
				Boundary->OwningRegion = Region;
				Spline->SetSplinePoints({FVector(400,-5000,0), FVector(10000,-5000,0),
					FVector(10000,5000,0), FVector(400,5000,0)}, ESplineCoordinateSpace::World, false);
				for (int32 Point = 0; Point < Spline->GetNumberOfSplinePoints(); ++Point)
					Spline->SetSplinePointType(Point, ESplinePointType::Linear, false);
				Spline->SetClosedLoop(true, true);
				Region->BakeGeometry();
				if (!Test->TestTrue(TEXT("production water authoring bakes valid geometry"), Region->HasValidBakedGeometry())) return true;
				Region->FinishSpawning(FTransform::Identity);
				FCatBeginCastCommand Cast;
				Cast.RequestId = FGuid::NewGuid();
				Cast.RodActorId = RodId;
				Cast.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
				Cast.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
				Cast.ExpectedWaterRegionHandle = Region->GetWaterRegionHandle();
				Cast.ClientCandidateWorldPoint = FVector(650, 0, 0);
				const auto Begun = Fishing->BeginCast(Primary, Cast);
				if (!Test->TestTrue(TEXT("real Service creates the formal cast session"), Begun.Command.bCommitted)) return true;
				SessionId = Begun.Command.FishingSessionId;
				Session = Fishing->FindSession(SessionId);
				Stage = 2;
			}
			if (!Session.IsValid() || Session->IsTerminal()) { Test->AddError(TEXT("group network session ended before verdict")); return true; }
			if (Stage == 2)
			{
				if (Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow) return false;
				if (!Test->TestTrue(TEXT("formal bite starts the actual fight runner"), Session->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted)) return true;
				if (!Test->TestTrue(TEXT("hook receipt includes an actual selected fish in HookedFight"),
					Session->GetSnapshot().Phase == ECatFishingPhase::HookedFight
					&& IsValid(Session->GetSnapshot().FishEncounterActor))) return true;
				Stage = 3;
				StageStarted = Now;
				for (auto* Remote : RemoteControllers) InitialHelperPositions.Add(Remote->GetPawn()->GetActorLocation());
				InitialRodPosition = Rod->GetActorLocation();
				InitialFishPosition = Session->GetSnapshot().FishEncounterActor->GetActorLocation();
			}
			TArray<ACatFishingSession*> ClientSessions;
			for (int32 Index = 0; Index < Clients.Num(); ++Index)
			{
				ACatFishingSession* Found = UCatFishingViewBridge::FindFishingSessionForPlayerState(Clients[Index], LocalClients[Index]->PlayerState);
				if (!Found || Found->GetSnapshot().FishingSessionId != SessionId) return false;
				ClientSessions.Add(Found);
			}
			if (Stage == 3)
			{
				for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
					LocalClients[Index]->GetPawn()->AddMovementInput(Index == 2 ? FVector::ForwardVector : -FVector::ForwardVector);
				PrimaryCat->AddMovementInput(-FVector::ForwardVector);
				++FightSamples;
				const double LineLoad = Session->GetSnapshot().NormalizedLineLoad;
				if (!FMath::IsFinite(LineLoad) || Rod->GetActorLocation().ContainsNaN()
					|| Session->GetSnapshot().FishEncounterActor->GetActorLocation().ContainsNaN())
				{
					Test->AddError(TEXT("live four-player fight produced a non-finite position or line load"));
					return true;
				}
				MaximumSampledLineLoad = FMath::Max(MaximumSampledLineLoad, LineLoad);
				bool bAllMoveAccepted = true;
				for (auto* Remote : RemoteControllers)
				{
					auto* Movement = Cast<UCatCharacterMovementComponent>(Cast<ACatCharacter>(Remote->GetPawn())->GetCharacterMovement());
					bAllMoveAccepted &= Movement && Movement->GetAcceptedFishingMoveIntent().SizeSquared() > 0.01;
				}
				if (!bAllMoveAccepted || Now - StageStarted < 0.5) return false;
				for (ACatFishingSession* ClientSession : ClientSessions)
					if (ClientSession->GetSnapshot().FightParticipantCount != 4 || ClientSession->GetSnapshot().CombinedFightStaminaMaximum <= 0) return false;
				for (int32 Index = 0; Index < RemoteControllers.Num(); ++Index)
					if (!Test->TestTrue(FString::Printf(TEXT("each remote helper moves in the authority CMC world PlayerId=%d Initial=%s Current=%s"),
						RemoteControllers[Index]->PlayerState->GetPlayerId(), *InitialHelperPositions[Index].ToCompactString(),
						*RemoteControllers[Index]->GetPawn()->GetActorLocation().ToCompactString()),
						FVector::DistSquared(InitialHelperPositions[Index], RemoteControllers[Index]->GetPawn()->GetActorLocation()) > 1.0)) return true;
				SampledRodTravel = FVector::Distance(InitialRodPosition, Rod->GetActorLocation());
				SampledFishTravel = FVector::Distance(InitialFishPosition, Session->GetSnapshot().FishEncounterActor->GetActorLocation());
				OldEpoch = Rod->GetControlEpoch();
				FCatLeaveRodCommand Leave;
				Leave.Context.RequestId = FGuid::NewGuid();
				Leave.Context.RodActorId = RodId;
				Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
				if (!Test->TestTrue(TEXT("host primary leaves the live four-person fight"), Fishing->LeaveRod(Primary, Leave).bCommitted)) return true;
				Stage = 4;
			}
			if (Stage == 4)
			{
				for (int32 Index = 0; Index < ClientRods.Num(); ++Index)
					if (ClientRods[Index]->GetOperatorCount() != 3 || ClientRods[Index]->GetControlEpoch() == OldEpoch
						|| ClientSessions[Index]->GetSnapshot().FightParticipantCount != 3) return false;
				if (!Test->TestEqual(TEXT("earliest remote helper receives control"), Rod->GetPresentationState().OperatorPlayerState.Get(), RemoteControllers[0]->PlayerState.Get())) return true;
				FCatFishingInputEdge Stale;
				Stale.RequestId = StaleRequest = FGuid::NewGuid();
				Stale.InputSequence = 1000;
				Stale.ControlRodActorId = RodId;
				Stale.ControlEpoch = OldEpoch;
				Test->AddExpectedErrorPlain(TEXT("Event=fishing_control_input_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
				Test->AddExpectedErrorPlain(TEXT("Error=ECatFishingCommandError::InputSequenceStale"), EAutomationExpectedErrorFlags::Contains, 1);
				SendStale(LocalClients[0]->GetFishingCommandComponent(), Stale);
				Stage = 5;
			}
			if (Stage == 5)
			{
				FCatFishingCommandResult Result;
				if (!LocalClients[0]->GetFishingCommandComponent()->TryGetResult(StaleRequest, Result)) return false;
				Test->TestTrue(TEXT("late pre-promotion input is rejected across real RPC"), !Result.bCommitted && Result.Error == ECatFishingCommandError::InputSequenceStale);
				const auto View = FCatFishingViewState::FromSnapshot(ClientSessions[0]->GetSnapshot());
				Test->TestEqual(TEXT("new primary UI sees three live member balances"), View.FightParticipantCount, 3);
				Test->TestTrue(TEXT("new primary UI uses replicated total stamina"), View.CombinedFightStamina >= 0 && View.CombinedFightStamina <= View.CombinedFightStaminaMaximum);
				FCatInventoryEndpointSnapshot Locked;
				Test->TestEqual(TEXT("handoff keeps the original rod resource lock"), Equipment->ReadInventoryTransferEndpoint(TEXT("ActiveUse"), RodItemId, Locked), ECatDomainCommandError::InvalidPhase);
				if (Test->HasAnyErrors()) return true;
				Test->AddInfo(FString::Printf(TEXT("Event=fishing_group_network_verified SessionId=%s RodActorId=%s Members=3 PreviousMembers=4 ControlEpoch=%u OldControlEpoch=%u TotalStamina=%.3f MaximumStamina=%.3f LineLoad=%.3f Samples=%d MaximumLineLoad=%.3f RodTravelCm=%.3f FishTravelCm=%.3f Server=Listen Clients=3 Evidence=runtime_behavior"),
					*SessionId.ToString(), *RodId.ToString(), Rod->GetControlEpoch(), OldEpoch,
					View.CombinedFightStamina, View.CombinedFightStaminaMaximum, Session->GetSnapshot().NormalizedLineLoad,
					FightSamples, MaximumSampledLineLoad, SampledRodTravel, SampledFishTravel));
				Test->AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
				Session->CancelFromAuthority(FGuid::NewGuid());
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		FGate Gate;
		FSend SendStale;
		double Started, StageStarted = 0;
		int32 Stage = 0, JoinIndex = 0;
		bool bFormationReady = false;
		int32 FightSamples = 0;
		double MaximumSampledLineLoad = 0.0, SampledRodTravel = 0.0, SampledFishTravel = 0.0;
		FVector InitialRodPosition = FVector::ZeroVector, InitialFishPosition = FVector::ZeroVector;
		TArray<FVector> InitialHelperPositions;
		uint32 OldEpoch = 0;
		FGuid RodId, RodItemId, SessionId, JoinRequest, StaleRequest;
		TWeakObjectPtr<ACatFishingRodActor> Rod;
		TWeakObjectPtr<ACatFishingSession> Session;
		TWeakObjectPtr<UCatEquipmentComponent> Equipment;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupNetworkTest,
	"Catfishing.Editor.Fishing.GroupListenThreeClients",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatFishingGroupNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!Map) return false;
	Map->bIsNameStableForNetworking = true;
	Map->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("engine cube supplies the real network ground collision"), Cube)) return false;
	AStaticMeshActor* Ground = Map->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("native static mesh floor actor exists"), Ground)) return false;
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
	Ground->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ground->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	Ground->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(-1700, 0, -10), FVector(40, 100, 0.2)));
	Map->SpawnActor<ACatCampHubActor>(FVector(-600, 0, 100), FRotator::ZeroRotator);
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(4);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
		if (Driver.DefName == TEXT("GameNetDriver"))
			Driver.DriverClassName = Driver.DriverClassNameFallback = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishingGroupNetwork::FVerify>(this,
		[](ACatfishingGameModeBase* Mode)
		{
			Mode->bRunCommandsOpen = true;
			Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
			Mode->RunPublicState.Phase.bFishingAllowed = true;
		},
		[](UCatFishingCommandComponent* Commands, FCatFishingInputEdge Edge)
		{ Commands->ServerSubmitFishingAbilityCommand(ECatFishingCommandType::RequestHook, Edge); }));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
