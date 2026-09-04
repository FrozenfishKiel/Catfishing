#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace CatRodDurabilityTests
{
	// 只替换本测试的目录配置；所有实例、会话、磨损和维修均经过正式公开入口。
	struct FFixture
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions = Settings->Definitions;
		FName SavedDriftwoodId = Settings->DriftwoodDefinitionId;
		ECatDomainPolicy SavedTrustPolicy = Settings->ProfileLoadoutTrustPolicy;
		int32 SavedCapacity = Settings->InventorySlotCapacity;
		int32 SavedStackCapacity = Settings->InventoryQuantityStackCapacity;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
		FTestWorldWrapper WorldWrapper;
		ACatCharacter* Character = nullptr;
		UCatEquipmentComponent* Equipment = nullptr;
		FGuid RodId;

		~FFixture()
		{
			Settings->Definitions = SavedDefinitions;
			Settings->DriftwoodDefinitionId = SavedDriftwoodId;
			Settings->ProfileLoadoutTrustPolicy = SavedTrustPolicy;
			Settings->InventorySlotCapacity = SavedCapacity;
			Settings->InventoryQuantityStackCapacity = SavedStackCapacity;
		}

		UCatEquipmentDefinition* AddDefinition(const FName Id, const ECatEquipmentKind Kind)
		{
			UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
			Definitions.Emplace(Definition);
			Definition->EquipmentDefinitionId = Id;
			Definition->Kind = Kind;
			Definition->FunctionalRouteId = Id;
			Definition->LoadoutSlotId = Id;
			Definition->bEnableRuntimeDefinition = true;
			Settings->Definitions.Add(TSoftObjectPtr<UCatEquipmentDefinition>(Definition));
			return Definition;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			Settings->Definitions.Reset();
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			Settings->InventorySlotCapacity = 12;
			Settings->InventoryQuantityStackCapacity = 20;
			Settings->DriftwoodDefinitionId = TEXT("DurabilityTestWood");
			UCatEquipmentDefinition* Rod = AddDefinition(TEXT("DurabilityTestRod"), ECatEquipmentKind::Rod);
			Rod->MaximumRodDurability = 100.0;
			Rod->FishingStrength = 50.0;
			Rod->MaximumLineLengthCentimeters = 1500.0;
			Rod->HighTensionWearMultiplier = 1.0;
			Rod->UseActorClass = ACatFishingRodActor::StaticClass();
			Rod->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
			UCatEquipmentDefinition* Bait = AddDefinition(TEXT("DurabilityTestBait"), ECatEquipmentKind::Bait);
			Bait->bRunConsumable = true;
			Bait->BiteRateMultiplier = 1.0;
			Bait->MinimumBiteDelayMultiplier = 1.0;
			UCatEquipmentDefinition* Float = AddDefinition(TEXT("DurabilityTestFloat"), ECatEquipmentKind::Float);
			Float->MaximumCastDistanceCentimeters = 1000.0;
			AddDefinition(Settings->DriftwoodDefinitionId, ECatEquipmentKind::Driftwood)->bRunConsumable = true;
			for (const TStrongObjectPtr<UCatEquipmentDefinition>& Definition : Definitions)
			{
				if (!Test.TestTrue(TEXT("temporary equipment definition is complete"), Definition->IsRuntimeDefinitionReady())) return false;
			}
			if (!Test.TestTrue(TEXT("creates authority equipment world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			UWorld* World = WorldWrapper.GetTestWorld();
			Character = World->SpawnActor<ACatCharacter>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			if (!Test.TestTrue(TEXT("spawns character and trusted player state"), Character && PlayerState)) return false;
			Character->SetPlayerState(PlayerState);
			Equipment = Character->GetEquipmentComponent();
			if (!Test.TestNotNull(TEXT("character owns real equipment component"), Equipment)) return false;
			for (const FName Id : {FName(TEXT("DurabilityTestRod")), FName(TEXT("DurabilityTestFloat"))})
			{
				if (!Test.TestTrue(TEXT("grants real equipment instance"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("grants bait for several sessions"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("DurabilityTestBait"), 8).bCommitted)) return false;
			RodId = Equipment->GetSnapshot().RodItemInstanceId;
			return Test.TestTrue(TEXT("grants full durable rod with stable instance identity"),
				RodId.IsValid() && Equipment->GetSnapshot().RodDurability == 100.0);
		}

		bool Deploy(FAutomationTestBase& Test, const FGuid Id)
		{
			return Test.TestTrue(TEXT("deploys the existing inventory instance"), Equipment->Use(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted);
		}

		bool Begin(FAutomationTestBase& Test, const FGuid SessionId, const bool bCommitBait = true)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			if (!Test.TestTrue(TEXT("reserves fishing use of the deployed rod"), Equipment->BeginFishingUse(SessionId,
				Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId,
				Loadout.Revision).bReserved)) return false;
			return !bCommitBait || Test.TestTrue(TEXT("commits this session bait"),
				Equipment->CommitFishingBaitDeferred(SessionId).bApplied);
		}

		const FCatRunInventorySlot* FindRod(const FGuid Id) const
		{
			return Equipment->GetSnapshot().InventorySlots.FindByPredicate(
				[Id](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == Id; });
		}

		int32 WoodQuantity() const
		{
			int32 Quantity = 0;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			{
				if (Slot.DefinitionId == Settings->DriftwoodDefinitionId) Quantity += Slot.Quantity;
			}
			return Quantity;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodCumulativeWearTest,
	"Catfishing.Unit.Equipment.RodDurability.CumulativeWearIsImmediateAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodCumulativeWearTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid SessionId = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId)
		|| !Fixture.Begin(*this, SessionId, false)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_wear_rejected"), EAutomationExpectedErrorFlags::Contains, 10);
	TestEqual(TEXT("wear cannot precede bait commitment"), Equipment->ApplyFishingRodWear(SessionId, 1, 12.5).Error,
		ECatDomainCommandError::InvalidPhase);
	if (!TestTrue(TEXT("commits bait before active fight"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
	const FCatFishingUseOperationResult First = Equipment->ApplyFishingRodWear(SessionId, 1, 12.5);
	TestTrue(TEXT("first cumulative sample applies immediately"), First.bApplied);
	TestEqual(TEXT("first result reads instance durability"), First.RemainingRodDurability, 87.5);
	TestEqual(TEXT("public snapshot changes before session close"), Equipment->GetSnapshot().RodDurability, 87.5);
	const int64 FirstRevision = Equipment->GetSnapshot().Revision;
	const FCatFishingUseOperationResult Replay = Equipment->ApplyFishingRodWear(SessionId, 1, 12.5);
	TestEqual(TEXT("same sequence and total is a replay"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("replay does not apply twice"), Replay.bApplied);
	TestEqual(TEXT("replay does not publish a new revision"), Equipment->GetSnapshot().Revision, FirstRevision);
	TestEqual(TEXT("changed payload cannot reuse a sequence"), Equipment->ApplyFishingRodWear(SessionId, 1, 15.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("missing intermediate sequence is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3, 20.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestTrue(TEXT("next cumulative sample applies"), Equipment->ApplyFishingRodWear(SessionId, 2, 20.0).bApplied);
	TestEqual(TEXT("second sample deducts only its delta"), Equipment->GetSnapshot().RodDurability, 80.0);
	const int64 AcceptedRevision = Equipment->GetSnapshot().Revision;
	TestEqual(TEXT("out of order older sample is rejected"), Equipment->ApplyFishingRodWear(SessionId, 1, 12.5).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("cumulative total cannot decrease"), Equipment->ApplyFishingRodWear(SessionId, 3, 19.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("negative total is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3, -1.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("non finite total is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3,
		std::numeric_limits<double>::quiet_NaN()).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("unknown session has no write authority"), Equipment->ApplyFishingRodWear(FGuid::NewGuid(), 1, 5.0).Error,
		ECatDomainCommandError::NotFound);
	Fixture.Character->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("client cannot mutate a valid server session"), Equipment->ApplyFishingRodWear(SessionId, 3, 25.0).Error,
		ECatDomainCommandError::DependencyUnavailable);
	Fixture.Character->SetRole(ROLE_Authority);
	TestEqual(TEXT("all rejected writes preserve durability"), Equipment->GetSnapshot().RodDurability, 80.0);
	TestEqual(TEXT("all rejected writes preserve revision"), Equipment->GetSnapshot().Revision, AcceptedRevision);
	double Durability = -1.0;
	bool bBroken = true;
	TestTrue(TEXT("session reads its bound deployed instance"), Equipment->GetFishingRodDurability(SessionId, Durability, bBroken));
	TestEqual(TEXT("bound instance sees cumulative wear"), Durability, 80.0);
	TestFalse(TEXT("positive durability remains usable"), bBroken);
	TestTrue(TEXT("release closes session without another wear deduction"), Equipment->ReleaseFishingUse(SessionId).bApplied);
	TestEqual(TEXT("late released session wear cannot change the rod"), Equipment->ApplyFishingRodWear(SessionId, 3, 25.0).Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("release preserves the already deducted durability"), Equipment->GetSnapshot().RodDurability, 80.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodDurabilityAcrossSessionsTest,
	"Catfishing.Unit.Equipment.RodDurability.PersistsAcrossSessionsAndRedeployment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodDurabilityAcrossSessionsTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid FirstSession = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, FirstSession)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	if (!TestTrue(TEXT("first fight wears the rod"), Equipment->ApplyFishingRodWear(FirstSession, 1, 35.0).bApplied)) return false;
	TestTrue(TEXT("first fight closes"), Equipment->ReleaseFishingUse(FirstSession).bApplied);
	const FCatInventoryItemUseResult Recalled = Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId);
	if (!TestTrue(TEXT("recalls worn instance"), Recalled.bCommitted)) return false;
	TestEqual(TEXT("recall retains the original instance"), Recalled.Item.ItemInstanceId, Fixture.RodId);
	TestEqual(TEXT("recall returns worn durability"), Recalled.Item.RodDurability, 65.0);
	const FCatRunInventorySlot* Slot = Fixture.FindRod(Fixture.RodId);
	if (!TestNotNull(TEXT("same rod reappears in inventory"), Slot)) return false;
	TestEqual(TEXT("inventory is not restored to definition maximum"), Slot->RodDurability, 65.0);
	if (!Fixture.Deploy(*this, Fixture.RodId)) return false;
	const FGuid SecondSession = FGuid::NewGuid();
	if (!Fixture.Begin(*this, SecondSession)) return false;
	double Durability = 0.0;
	bool bBroken = true;
	TestTrue(TEXT("next session binds the original instance"), Equipment->GetFishingRodDurability(SecondSession, Durability, bBroken));
	TestEqual(TEXT("next fight starts with remaining durability"), Durability, 65.0);
	TestTrue(TEXT("new session has its own cumulative wear counter"), Equipment->ApplyFishingRodWear(SecondSession, 1, 10.0).bApplied);
	TestEqual(TEXT("both sessions accumulate on one rod"), Equipment->GetSnapshot().RodDurability, 55.0);
	TestTrue(TEXT("second fight closes"), Equipment->ReleaseFishingUse(SecondSession).bApplied);
	const FCatInventoryItemUseResult RecalledAgain = Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId);
	TestTrue(TEXT("second recall succeeds"), RecalledAgain.bCommitted);
	TestEqual(TEXT("second recall preserves total lifetime wear"), RecalledAgain.Item.RodDurability, 55.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBrokenRodRepairTest,
	"Catfishing.Unit.Equipment.RodDurability.BrokenInstanceRequiresRepairAndRetainsIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBrokenRodRepairTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid SessionId = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, SessionId)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	const FCatFishingUseOperationResult Break = Equipment->ApplyFishingRodWear(SessionId, 1, 120.0);
	TestTrue(TEXT("wear reaching zero marks the instance broken immediately"), Break.bApplied && Break.bRodBroken);
	TestEqual(TEXT("overdraw clamps remaining durability to zero"), Break.RemainingRodDurability, 0.0);
	TestTrue(TEXT("wear leaves session cleanup to its owner"), Equipment->IsFishingUseActive(SessionId));
	TestTrue(TEXT("public broken fact is already visible"), Equipment->GetSnapshot().bRodBroken);
	TestTrue(TEXT("broken session releases"), Equipment->ReleaseFishingUse(SessionId).bApplied);
	const FCatInventoryItemUseResult Recalled = Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId);
	if (!TestTrue(TEXT("broken rod can be returned for repair"), Recalled.bCommitted)) return false;
	TestTrue(TEXT("returned item is still broken"), Recalled.Item.bRodBroken);
	TestEqual(TEXT("returned broken item remains empty"), Recalled.Item.RodDurability, 0.0);
	TestFalse(TEXT("redeployment cannot heal a broken rod"), Equipment->Use(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Fixture.RodId).bCommitted);
	const FCatRunInventorySlot* BrokenSlot = Fixture.FindRod(Fixture.RodId);
	if (!TestNotNull(TEXT("rejected deployment retains the broken inventory item"), BrokenSlot)) return false;
	TestTrue(TEXT("normalization preserves zero durability and broken state"), BrokenSlot->bRodBroken && BrokenSlot->RodDurability == 0.0);
	if (!TestTrue(TEXT("grants repair material"), Equipment->GrantInventoryQuantityFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Fixture.Settings->DriftwoodDefinitionId, 2).bCommitted)) return false;
	const FGuid RepairRequest = FGuid::NewGuid();
	const int64 RepairRevision = Equipment->GetSnapshot().Revision;
	TestTrue(TEXT("camp repair restores the selected existing instance"), Equipment->RepairRodAtCamp(
		RepairRequest, RepairRevision, true).bCommitted);
	TestEqual(TEXT("repair preserves identity"), Equipment->GetSnapshot().RodItemInstanceId, Fixture.RodId);
	TestEqual(TEXT("repair restores the maximum durability"), Equipment->GetSnapshot().RodDurability, 100.0);
	TestFalse(TEXT("repair clears broken state"), Equipment->GetSnapshot().bRodBroken);
	TestEqual(TEXT("repair consumes exactly one material"), Fixture.WoodQuantity(), 1);
	TestFalse(TEXT("replayed repair does not apply again"), Equipment->RepairRodAtCamp(RepairRequest, RepairRevision, true).bCommitted);
	TestEqual(TEXT("replayed repair cannot consume another material"), Fixture.WoodQuantity(), 1);
	const FCatRunInventorySlot* RepairedSlot = Fixture.FindRod(Fixture.RodId);
	if (!TestNotNull(TEXT("repaired instance remains in inventory"), RepairedSlot)) return false;
	TestTrue(TEXT("inventory receives the repair fact"), !RepairedSlot->bRodBroken && RepairedSlot->RodDurability == 100.0);
	if (!Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, FGuid::NewGuid())) return false;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPurchasedRodIndependenceTest,
	"Catfishing.Unit.Equipment.RodDurability.NewPurchaseAndOldSessionRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPurchasedRodIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid OldSession = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, OldSession)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	if (!TestTrue(TEXT("old rod receives wear"), Equipment->ApplyFishingRodWear(OldSession, 1, 30.0).bApplied)) return false;
	TestTrue(TEXT("old session releases"), Equipment->ReleaseFishingUse(OldSession).bApplied);
	if (!TestTrue(TEXT("old rod returns to inventory"), Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId).bCommitted)) return false;
	if (!TestTrue(TEXT("shop grant creates another rod instance"), Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("DurabilityTestRod")).bCommitted)) return false;
	FGuid NewRodId;
	int32 RodCount = 0;
	for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
	{
		if (Slot.DefinitionId != FName(TEXT("DurabilityTestRod"))) continue;
		++RodCount;
		if (Slot.ItemInstanceId != Fixture.RodId)
		{
			NewRodId = Slot.ItemInstanceId;
			TestEqual(TEXT("only new instance starts full"), Slot.RodDurability, 100.0);
		}
	}
	if (!TestEqual(TEXT("both purchased instances coexist"), RodCount, 2)
		|| !TestTrue(TEXT("new purchase has a separate identity"), NewRodId.IsValid())) return false;
	const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
	if (!TestTrue(TEXT("selects new rod by its exact instance"), Equipment->ConfigureLoadoutFromAuthority(
		FGuid::NewGuid(), Loadout.Revision, Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
		Loadout.FloatDefinitionId, NAME_None, NAME_None, NewRodId, Loadout.BaitItemInstanceId,
		Loadout.FloatItemInstanceId).bCommitted)) return false;
	double OldDurability = 0.0;
	bool bOldBroken = true;
	TestTrue(TEXT("old session still reads its bound old rod"), Equipment->GetFishingRodDurability(OldSession, OldDurability, bOldBroken));
	TestEqual(TEXT("changing selection cannot redirect the session lookup"), OldDurability, 70.0);
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_wear_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCatFishingUseOperationResult Late = Equipment->ApplyFishingRodWear(OldSession, 2, 60.0);
	TestEqual(TEXT("late wear from old session is terminal"), Late.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("late result reports the old bound rod"), Late.RemainingRodDurability, 70.0);
	TestEqual(TEXT("late old wear cannot damage newly selected rod"), Equipment->GetSnapshot().RodDurability, 100.0);
	const FCatRunInventorySlot* OldSlot = Fixture.FindRod(Fixture.RodId);
	if (!TestNotNull(TEXT("old worn rod remains separately stored"), OldSlot)) return false;
	TestEqual(TEXT("buying another rod never repairs the old one"), OldSlot->RodDurability, 70.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodSessionDurabilityTest,
	"Catfishing.Unit.Equipment.RodDurability.FightWritesSameInstanceAndBreakReleasesOperators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodSessionDurabilityTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid FirstId = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId)
		|| !Fixture.Begin(*this, FirstId)) return false;
	UWorld* World = Fixture.WorldWrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	APlayerState* Owner = Fixture.Character->GetPlayerState();
	if (!TestTrue(TEXT("initializes the deployed rod projection"), Rod && Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), Fixture.RodId, TEXT("DurabilityTestRod"), TEXT("TestSkin"), Owner, Owner, true, false))) return false;
	const auto MakeSession = [&](const FGuid Id)
	{
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		Session->Snapshot.FishingSessionId = Id;
		Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
		Session->Snapshot.RodActor = Rod;
		Session->AttemptSnapshot.RodItemInstanceId = Fixture.RodId;
		Session->CastEquipment = Fixture.Equipment;
		Session->FightRunner = NewObject<UCatFishingFightRunner>(Session);
		return Session;
	};
	ACatFishingSession* First = MakeSession(FirstId);
	FCatFightStepResult Step;
	Step.RodWearDelta = 25.0;
	Step.AbsoluteRodWear = 25.0;
	First->HandleFightRunnerStepFromAuthority(Step, 80.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("fight snapshot mirrors immediately written instance durability"), First->GetSnapshot().RodDurabilityRemaining, 75.0);
	TestEqual(TEXT("fight wear is already in equipment before termination"), Fixture.Equipment->GetSnapshot().RodDurability, 75.0);
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedErrorPlain(TEXT("Event=fishing_rod_broken"), EAutomationExpectedErrorFlags::Contains, 1);
	First->FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Cancelled, TEXT("durability test cancel"));
	First->HandleFightRunnerStepFromAuthority(Step, 80.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("cancel and late step neither refund nor duplicate wear"), Fixture.Equipment->GetSnapshot().RodDurability, 75.0);
	const FGuid SecondId = FGuid::NewGuid();
	if (!Fixture.Begin(*this, SecondId)) return false;
	ACatFishingSession* Second = MakeSession(SecondId);
	Step.RodWearDelta = 75.0;
	Step.AbsoluteRodWear = 75.0;
	Step.Outcome = ECatFightStepOutcome::RodBroken;
	Second->HandleFightRunnerStepFromAuthority(Step, 0.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("depletion is a rod-broken terminal"), Second->GetSnapshot().Outcome, ECatFishingOutcome::RodBroken);
	TestEqual(TEXT("terminal mirror stays at zero"), Second->GetSnapshot().RodDurabilityRemaining, 0.0);
	TestTrue(TEXT("the real item is broken"), Fixture.Equipment->GetSnapshot().bRodBroken);
	TestTrue(TEXT("the deployed rod replicates broken state"), Rod->GetPresentationState().bBroken);
	TestEqual(TEXT("broken rod releases every operator"), Rod->GetOperatorCount(), 0);
	TestFalse(TEXT("broken session no longer blocks inventory recall"), Fixture.Equipment->HasActiveFishingUse());
	return !HasAnyErrors();
}

#endif
