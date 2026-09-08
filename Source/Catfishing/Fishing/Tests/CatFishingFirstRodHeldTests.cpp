#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "OnlineSubsystemTypes.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingFirstRodHeldTest,
	"Catfishing.Unit.Fishing.Service.FirstRodInteractHoldsFormalRodAndPreservesToggleAndRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingFirstRodHeldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const FName DefinitionId : {FName(TEXT("StarterRodT1")), FName(TEXT("ShopRodT2"))})
	{
		FTestWorldWrapper WorldWrapper;
		if (!TestTrue(TEXT("creates authority world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
		WorldWrapper.ForwardErrorMessages(this);
		UWorld* World = WorldWrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("creates project game mode"), World->SetGameMode(URL))) return false;
		if (!TestTrue(TEXT("starts presentation lifecycle"), WorldWrapper.BeginPlayInTestWorld())) return false;
		ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
		ACatfishingPlayerState* Player = World->SpawnActor<ACatfishingPlayerState>();
		ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
		if (!TestTrue(TEXT("spawns real authority hosts"), GameMode && Controller && Player && Character)) return false;
		Controller->PlayerState = Player;
		Character->SetPlayerState(Player);
		Controller->Possess(Character);
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("FirstRodHeld"), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		GameMode->bRunCommandsOpen = true;
		GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		GameMode->RunPublicState.Phase.bFishingAllowed = true;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
		Controller->SetPlayer(LocalPlayer.Get());
		Controller->SetControlRotation(FRotator(15.0, 35.0, 0.0));
		if (!TestTrue(TEXT("passes production command gates"),
			Controller->IsLocalController() && GameMode->CanAcceptFishingCommand(Controller))) return false;

		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* GroundBox = NewObject<UBoxComponent>(Ground);
		Ground->SetRootComponent(GroundBox);
		Ground->AddInstanceComponent(GroundBox);
		GroundBox->InitBoxExtent(FVector(1000.0, 1000.0, 10.0));
		GroundBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GroundBox->SetCollisionResponseToAllChannels(ECR_Block);
		GroundBox->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0, 0.0, -10.0));
		UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
		if (!TestTrue(TEXT("grants formal rod instance"), Equipment->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), Equipment->GetSnapshot().Revision, DefinitionId).bCommitted)) return false;
		const FGuid ItemId = Equipment->GetSnapshot().RodItemInstanceId;
		UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
		UCatFishingCommandComponent* Commands = Controller->GetFishingCommandComponent();
		UCatFishingSettings* Settings = GetMutableDefault<UCatFishingSettings>();
		const FVector OriginalLocation = Character->GetActorLocation();
		const EMovementMode OriginalMovement = Character->GetCharacterMovement()->MovementMode;

		// 持握配置无效会在 Use 后失败，必须恢复同一个库存实例并清除未发布 Actor。
		{
			TGuardValue<double> InvalidSpeed(Settings->HeldRodMaximumAngularSpeedDegreesPerSecond, 0.0);
			AddExpectedErrorPlain(TEXT("Event=fishing_rod_place_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedErrorPlain(TEXT("Event=fishing_command_result Type=ECatFishingCommandType::PlaceRod Committed=false"),
				EAutomationExpectedErrorFlags::Contains, 1);
			const FCatFishingInputEdge FailedEdge = Commands->SubmitRodInteract();
			FCatFishingCommandResult Failed;
			TestTrue(TEXT("failed first R returns correlated result"), Commands->TryGetResult(FailedEdge.RequestId, Failed));
			TestFalse(TEXT("failed held preparation does not commit"), Failed.bCommitted);
			TestEqual(TEXT("reports held dependency failure"), Failed.Error, ECatFishingCommandError::DependencyUnavailable);
			TestNull(TEXT("failed first R leaves no deployed rod"), Fishing->FindDeployedRod(Player));
			TestNull(TEXT("failed first R leaves no operator"), Fishing->FindRodOperatedBy(Player));
			TestTrue(TEXT("rollback restores same inventory instance"), Equipment->GetSnapshot().InventorySlots.ContainsByPredicate(
				[ItemId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == ItemId && Slot.Quantity == 1; }));
		}

		const FCatFishingInputEdge FirstEdge = Commands->SubmitRodInteract();
		FCatFishingCommandResult First;
		if (!TestTrue(TEXT("first successful R returns result"), Commands->TryGetResult(FirstEdge.RequestId, First))
			|| !TestTrue(TEXT("first R commits"), First.bCommitted)) return false;
		ACatFishingRodActor* Rod = Fishing->FindDeployedRod(Player);
		if (!TestNotNull(TEXT("first R creates a registered rod"), Rod)) return false;
		const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
		TestEqual(TEXT("spawns the formal configured Blueprint"), Rod->GetClass(), Definition->UseActorClass.Get());
		TestEqual(TEXT("first R already operates the new rod"), Fishing->FindRodOperatedBy(Player), Rod);
		TestEqual(TEXT("first R is held"), Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Held);
		TestEqual(TEXT("first R holder is the player"), Rod->GetPresentationState().HolderPlayerState.Get(), static_cast<APlayerState*>(Player));
		TestEqual(TEXT("initial state contains exactly one operator"), Rod->GetOperatorCount(), 1);
		TestEqual(TEXT("first R uses original instance"), Rod->GetPresentationState().ItemInstanceId, ItemId);
		TestEqual(TEXT("one initial state without a second operate command"), First.RodActorRevision, int64{1});
		const FVector ExpectedGrip = Character->GetActorLocation()
			+ Controller->GetControlRotation().RotateVector(Settings->HeldRodGripOffsetCentimeters);
		TestTrue(TEXT("formal BP is in the hand before any tick"), Rod->GetGripWorldTransform().GetLocation().Equals(ExpectedGrip, 0.01));
		TestTrue(TEXT("held rod updates with movement"), Rod->IsActorTickEnabled());
		const int64 UsedEquipmentRevision = Equipment->GetSnapshot().Revision;
		for (const ECatFishingRodPoseMode ExpectedPose : {ECatFishingRodPoseMode::Grounded, ECatFishingRodPoseMode::Held,
			ECatFishingRodPoseMode::Grounded, ECatFishingRodPoseMode::Held})
		{
			const FCatFishingInputEdge Edge = Commands->SubmitRodInteract();
			FCatFishingCommandResult Result;
			TestTrue(TEXT("subsequent R has a result"), Commands->TryGetResult(Edge.RequestId, Result));
			TestTrue(TEXT("subsequent R commits"), Result.bCommitted);
			TestEqual(TEXT("R alternates put down and pick up"), Rod->GetPresentationState().PoseMode, ExpectedPose);
			TestEqual(TEXT("R keeps the same actor"), Fishing->FindDeployedRod(Player), Rod);
			TestEqual(TEXT("toggle never uses inventory again"), Equipment->GetSnapshot().Revision, UsedEquipmentRevision);
		}
		TestEqual(TEXT("only one deployed rod exists"), Fishing->GetDeployedRodCountForDiagnostics(), 1);
		TestTrue(TEXT("taking or leaving rod never teleports character"), Character->GetActorLocation().Equals(OriginalLocation));
		TestEqual(TEXT("taking or leaving rod preserves movement mode"), Character->GetCharacterMovement()->MovementMode.GetValue(), OriginalMovement);
	}
	return !HasAnyErrors();
}

#endif
