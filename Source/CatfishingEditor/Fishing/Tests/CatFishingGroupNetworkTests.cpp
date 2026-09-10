#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
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
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/CatInventoryComponent.h"
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
			GameModeHandle = FGameModeEvents::OnGameModeInitializedEvent().AddLambda([](AGameModeBase* GameMode)
			{
				if (GameMode && GameMode->GetWorld() && GameMode->GetWorld()->WorldType == EWorldType::PIE
					&& GameMode->GetClass() == ACatfishingGameModeBase::StaticClass())
					GameMode->DefaultPawnClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
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
			FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeHandle);
			bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		bool bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
		FDelegateHandle StableWorldHandle, GameModeHandle;
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
				if (!bBodiesPlaced)
				{
					// Each owner supplies Body/Grab RPCs directly; disable controller polling so no synthetic
					// keyboard state can overwrite the requests being verified by this network fixture.
					Primary->SetActorTickEnabled(false);
					for (auto* Local : LocalClients) Local->SetActorTickEnabled(false);
					for (auto* Remote : RemoteControllers) Remote->SetActorTickEnabled(false);
					TArray<ACatfishingPlayerController*> Everyone = {Primary};
					Everyone.Append(RemoteControllers);
					for (int32 Index = 0; Index < Everyone.Num(); ++Index)
					{
						auto* Cat = CastChecked<ACatCharacter>(Everyone[Index]->GetPawn());
						auto* Physical = Cat->GetPhysicalBodyComponent();
						if (!Test->TestEqual(TEXT("four-player physics uses the actual formal cat asset"), Cat->GetClass()->GetPathName(),
							FString(TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")))) return true;
						const double ExtentX = Physical->GetBody()->GetScaledBoxExtent().X;
						const double Reach = Physical->GetGrab()->GetReachLengthCm();
						const FVector Shoulder = Physical->GetShoulderLocalPoint(true);
						const double FirstRowY = Shoulder.X + Reach * 0.5 - FMath::Abs(Shoulder.Y);
						const double RowSpacing = ExtentX + Shoulder.X + Reach * 0.5;
						const FVector SpawnPoint = Index == 0 ? FVector(0, 0, Cat->GetBodyStandRootHeightCm())
							: FVector(ExtentX * 4.0, FirstRowY + (Index - 1) * RowSpacing, Cat->GetBodyStandRootHeightCm());
						if (!Test->TestTrue(TEXT("setup places all three real bodies on the ground"), Cat->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(
							FTransform(FRotator(0, Index == 0 ? 0 : -90, 0), SpawnPoint), TEXT("NetworkTestSetup")))) return true;
						Cat->GetPhysicalBodyComponent()->SetViewIntent(FRotator(0, Index == 0 ? 0 : -90, 0));
						Cat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
						if (Index == 0)
						{
							if (!Test->TestTrue(TEXT("only the primary receives a fishing-session stamina baseline"),
								Cat->GetCatAbilitySystemComponent()->InitializeFishingStaminaForSession())) return true;
						}
						else
						{
							const float ExistingBalance = 17.0f + Index;
							Cat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), ExistingBalance);
							HelperStamina.Add(ExistingBalance);
						}
					}
					PrimaryCat->GetPhysicalBodyComponent()->GetGrab()->SetGrabInput(true, true);
					bBodiesPlaced = true;
					SetupStarted = Now;
					return false;
				}
				for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
				{
					auto* LocalBody = CastChecked<ACatCharacter>(LocalClients[Index]->GetPawn())->GetPhysicalBodyComponent();
					auto* AuthorityBody = CastChecked<ACatCharacter>(RemoteControllers[Index]->GetPawn())->GetPhysicalBodyComponent();
					if (LocalBody->GetResetEpoch() != AuthorityBody->GetResetEpoch()
						|| LocalBody->GetControlEpoch() != AuthorityBody->GetControlEpoch()) return false;
					LocalBody->SetViewIntent(FRotator(0, -90, 0));
				}
				if (Now - SetupStarted < 0.6) return false;
				Primary->SetControlRotation(FRotator::ZeroRotator);
				Equipment = PrimaryCat->GetEquipmentComponent();
				if (!Equipment.IsValid()) return true;
				for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
					Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id);
				Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 4);
				FCatPlaceRodCommand Place;
				Place.RequestId = FGuid::NewGuid();
				Place.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
				Place.ExpectedInventoryRevision = PrimaryCat->GetInventoryComponent()->GetInventoryRevision();
				const auto Placed = Fishing->PlaceRod(Primary, Place);
				if (!Test->TestTrue(TEXT("real Service places and physically holds the formal rod"), Placed.bCommitted)) return true;
				RodId = Placed.RodActorId;
				Rod = Fishing->FindDeployedRodById(RodId);
				if (!Rod.IsValid() || !Rod->GetPhysicalRodComponent()->IsReady()) return true;
				RodItemId = Rod->GetPresentationState().ItemInstanceId;
                // Upright hands do not move an ungripped capsule toward an out-of-reach shaft.
                // Place the ground-level fixture within actual reach of exposed rod geometry.
                const FVector ShaftPoint = Rod->GetGripWorldTransform().GetLocation() + Rod->GetPhysicalRodBody()->GetForwardVector() * 12.0;
                for (int32 I = 0; I < RemoteControllers.Num(); ++I)
                {
                    auto* HelperBody = CastChecked<ACatCharacter>(RemoteControllers[I]->GetPawn())->GetPhysicalBodyComponent();
                    const FVector At(ShaftPoint.X + 40, ShaftPoint.Y - 6.8 + I * 64.4, HelperBody->GetStandRootHeightCm());
                    HelperBody->TeleportBodyFromAuthority(FTransform(FRotator(0, I == 0 ? 180 : -90, 0), At), TEXT("UprightGroundGripFixture"));
                }
				// Leave the real rod at its authored hold pose. One helper holds the shaft;
				// the other two grip the preceding cat, so the graph follows physical contact.
				Stage = 1;
				StageStarted = Now;
			}

			for (int32 Index = 0; Index < RemoteControllers.Num(); ++Index)
				if (!Test->TestEqual(TEXT("physical helpers never initialize, pay or recover fishing stamina"),
					CastChecked<ACatCharacter>(RemoteControllers[Index]->GetPawn())->GetCatAbilitySystemComponent()->GetNumericAttribute(
						UCatSurvivalAttributeSet::GetFightStaminaAttribute()), HelperStamina[Index])) return true;

			TArray<ACatFishingRodActor*> ClientRods;
			for (UWorld* Client : Clients)
			{
				ACatFishingRodActor* Found = nullptr;
				for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
					if (It->GetPresentationState().RodActorId == RodId) Found = *It;
				if (!Found || Found->GetControlEpoch() == 0) return false;
				ClientRods.Add(Found);
			}
			TArray<AActor*> ClientGripTargets;
			for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
			{
				auto* Body = CastChecked<ACatCharacter>(LocalClients[Index]->GetPawn())->GetPhysicalBodyComponent();
				const FVector Shoulder = Body->GetGrab()->GetShoulderWorldLocation(true);
				AActor* Target = ClientRods[Index];
				FVector AimPoint;
				if (Index == 0)
				{
					AimPoint = ClientRods[Index]->GetGripWorldTransform().GetLocation() + ClientRods[Index]->GetPhysicalRodBody()->GetForwardVector() * 12.0;
				}
				else
				{
					ACatCharacter* PreviousCat = nullptr;
					const int32 PreviousPlayerId = LocalClients[Index - 1]->PlayerState->GetPlayerId();
					for (TActorIterator<ACatCharacter> It(Clients[Index]); It; ++It)
						if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == PreviousPlayerId) PreviousCat = *It;
					if (!PreviousCat) return false;
					Target = PreviousCat;
					AimPoint = PreviousCat->GetPhysicalBodyComponent()->GetBody()->GetComponentLocation();
				}
				ClientGripTargets.Add(Target);
				// A human stops moving the mouse once contact is established. Continuing to
				// aim at the moving target's centre would keep applying new shoulder/yaw input
				// during the later "released movement" convergence measurement.
				if (!bOwnerAimFixed[Index])
				{
					if (Body->GetGrab()->IsGripping(true)) bOwnerAimFixed[Index] = true;
					else FixedOwnerAim[Index] = (AimPoint - Shoulder).Rotation();
				}
				Body->SetViewIntent(FixedOwnerAim[Index]);
			}
			if (Stage == 1)
			{
				TArray<ACatCharacter*> AuthorityCats = {PrimaryCat};
				for (auto* Remote : RemoteControllers) AuthorityCats.Add(CastChecked<ACatCharacter>(Remote->GetPawn()));
				if (!bBodiesReady)
				{
					bool bReady = PrimaryCat->GetPhysicalBodyComponent()->IsGrounded();
					for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
					{
						auto* LocalCat = CastChecked<ACatCharacter>(LocalClients[Index]->GetPawn());
						bReady &= LocalCat->GetPhysicalBodyComponent()->IsGrounded() && AuthorityCats[Index + 1]->GetPhysicalBodyComponent()->IsGrounded()
							&& FVector::Distance(LocalCat->GetActorLocation(), AuthorityCats[Index + 1]->GetActorLocation()) < 5.0;
					}
					if (!bReady)
					{
						if (Now - StageStarted < 10.0) return false;
						Test->AddError(TEXT("physical bodies did not settle and converge on all three owners")); return true;
					}
					bBodiesReady = true;
				}
				if (JoinIndex < LocalClients.Num())
				{
					auto* LocalBody = CastChecked<ACatCharacter>(LocalClients[JoinIndex]->GetPawn())->GetPhysicalBodyComponent();
					if (!bGrabRequested)
					{
						LocalBody->GetGrab()->SetGrabInput(true, true);
						bGrabRequested = true; GripStarted = Now;
					}
					UCatPhysicsGrabComponent* AuthorityGrab = AuthorityCats[JoinIndex + 1]->GetPhysicalBodyComponent()->GetGrab();
					AActor* AuthorityTarget = JoinIndex == 0 ? static_cast<AActor*>(Rod.Get()) : AuthorityCats[JoinIndex];
					if (!AuthorityGrab->IsGripping(true) || AuthorityGrab->GetGripTarget(true) != AuthorityTarget
						|| !LocalBody->GetGrab()->IsGripping(true) || LocalBody->GetGrab()->GetGripTarget(true) != ClientGripTargets[JoinIndex]
						|| ClientRods[JoinIndex]->GetOperatorCount() != 1)
					{
						if (Now - GripStarted < 8.0) return false;
						Test->AddError(FString::Printf(TEXT("owner grip failed to reach the actual contact graph and single-primary ownership Helper=%d Target=%s Expected=%s Hand=%s Rod=%s"),
							JoinIndex, *GetNameSafe(AuthorityGrab->GetGripTarget(true)), *GetNameSafe(AuthorityTarget), *AuthorityCats[JoinIndex + 1]->GetPhysicalBodyComponent()->GetHand(true)->GetComponentLocation().ToCompactString(),
							*Rod->GetGripWorldTransform().GetLocation().ToCompactString())); return true;
					}
					++JoinIndex; bGrabRequested = false;
					return false;
				}
				for (ACatFishingRodActor* ClientRod : ClientRods) if (ClientRod->GetOperatorCount() != 1) return false;
				if (!Test->TestEqual(TEXT("three physical helpers never become fishing operators"), Rod->GetOperatorCount(), 1)
					|| !Test->TestEqual(TEXT("physical grips preserve the original primary"), Rod->GetPresentationState().OperatorPlayerState.Get(), Primary->PlayerState.Get())) return true;
				if (PreCastMotionStage == 0)
				{
					for (ACatCharacter* Cat : AuthorityCats)
					{
						PreCastPositions.Add(Cat->GetActorLocation());
						PreCastStamina.Add(Cat->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()));
					}
					PreCastMotionStarted = Now; PreCastMotionStage = 1;
				}
				if (PreCastMotionStage == 1)
				{
					for (auto* Local : LocalClients) CastChecked<ACatCharacter>(Local->GetPawn())->GetPhysicalBodyComponent()->SetMoveIntent(-FVector::ForwardVector);
					PrimaryCat->GetPhysicalBodyComponent()->SetMoveIntent(-FVector::ForwardVector);
					if (Now - PreCastMotionStarted < 0.8) return false;
					for (int32 Index = 0; Index < AuthorityCats.Num(); ++Index)
					{
						if (!Test->TestTrue(TEXT("pre-cast owner intent reaches real authority physics and moves the connected body"),
							AuthorityCats[Index]->GetPhysicalBodyComponent()->GetMoveIntent().X < -0.9
							&& FVector::DistSquared(PreCastPositions[Index], AuthorityCats[Index]->GetActorLocation()) > 1.0)) return true;
					}
					for (auto* Local : LocalClients) CastChecked<ACatCharacter>(Local->GetPawn())->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
					PrimaryCat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
					PreCastMotionStage = 2; PreCastMotionStarted = Now; return false;
				}
				if (PreCastMotionStage == 2)
				{
					bool bSettled = PrimaryCat->GetPhysicalBodyComponent()->IsGrounded() && PrimaryCat->GetVelocity().Size2D() < 3.0;
					const bool bLogSample = Now - LastSettleSampleSeconds >= 1.0;
					if (bLogSample)
					{
						LastSettleSampleSeconds = Now;
						Test->AddInfo(FString::Printf(TEXT("Event=fishing_physical_network_settle_primary Seconds=%.3f Position=%s Velocity=%s UpZ=%.3f Grounded=%d Grip=%s RodAngularVelocity=%s"),
							Now - PreCastMotionStarted, *PrimaryCat->GetActorLocation().ToCompactString(), *PrimaryCat->GetVelocity().ToCompactString(),
							PrimaryCat->GetPhysicalBodyComponent()->GetBody()->GetUpVector().Z, PrimaryCat->GetPhysicalBodyComponent()->IsGrounded(),
							*GetNameSafe(PrimaryCat->GetPhysicalBodyComponent()->GetGrab()->GetGripTarget(true)),
							*Rod->GetPhysicalRodComponent()->GetBody()->GetPhysicsAngularVelocityInRadians().ToCompactString()));
					}
					for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
					{
						auto* LocalCat = CastChecked<ACatCharacter>(LocalClients[Index]->GetPawn());
						const double PositionError = FVector::Distance(LocalCat->GetActorLocation(), AuthorityCats[Index + 1]->GetActorLocation());
						bSettled &= PositionError < 5.0
							&& LocalCat->GetVelocity().Size2D() < 3.0 && AuthorityCats[Index + 1]->GetVelocity().Size2D() < 3.0;
						if (bLogSample) Test->AddInfo(FString::Printf(TEXT("Event=fishing_physical_network_settle_helper Index=%d ErrorCm=%.3f ServerPosition=%s ClientPosition=%s ServerVelocity=%s ClientVelocity=%s AcceptedMove=%s Grip=%s"),
							Index, PositionError, *AuthorityCats[Index + 1]->GetActorLocation().ToCompactString(), *LocalCat->GetActorLocation().ToCompactString(),
							*AuthorityCats[Index + 1]->GetVelocity().ToCompactString(), *LocalCat->GetVelocity().ToCompactString(),
							*AuthorityCats[Index + 1]->GetPhysicalBodyComponent()->GetMoveIntent().ToCompactString(),
							*GetNameSafe(AuthorityCats[Index + 1]->GetPhysicalBodyComponent()->GetGrab()->GetGripTarget(true))));
					}
					if (!bSettled) SettledSinceSeconds = 0.0;
					else if (SettledSinceSeconds == 0.0) SettledSinceSeconds = Now;
					if (!bSettled || Now - SettledSinceSeconds < 0.3)
					{
						if (Now - PreCastMotionStarted < 6.0) return false;
						Test->AddError(TEXT("physical bodies failed to settle and replicate after releasing movement")); return true;
					}
					for (int32 Index = 0; Index < AuthorityCats.Num(); ++Index)
						if (!Test->TestEqual(TEXT("pre-cast physical cooperation spends no fishing stamina"), AuthorityCats[Index]->GetCatAbilitySystemComponent()->GetNumericAttribute(
							UCatSurvivalAttributeSet::GetFightStaminaAttribute()), PreCastStamina[Index])) return true;
					PreCastMotionStage = 3;
					Test->AddInfo(TEXT("Event=fishing_physical_network_precast_verified FishingOperators=1 ActualGrips=4 OwnerPositionErrorCm=5 StaminaUnchanged=1"));
				}
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
				if (!Test->TestEqual(TEXT("cast retains only the primary as fishing operator"), Rod->GetOperatorCount(), 1)) return true;
				SessionId = Begun.Command.FishingSessionId;
				Session = Fishing->FindSession(SessionId);
				Stage = 2;
			}
			if (!Session.IsValid() || Session->IsTerminal()) { Test->AddError(TEXT("group network session ended before verdict")); return true; }
			if (Stage == 2)
			{
				const ECatFishingPhase Phase = Session->GetSnapshot().Phase;
				UnloadedPhases.Add(Phase);
				TArray<ACatCharacter*> AuthorityCats = {PrimaryCat};
				for (auto* Remote : RemoteControllers) AuthorityCats.Add(CastChecked<ACatCharacter>(Remote->GetPawn()));
				for (int32 Index = 0; Index < AuthorityCats.Num(); ++Index)
				{
					if (!Test->TestTrue(TEXT("pre-fight session retains every actual rod or cat-chain grip"), AuthorityCats[Index]->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true))) return true;
					if (!Test->TestEqual(TEXT("cast and bite waiting do not charge movement fishing stamina"), AuthorityCats[Index]->GetCatAbilitySystemComponent()->GetNumericAttribute(
						UCatSurvivalAttributeSet::GetFightStaminaAttribute()), PreCastStamina[Index])) return true;
				}
				if (Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow) return false;
				if (!Test->TestTrue(TEXT("real waiting and bite window retain the same session while helpers remain physically connected"),
					UnloadedPhases.Contains(ECatFishingPhase::Waiting)
					&& UnloadedPhases.Contains(ECatFishingPhase::TrueBiteWindow))) return true;
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
				ACatFishingSession* Found = nullptr;
				for (TActorIterator<ACatFishingSession> It(Clients[Index]); It; ++It)
					if (It->GetSnapshot().FishingSessionId == SessionId) Found = *It;
				if (!Found) return false;
				if (!Test->TestNull(TEXT("a physical helper has no fishing session or fishing HUD ownership"),
					UCatFishingViewBridge::FindFishingSessionForPlayerState(Clients[Index], LocalClients[Index]->PlayerState))) return true;
				ClientSessions.Add(Found);
			}
			if (Stage == 3)
			{
				for (int32 Index = 0; Index < LocalClients.Num(); ++Index)
					CastChecked<ACatCharacter>(LocalClients[Index]->GetPawn())->GetPhysicalBodyComponent()->SetMoveIntent(Index == 2 ? FVector::ForwardVector : -FVector::ForwardVector);
				PrimaryCat->GetPhysicalBodyComponent()->SetMoveIntent(-FVector::ForwardVector);
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
					auto* Physical = CastChecked<ACatCharacter>(Remote->GetPawn())->GetPhysicalBodyComponent();
					bAllMoveAccepted &= Physical->GetMoveIntent().SizeSquared() > 0.01;
				}
				if (!bAllMoveAccepted || Now - StageStarted < 0.5) return false;
				for (ACatFishingSession* ClientSession : ClientSessions)
					if (ClientSession->GetSnapshot().FightParticipantCount != 1 || ClientSession->GetSnapshot().CombinedFightStaminaMaximum <= 0) return false;
				for (int32 Index = 0; Index < RemoteControllers.Num(); ++Index)
					if (!Test->TestTrue(FString::Printf(TEXT("each remote helper moves in the authority Chaos world PlayerId=%d Initial=%s Current=%s"),
						RemoteControllers[Index]->PlayerState->GetPlayerId(), *InitialHelperPositions[Index].ToCompactString(),
						*RemoteControllers[Index]->GetPawn()->GetActorLocation().ToCompactString()),
						FVector::DistSquared(InitialHelperPositions[Index], RemoteControllers[Index]->GetPawn()->GetActorLocation()) > 1.0)) return true;
				SampledRodTravel = FVector::Distance(InitialRodPosition, Rod->GetActorLocation());
				SampledFishTravel = FVector::Distance(InitialFishPosition, Session->GetSnapshot().FishEncounterActor->GetActorLocation());
				OldEpoch = Rod->GetControlEpoch();
				for (auto* Local : LocalClients) CastChecked<ACatCharacter>(Local->GetPawn())->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
				PrimaryCat->GetPhysicalBodyComponent()->SetMoveIntent(FVector::ZeroVector);
				// Release the far end of the cat chain through its actual owning-client RPC.
				CastChecked<ACatCharacter>(LocalClients[2]->GetPawn())->GetPhysicalBodyComponent()->GetGrab()->SetGrabInput(true, false);
				Stage = 4;
			}
			if (Stage == 4)
			{
				if (CastChecked<ACatCharacter>(RemoteControllers[2]->GetPawn())->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true)
					|| CastChecked<ACatCharacter>(LocalClients[2]->GetPawn())->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true)) return false;
				if (!Test->TestEqual(TEXT("helper release leaves the primary control epoch unchanged"), Rod->GetControlEpoch(), OldEpoch)
					|| !Test->TestEqual(TEXT("helper release leaves the fishing session with one primary"), Session->GetSnapshot().FightParticipantCount, 1)
					|| !Test->TestEqual(TEXT("helper release cannot replace the session fisher"), Session->GetSnapshot().FisherPlayerState.Get(), Primary->PlayerState.Get())) return true;
				FCatFishingInputEdge Forbidden;
				Forbidden.RequestId = StaleRequest = FGuid::NewGuid();
				Forbidden.InputSequence = 1000;
				Forbidden.ControlRodActorId = RodId;
				Forbidden.ControlEpoch = OldEpoch;
				Test->AddExpectedErrorPlain(TEXT("Event=fishing_control_input_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
				Test->AddExpectedErrorPlain(TEXT("Error=ECatFishingCommandError::NotFisher"), EAutomationExpectedErrorFlags::Contains, 1);
				SendStale(LocalClients[0]->GetFishingCommandComponent(), Forbidden);
				Stage = 5;
			}
			if (Stage == 5)
			{
				FCatFishingCommandResult Result;
				if (!LocalClients[0]->GetFishingCommandComponent()->TryGetResult(StaleRequest, Result)) return false;
				if (!Test->TestTrue(TEXT("a direct physical rod holder cannot submit the primary's fishing input"),
					!Result.bCommitted && Result.Error == ECatFishingCommandError::NotFisher)) return true;
				UCatPhysicsGrabComponent* PrimaryGrab = PrimaryCat->GetPhysicalBodyComponent()->GetGrab();
				const FGuid PrimaryGripId = PrimaryGrab->GetGripState(true).GripId;
				PrimaryGrab->SetGrabInput(true, false);
				PrimaryGrab->SetGrabInput(false, false);
				if (!Test->TestTrue(TEXT("ordinary mouse release preserves the same explicitly held primary contact"),
					PrimaryGrab->IsGripping(true) && PrimaryGrab->GetGripState(true).bExplicitHold
					&& PrimaryGrab->GetGripState(true).GripId == PrimaryGripId && Rod->IsPrimaryOperator(Primary->PlayerState))) return true;
				const FCatFishingInputEdge LeaveEdge = Primary->GetFishingCommandComponent()->SubmitRodInteract();
				FCatFishingCommandResult LeaveResult;
				if (!Test->TestTrue(TEXT("the real R route returns a correlated successful leave receipt"),
					Primary->GetFishingCommandComponent()->TryGetResult(LeaveEdge.RequestId, LeaveResult)
					&& LeaveResult.bCommitted && LeaveResult.CommandType == ECatFishingCommandType::LeaveRod)) return true;
				if (!Test->TestFalse(TEXT("R releases the primary contact and its explicit source"),
					PrimaryGrab->IsGripping(true) || PrimaryGrab->GetGripState(true).bExplicitHold)) return true;
				ParkedPose = Rod->GetActorTransform();
				StageStarted = Now;
				Stage = 6;
			}
			if (Stage == 6)
			{
				if (Now - StageStarted < 1 || Rod->GetOperatorCount() != 0 || Session->GetSnapshot().FisherPlayerState != nullptr) return false;
				for (int32 Index = 0; Index < ClientRods.Num(); ++Index)
					if (ClientRods[Index]->GetOperatorCount() != 0 || ClientRods[Index]->GetControlEpoch() == OldEpoch
						|| ClientSessions[Index]->GetSnapshot().FisherPlayerState != nullptr) return false;
				UCatPhysicsGrabComponent* RemainingGrab = CastChecked<ACatCharacter>(RemoteControllers[0]->GetPawn())->GetPhysicalBodyComponent()->GetGrab();
				if (!Test->TestFalse(TEXT("parking clears the helper rod grip on both endpoints"),
					RemainingGrab->IsGripping(true) || CastChecked<ACatCharacter>(LocalClients[0]->GetPawn())->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true))
					|| !Test->TestNull(TEXT("a remaining physical helper is never promoted automatically"), Rod->GetPresentationState().OperatorPlayerState.Get())
					|| !Test->TestEqual(TEXT("unattended fishing preserves the deployment owner"), Rod->GetPresentationState().OwnerPlayerState.Get(), Primary->PlayerState.Get())
					|| !Test->TestFalse(TEXT("the same line and fishing session continue unattended"), Session->IsTerminal())) return true;
				FCatInventoryEndpointSnapshot Locked;
				Test->TestEqual(TEXT("unattended fishing keeps the original rod resource lock"), Equipment->ReadInventoryTransferEndpoint(TEXT("ActiveUse"), RodItemId, Locked), ECatDomainCommandError::InvalidPhase);
				if (Test->HasAnyErrors()) return true;
				Test->AddInfo(FString::Printf(TEXT("Event=fishing_physical_helpers_network_verified SessionId=%s RodActorId=%s PreviousFishingOperators=1 CurrentFishingOperators=0 PhysicalHelpers=3 HelperASCUnchanged=1 AutoPromotion=0 ControlEpoch=%u OldControlEpoch=%u LineLoad=%.3f Samples=%d MaximumLineLoad=%.3f RodTravelCm=%.3f FishTravelCm=%.3f Server=Listen Clients=3 Evidence=runtime_behavior"),
					*SessionId.ToString(), *RodId.ToString(), Rod->GetControlEpoch(), OldEpoch, Session->GetSnapshot().NormalizedLineLoad,
					FightSamples, MaximumSampledLineLoad, SampledRodTravel, SampledFishTravel));
				Test->TestTrue(TEXT("same session's rod stays fixed while unattended"), Rod->GetActorTransform().Equals(ParkedPose, .01));
				// Compare the synchronous command transaction: live fish wear may advance the
				// equipment revision while waiting for the client acknowledgements below.
				const int64 BeforePickupRevision = Equipment->GetSnapshot().Revision;
				const auto Pickup = Primary->GetFishingCommandComponent()->SubmitRodInteract();
				FCatFishingCommandResult PickupResult;
				if (!Test->TestTrue(TEXT("real R directly picks up the parked rod during the same fight"),
					Primary->GetFishingCommandComponent()->TryGetResult(Pickup.RequestId, PickupResult)
					&& PickupResult.bCommitted && PickupResult.CommandType == ECatFishingCommandType::OperateRod)) return true;
				if (!Test->TestEqual(TEXT("R retake never uses inventory again"), Equipment->GetSnapshot().Revision, BeforePickupRevision)) return true;
				Test->AddInfo(FString::Printf(TEXT("Event=fishing_rod_retake_inventory_verified SessionId=%s RodActorId=%s RodItemInstanceId=%s BeforeRevision=%lld AfterRevision=%lld Authority=true NetMode=ListenServer Result=Unchanged"),
					*SessionId.ToString(), *RodId.ToString(), *RodItemId.ToString(), BeforePickupRevision, Equipment->GetSnapshot().Revision));
				Stage = 7;
				return false;
			}
			if (Stage == 7)
			{
				for (int32 Index = 0; Index < ClientRods.Num(); ++Index)
					if (ClientRods[Index]->GetOperatorCount() != 1 || !ClientSessions[Index]->GetSnapshot().FisherPlayerState) return false;
				Test->TestEqual(TEXT("R retake preserves Session ID"), Session->GetSnapshot().FishingSessionId, SessionId);
				Test->TestEqual(TEXT("R retake preserves rod identity"), Rod->GetPresentationState().RodActorId, RodId);
				Test->TestEqual(TEXT("R retake preserves the same rod inventory instance"), Rod->GetPresentationState().ItemInstanceId, RodItemId);
				Test->TestEqual(TEXT("R retake restores only the owner as fisher"), Session->GetSnapshot().FisherPlayerState.Get(), Primary->PlayerState.Get());
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
		FTransform ParkedPose = FTransform::Identity;
		bool bBodiesReady = false;
		bool bBodiesPlaced = false;
		double SetupStarted = 0.0;
		bool bGrabRequested = false;
		double GripStarted = 0.0;
		int32 PreCastMotionStage = 0;
		double PreCastMotionStarted = 0.0;
		double LastSettleSampleSeconds = 0.0, SettledSinceSeconds = 0.0;
		TArray<FVector> PreCastPositions;
		TArray<float> PreCastStamina;
		TArray<float> HelperStamina;
		bool bOwnerAimFixed[3] = {false, false, false};
		FRotator FixedOwnerAim[3] = {FRotator::ZeroRotator, FRotator::ZeroRotator, FRotator::ZeroRotator};
		TSet<ECatFishingPhase> UnloadedPhases;
		int32 FightSamples = 0;
		double MaximumSampledLineLoad = 0.0, SampledRodTravel = 0.0, SampledFishTravel = 0.0;
		FVector InitialRodPosition = FVector::ZeroVector, InitialFishPosition = FVector::ZeroVector;
		TArray<FVector> InitialHelperPositions;
		uint32 OldEpoch = 0;
		FGuid RodId, RodItemId, SessionId, StaleRequest;
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
	if (!TestNotNull(TEXT("formal cat asset is available"), LoadClass<ACatCharacter>(nullptr,
		TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C")))) return false;
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
