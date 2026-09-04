#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "OnlineSubsystemTypes.h"

namespace CatFishingBrokenRodPackTests
{
	struct FFixture
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		int32 SavedCapacity = Settings->InventorySlotCapacity;
		FTestWorldWrapper WorldWrapper;
		ACatfishingPlayerController* Controller = nullptr;
		ACatfishingPlayerState* PlayerState = nullptr;
		ACatCharacter* Character = nullptr;
		ACatFishingRodActor* Rod = nullptr;
		UCatEquipmentComponent* Equipment = nullptr;
		UCatFishingService* Fishing = nullptr;
		FGuid RodItemId;

		~FFixture() { Settings->InventorySlotCapacity = SavedCapacity; }

		bool Initialize(FAutomationTestBase& Test)
		{
			Settings->InventorySlotCapacity = 3;
			if (!Test.TestTrue(TEXT("creates a real service world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			if (!Test.TestTrue(TEXT("starts actor presentation lifecycle"), WorldWrapper.BeginPlayInTestWorld())) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			Fishing = World->GetSubsystem<UCatFishingService>();
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			Character = World->SpawnActor<ACatCharacter>();
			Rod = World->SpawnActor<ACatFishingRodActor>();
			if (!Test.TestTrue(TEXT("creates service, controller, character and rod"),
				Fishing && Controller && PlayerState && Character && Rod)) return false;
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("BrokenRodPackOwner"), FName(TEXT("CAT_TEST")));
			PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			Controller->PlayerState = PlayerState;
			Character->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			Equipment = Character->GetEquipmentComponent();
			if (!Test.TestNotNull(TEXT("character has equipment"), Equipment)) return false;
			for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
			{
				if (!Test.TestTrue(TEXT("grants formal equipment"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("grants two bait uses"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 2).bCommitted)) return false;
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			RodItemId = Loadout.RodItemInstanceId;
			if (!Test.TestTrue(TEXT("deploys the real rod instance"), Equipment->Use(
				FGuid::NewGuid(), Loadout.Revision, RodItemId).bCommitted)) return false;
			if (!Test.TestTrue(TEXT("initializes deployed owner rod without operators"), Rod->InitializeAuthoritativeIdentity(
				FGuid::NewGuid(), RodItemId, Loadout.RodDefinitionId, NAME_None, PlayerState, nullptr, true, false))) return false;
			if (!Test.TestTrue(TEXT("registers the deployed rod"), Fishing->RegisterDeployedRod(PlayerState, Rod))) return false;
			Rod->SetActorLocation(Character->GetActorLocation() + FVector(80.0, 0.0, 0.0));
			const FGuid SessionId = FGuid::NewGuid();
			if (!Test.TestTrue(TEXT("reserves a real fishing session"), Equipment->BeginFishingUse(SessionId,
				RodItemId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId,
				Equipment->GetSnapshot().Revision).bReserved)) return false;
			if (!Test.TestTrue(TEXT("commits bait"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
			const FCatFishingUseOperationResult Worn = Equipment->ApplyFishingRodWear(SessionId, 1, Loadout.RodDurability + 1.0);
			if (!Test.TestTrue(TEXT("wear breaks the actual inventory instance"), Worn.bApplied && Worn.bRodBroken)) return false;
			if (!Test.TestTrue(TEXT("publishes matching broken actor state"), Rod->SetBrokenFromAuthority(
				true, Rod->GetPresentationState().RodActorRevision))) return false;
			return Test.TestTrue(TEXT("terminal fight releases its reservation"), Equipment->ReleaseFishingUse(SessionId).bApplied);
		}

		FCatPackRodCommand PackCommand() const
		{
			FCatPackRodCommand Command;
			Command.Context.RequestId = FGuid::NewGuid();
			Command.Context.RodActorId = Rod->GetPresentationState().RodActorId;
			Command.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
			return Command;
		}

		int32 CountStoredRod() const
		{
			int32 Count = 0;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			{
				if (Slot.ItemInstanceId == RodItemId && Slot.Quantity > 0) ++Count;
			}
			return Count;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBrokenRodPackCapacityTest,
	"Catfishing.Unit.Fishing.Service.BrokenRodPackRestoresWorldWhenInventoryIsFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBrokenRodPackCapacityTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBrokenRodPackTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	TestEqual(TEXT("owner lookup used by X input retains broken rod"), Fixture.Fishing->FindDeployedRod(Fixture.PlayerState), Fixture.Rod);
	TestNull(TEXT("broken rod is absent from new operator candidates"),
		Fixture.Fishing->FindNearestOperableRod(Fixture.Character->GetActorLocation(), 250.0));
	int32 SlotIndex = INDEX_NONE;
	TestFalse(TEXT("broken deployed rod cannot accept another cast operator"), Fixture.Rod->AddOperatorFromAuthority(
		Fixture.PlayerState, Fixture.Rod->GetPresentationState().RodActorRevision, SlotIndex));
	if (!TestTrue(TEXT("fills the slot freed by deployment"), Fixture.Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(), Fixture.Equipment->GetSnapshot().Revision, TEXT("FeatherFloat")).bCommitted)) return false;
	const int64 EquipmentRevision = Fixture.Equipment->GetSnapshot().Revision;
	const int64 RodRevision = Fixture.Rod->GetPresentationState().RodActorRevision;
	AddExpectedErrorPlain(TEXT("Event=fishing_rod_pack_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCatFishingCommandResult Rejected = Fixture.Fishing->PackRod(Fixture.Controller, Fixture.PackCommand());
	TestFalse(TEXT("full inventory rejects pack"), Rejected.bCommitted);
	TestEqual(TEXT("capacity rejection keeps its domain error"), Rejected.Error, ECatFishingCommandError::GuardCapacityExceeded);
	TestTrue(TEXT("rejected pack restores deployed state"), Fixture.Rod->GetPresentationState().bDeployed);
	TestFalse(TEXT("rejected pack keeps the world rod visible"), Fixture.Rod->IsHidden());
	TestTrue(TEXT("rejected pack keeps world collision"), Fixture.Rod->GetActorEnableCollision());
	TestTrue(TEXT("rejected pack keeps broken state"), Fixture.Rod->GetPresentationState().bBroken);
	TestEqual(TEXT("inventory never receives a duplicate"), Fixture.CountStoredRod(), 0);
	TestEqual(TEXT("capacity rejection never changes inventory revision"), Fixture.Equipment->GetSnapshot().Revision, EquipmentRevision);
	TestTrue(TEXT("reply includes the restored actor revision"), Rejected.RodActorRevision > RodRevision);
	TestEqual(TEXT("reply revision matches current actor"), Rejected.RodActorRevision, Fixture.Rod->GetPresentationState().RodActorRevision);
	TestEqual(TEXT("failed pack preserves service ownership lookup"), Fixture.Fishing->FindDeployedRod(Fixture.PlayerState), Fixture.Rod);
	Fixture.Settings->InventorySlotCapacity = 4;
	const FCatFishingCommandResult Packed = Fixture.Fishing->PackRod(Fixture.Controller, Fixture.PackCommand());
	TestTrue(TEXT("new request can pack the same broken rod after capacity is available"), Packed.bCommitted);
	TestEqual(TEXT("successful retry returns exactly the same instance once"), Fixture.CountStoredRod(), 1);
	TestFalse(TEXT("packed actor is no longer deployed"), Fixture.Rod->GetPresentationState().bDeployed);
	TestTrue(TEXT("packed actor is hidden during replication grace period"), Fixture.Rod->IsHidden());
	TestNull(TEXT("successful pack removes world ownership lookup"), Fixture.Fishing->FindDeployedRod(Fixture.PlayerState));
	TestEqual(TEXT("packed broken rod is not repaired"), Fixture.Equipment->GetSnapshot().RodDurability, 0.0);
	TestFalse(TEXT("broken inventory rod cannot be deployed again"), Fixture.Equipment->Use(
		FGuid::NewGuid(), Fixture.Equipment->GetSnapshot().Revision, Fixture.RodItemId).bCommitted);
	TestEqual(TEXT("failed redeployment preserves the one broken inventory instance"), Fixture.CountStoredRod(), 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBrokenRodPackObserverTest,
	"Catfishing.Unit.Fishing.Service.BrokenRodPackSurvivesInventoryObserverAndReentrantRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBrokenRodPackObserverTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBrokenRodPackTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	bool bObserverRan = false;
	bool bObserverSawPackedRod = false;
	bool bActorRevisionAdvanced = false;
	FCatFishingCommandResult NestedPack;
	const FDelegateHandle Observer = Fixture.Equipment->OnSnapshotChanged.AddLambda([&]()
	{
		bObserverRan = true;
		bObserverSawPackedRod = !Fixture.Rod->GetPresentationState().bDeployed;
		// 复现旧顺序风险：库存通知中的合法 Actor 写入会推进 Revision，但不应使已经归还的坏竿回滚 Use。
		bActorRevisionAdvanced = Fixture.Rod->SetRodSkinFromAuthority(TEXT("PackObserverSkin"),
			Fixture.Rod->GetPresentationState().RodActorRevision);
		NestedPack = Fixture.Fishing->PackRod(Fixture.Controller, Fixture.PackCommand());
	});
	const FCatFishingCommandResult Packed = Fixture.Fishing->PackRod(Fixture.Controller, Fixture.PackCommand());
	Fixture.Equipment->OnSnapshotChanged.Remove(Observer);
	TestTrue(TEXT("inventory observer ran during the real return"), bObserverRan);
	TestTrue(TEXT("observer sees world rod already withdrawn"), bObserverSawPackedRod);
	TestTrue(TEXT("observer exercised an actor revision change"), bActorRevisionAdvanced);
	TestTrue(TEXT("original pack completes despite observer revision change"), Packed.bCommitted);
	TestFalse(TEXT("nested pack cannot return or restore the item again"), NestedPack.bCommitted);
	TestEqual(TEXT("nested pack observes resolved world state"), NestedPack.Error, ECatFishingCommandError::AlreadyResolved);
	TestEqual(TEXT("one inventory copy remains"), Fixture.CountStoredRod(), 1);
	TestFalse(TEXT("no deployed world copy remains"), Fixture.Rod->GetPresentationState().bDeployed);
	TestTrue(TEXT("world copy is hidden for its terminal replication window"), Fixture.Rod->IsHidden());
	TestNull(TEXT("service no longer exposes the packed actor"), Fixture.Fishing->FindDeployedRod(Fixture.PlayerState));
	TestTrue(TEXT("packed instance retains broken fact"), Fixture.Equipment->GetSnapshot().bRodBroken);
	TestEqual(TEXT("packed instance retains zero durability"), Fixture.Equipment->GetSnapshot().RodDurability, 0.0);
	return !HasAnyErrors();
}

#endif
