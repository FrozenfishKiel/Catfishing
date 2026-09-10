#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/CatInventoryComponent.h"
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
			AddExpectedErrorPlain(TEXT("Stage=PrepareHeldRod"), EAutomationExpectedErrorFlags::Contains, 1);
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

		// 第二根必须先有独立库存实例；使用不同正式型号同时覆盖备用竿的定义与 Actor 选择。
		const FName SecondDefinitionId = DefinitionId == FName(TEXT("StarterRodT1"))
			? FName(TEXT("ShopRodT2")) : FName(TEXT("StarterRodT1"));
		if (!TestTrue(TEXT("grants a second physical formal rod"), Equipment->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), Equipment->GetSnapshot().Revision, SecondDefinitionId).bCommitted)) return false;
		const FCatRunInventorySlot* SecondInventorySlot = Equipment->GetSnapshot().InventorySlots.FindByPredicate(
			[SecondDefinitionId](const FCatRunInventorySlot& Slot) { return Slot.DefinitionId == SecondDefinitionId && Slot.Quantity == 1; });
		if (!TestNotNull(TEXT("second formal rod has its own inventory instance"), SecondInventorySlot)) return false;
		const FCatRunInventorySlot SecondInventoryItem = *SecondInventorySlot;
		const FGuid SecondItemId = SecondInventoryItem.ItemInstanceId;
		TestNotEqual(TEXT("two physical rods have distinct instance IDs"), SecondItemId, ItemId);

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
		TestEqual(TEXT("first command acknowledges the committed primary revision"), First.RodActorRevision, Rod->GetPresentationState().RodActorRevision);
		const FVector ExpectedGrip = Character->GetPhysicalBodyComponent()->GetBody()->GetComponentLocation()
			+ Rod->GetGripWorldTransform().GetRotation().RotateVector(GetDefault<UCatFishingSettings>()->HeldRodGripOffsetCentimeters);
		TestTrue(TEXT("formal BP uses the original controlled held offset before any tick"), Rod->GetGripWorldTransform().GetLocation().Equals(ExpectedGrip, 0.01));
		TestTrue(TEXT("held rod updates with movement"), Rod->IsActorTickEnabled());
		const int64 UsedEquipmentRevision = Equipment->GetSnapshot().Revision;
		for (int32 Cycle = 0; Cycle < 2; ++Cycle)
		{
			const FCatFishingInputEdge Edge = Commands->SubmitRodInteract();
			FCatFishingCommandResult Result;
			TestTrue(TEXT("subsequent R has a result"), Commands->TryGetResult(Edge.RequestId, Result));
			TestTrue(TEXT("subsequent R commits"), Result.bCommitted);
			TestEqual(TEXT("R puts down the occupied physical rod"), Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Grounded);
			TestFalse(TEXT("R releases the actual holding constraint"), Character->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true));
			TestTrue(TEXT("fixture positions the released rod for actual regrip without moving the cat"), Rod->BeginPhysicalHoldFromAuthority(Player, true));
			TestEqual(TEXT("physical grip alone does not grant primary control"), Rod->GetOperatorCount(), 0);
			const FCatFishingInputEdge RetakeEdge = Commands->SubmitRodInteract();
			FCatFishingCommandResult Retake;
			TestTrue(TEXT("R explicitly retakes the physically held owned rod"), Commands->TryGetResult(RetakeEdge.RequestId, Retake) && Retake.bCommitted);
			TestEqual(TEXT("explicit R restores the primary role"), Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Held);
			TestEqual(TEXT("R keeps the same actor"), Fishing->FindDeployedRod(Player), Rod);
			TestEqual(TEXT("toggle never uses inventory again"), Equipment->GetSnapshot().Revision, UsedEquipmentRevision);
		}
		TestEqual(TEXT("only one deployed rod exists"), Fishing->GetDeployedRodCountForDiagnostics(), 1);
		TestTrue(TEXT("taking or leaving rod never teleports character"), Character->GetActorLocation().Equals(OriginalLocation));
		TestEqual(TEXT("taking or leaving rod preserves movement mode"), Character->GetCharacterMovement()->MovementMode.GetValue(), OriginalMovement);
		const int64 BeforeFocusLossRevision = Equipment->GetSnapshot().Revision;
		Commands->SubmitPrimaryPressed();
		Commands->ClearHeldInputForLifecycle(TEXT("TestFocusLostWhileAiming"));
		Commands->SubmitPrimaryReleased();
		TestEqual(TEXT("focus loss cancels uncommitted aim without creating a cast session"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
		TestEqual(TEXT("canceling pending aim cannot consume equipment"), Equipment->GetSnapshot().Revision, BeforeFocusLossRevision);

		// 直接命令同样守住一人一根手持竿，不只依赖 R 的正常分派。
		FCatPlaceRodCommand HeldPlaceCommand;
		HeldPlaceCommand.RequestId = FGuid::NewGuid();
		HeldPlaceCommand.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
		HeldPlaceCommand.ExpectedInventoryRevision = Character->GetInventoryComponent()->GetInventoryRevision();
		AddExpectedErrorPlain(TEXT("Reason=AlreadyOperatingRod"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedErrorPlain(TEXT("Event=fishing_command_result Type=ECatFishingCommandType::PlaceRod Committed=false"),
			EAutomationExpectedErrorFlags::Contains, 1);
		Commands->SubmitPlaceRod(HeldPlaceCommand);
		FCatFishingCommandResult HeldPlaceResult;
		TestTrue(TEXT("held direct placement has a correlated result"), Commands->TryGetResult(HeldPlaceCommand.RequestId, HeldPlaceResult));
		TestFalse(TEXT("cannot deploy a second rod while holding the first"), HeldPlaceResult.bCommitted);
		TestEqual(TEXT("held placement reports existing operation"), HeldPlaceResult.Error, ECatFishingCommandError::ActiveSessionExists);
		TestEqual(TEXT("rejected held placement preserves inventory revision"), Equipment->GetSnapshot().Revision, UsedEquipmentRevision);
		TestEqual(TEXT("rejected held placement leaves one deployed rod"), Fishing->GetDeployedRodCount(Player), 1);

		FCatFishingCommandResult PutDownFirst;
		const FCatFishingInputEdge PutDownFirstEdge = Commands->SubmitRodInteract();
		if (!TestTrue(TEXT("putting down first rod has a result"), Commands->TryGetResult(PutDownFirstEdge.RequestId, PutDownFirst))
			|| !TestTrue(TEXT("first rod can be put down before taking the spare"), PutDownFirst.bCommitted)) return false;
		const FVector SecondRodPlayerLocation = OriginalLocation + FVector(600.0, 0.0, 0.0);
		Character->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(Character->GetActorRotation(), SecondRodPlayerLocation), TEXT("TestPosition"));
		const FCatFishingInputEdge SecondEdge = Commands->SubmitRodInteract();
		FCatFishingCommandResult Second;
		if (!TestTrue(TEXT("second R has a correlated result"), Commands->TryGetResult(SecondEdge.RequestId, Second))
			|| !TestTrue(TEXT("R deploys the physical spare rod"), Second.bCommitted)) return false;
		ACatFishingRodActor* SecondRod = Fishing->FindDeployedRodById(Second.RodActorId);
		if (!TestNotNull(TEXT("second rod is independently registered"), SecondRod)) return false;
		const UCatEquipmentDefinition* SecondDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(SecondDefinitionId);
		TestEqual(TEXT("spare uses its own formal Blueprint class"), SecondRod->GetClass(), SecondDefinition->UseActorClass.Get());
		TestEqual(TEXT("spare uses its own physical instance"), SecondRod->GetPresentationState().ItemInstanceId, SecondItemId);
		TestEqual(TEXT("spare preserves its definition"), SecondRod->GetPresentationState().RodDefinitionId, SecondDefinitionId);
		TestEqual(TEXT("held plus grounded rods consume both deployment slots"), Fishing->GetDeployedRodCount(Player), 2);
		TestEqual(TEXT("first rod stays grounded"), Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Grounded);
		TestEqual(TEXT("first rod has no operator"), Rod->GetOperatorCount(), 0);
		TestEqual(TEXT("only second rod is held"), SecondRod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Held);
		TestEqual(TEXT("operator lookup targets second rod"), Fishing->FindRodOperatedBy(Player), SecondRod);
		TestFalse(TEXT("deployed physical rods are absent from inventory"), Equipment->GetSnapshot().InventorySlots.ContainsByPredicate(
			[ItemId, SecondItemId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == ItemId || Slot.ItemInstanceId == SecondItemId; }));

		FCatOperateRodCommand OperateFirst;
		OperateFirst.Context.RequestId = FGuid::NewGuid();
		OperateFirst.Context.RodActorId = Rod->GetPresentationState().RodActorId;
		OperateFirst.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
		AddExpectedErrorPlain(TEXT("Event=fishing_command_result Type=ECatFishingCommandType::OperateRod Committed=false"),
			EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedErrorPlain(TEXT("Reason=AlreadyOperatingRod"), EAutomationExpectedErrorFlags::Contains, 1);
		Commands->SubmitOperateRod(OperateFirst);
		FCatFishingCommandResult OperateFirstResult;
		TestTrue(TEXT("direct second operation has a correlated result"), Commands->TryGetResult(OperateFirst.Context.RequestId, OperateFirstResult));
		TestFalse(TEXT("cannot occupy the first rod while holding the second"), OperateFirstResult.bCommitted);
		TestEqual(TEXT("direct second operation cannot grant a second control role"), OperateFirstResult.Error, ECatFishingCommandError::RodOccupied);
		TestEqual(TEXT("rejected operation leaves first rod empty"), Rod->GetOperatorCount(), 0);
		TestEqual(TEXT("rejected operation preserves second rod's only operator"), SecondRod->GetOperatorCount(), 1);

		FCatFishingCommandResult PutDownSecond;
		const FCatFishingInputEdge PutDownSecondEdge = Commands->SubmitRodInteract();
		if (!TestTrue(TEXT("putting down second rod has a result"), Commands->TryGetResult(PutDownSecondEdge.RequestId, PutDownSecond))
			|| !TestTrue(TEXT("second rod can be put down"), PutDownSecond.bCommitted)) return false;
		TestNull(TEXT("both grounded rods leave the player empty handed"), Fishing->FindRodOperatedBy(Player));
		if (!TestTrue(TEXT("grants a third physical rod to distinguish the deployment limit from missing inventory"),
			Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, DefinitionId).bCommitted)) return false;
		Character->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(Character->GetActorRotation(), OriginalLocation - FVector(600.0, 0.0, 0.0)), TEXT("TestPosition"));
		const FCatEquipmentLoadoutSnapshot BeforeThird = Equipment->GetSnapshot();
		AddExpectedErrorPlain(TEXT("Reason=DeploymentLimitReached"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedErrorPlain(TEXT("Event=fishing_command_result Type=ECatFishingCommandType::PlaceRod Committed=false"),
			EAutomationExpectedErrorFlags::Contains, 1);
		const FCatFishingInputEdge ThirdEdge = Commands->SubmitRodInteract();
		FCatFishingCommandResult Third;
		TestTrue(TEXT("third R has a correlated result"), Commands->TryGetResult(ThirdEdge.RequestId, Third));
		TestFalse(TEXT("third deployed rod is rejected even with a physical spare"), Third.bCommitted);
		TestEqual(TEXT("third rod reports the deployment limit"), Third.Error, ECatFishingCommandError::RodDeploymentLimitReached);
		TestEqual(TEXT("third rejection leaves exactly two deployed rods"), Fishing->GetDeployedRodCount(Player), 2);
		TestTrue(TEXT("third rejection leaves the entire equipment snapshot unchanged"),
			FCatEquipmentLoadoutSnapshot::StaticStruct()->CompareScriptStruct(&BeforeThird, &Equipment->GetSnapshot(), 0));

		// 空手 X 必须按附近的具体竿收回；索引里的另一根本人竿不应影响目标或库存归还。
		Character->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(Character->GetActorRotation(), SecondRodPlayerLocation), TEXT("TestPosition"));
		const int64 FirstRodRevisionBeforePack = Rod->GetPresentationState().RodActorRevision;
		const FCatFishingInputEdge PackSecondEdge = Commands->SubmitCancel();
		FCatFishingCommandResult PackSecond;
		if (!TestTrue(TEXT("empty handed X has a correlated result"), Commands->TryGetResult(PackSecondEdge.RequestId, PackSecond))
			|| !TestTrue(TEXT("empty handed X packs nearby second rod"), PackSecond.bCommitted)) return false;
		TestEqual(TEXT("X result identifies the second rod"), PackSecond.RodActorId, Second.RodActorId);
		TestEqual(TEXT("packing second leaves first registered"), Fishing->FindDeployedRod(Player), Rod);
		TestEqual(TEXT("packing second preserves first rod revision"), Rod->GetPresentationState().RodActorRevision, FirstRodRevisionBeforePack);
		const FCatRunInventorySlot* ReturnedSecond = Equipment->GetSnapshot().InventorySlots.FindByPredicate(
			[SecondItemId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == SecondItemId; });
		if (!TestNotNull(TEXT("X returns the same second physical inventory instance"), ReturnedSecond)) return false;
		TestEqual(TEXT("returned second rod preserves definition"), ReturnedSecond->DefinitionId, SecondInventoryItem.DefinitionId);
		TestEqual(TEXT("returned second rod preserves durability"), ReturnedSecond->RodDurability, SecondInventoryItem.RodDurability);
		TestEqual(TEXT("returned second rod preserves quantity"), ReturnedSecond->Quantity, 1);
		TestFalse(TEXT("packing second never returns first rod's instance"), Equipment->GetSnapshot().InventorySlots.ContainsByPredicate(
			[ItemId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == ItemId; }));

		const FCatFishingInputEdge RedeployEdge = Commands->SubmitRodInteract();
		FCatFishingCommandResult Redeployed;
		if (!TestTrue(TEXT("freed slot deployment has a result"), Commands->TryGetResult(RedeployEdge.RequestId, Redeployed))
			|| !TestTrue(TEXT("packing frees a slot for another held rod"), Redeployed.bCommitted)) return false;
		ACatFishingRodActor* RedeployedRod = Fishing->FindDeployedRodById(Redeployed.RodActorId);
		if (!TestNotNull(TEXT("replacement rod is registered"), RedeployedRod)) return false;
		const FCatFishingInputEdge PackHeldEdge = Commands->SubmitCancel();
		FCatFishingCommandResult PackHeld;
		if (!TestTrue(TEXT("held X has a correlated result"), Commands->TryGetResult(PackHeldEdge.RequestId, PackHeld))
			|| !TestTrue(TEXT("held X releases and packs its own current rod"), PackHeld.bCommitted)) return false;
		TestEqual(TEXT("held X targets the operated rod, regardless of registry order"), PackHeld.RodActorId, Redeployed.RodActorId);
		TestEqual(TEXT("held X still leaves first rod registered"), Fishing->FindDeployedRod(Player), Rod);
		TestNull(TEXT("held X leaves no operator"), Fishing->FindRodOperatedBy(Player));

		const FCatFishingInputEdge BeforeDestroyEdge = Commands->SubmitRodInteract();
		FCatFishingCommandResult BeforeDestroy;
		if (!TestTrue(TEXT("cleanup fixture redeployment has a result"), Commands->TryGetResult(BeforeDestroyEdge.RequestId, BeforeDestroy))
			|| !TestTrue(TEXT("cleanup fixture restores two deployed rods"), BeforeDestroy.bCommitted)) return false;
		ACatFishingRodActor* DestroyedRod = Fishing->FindDeployedRodById(BeforeDestroy.RodActorId);
		if (!TestNotNull(TEXT("cleanup fixture has an operated rod"), DestroyedRod)) return false;
		TestTrue(TEXT("operated rod can be destroyed"), DestroyedRod->Destroy());
		TestEqual(TEXT("EndPlay removes only the destroyed rod's registration"), Fishing->GetDeployedRodCount(Player), 1);
		TestEqual(TEXT("EndPlay preserves the first grounded rod"), Fishing->FindDeployedRod(Player), Rod);
		TestNull(TEXT("EndPlay clears the destroyed rod's operator"), Fishing->FindRodOperatedBy(Player));
		TestTrue(TEXT("final grounded rod can be destroyed"), Rod->Destroy());
		TestEqual(TEXT("final destruction leaves no deployed registry entries"), Fishing->GetDeployedRodCountForDiagnostics(), 0);
	}
	return !HasAnyErrors();
}

#endif
