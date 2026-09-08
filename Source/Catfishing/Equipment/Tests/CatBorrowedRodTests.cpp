#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/Inventory/CatInventoryTransferService.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "UObject/StrongObjectPtr.h"

namespace CatBorrowedRodTests
{
	const FName OwnerRodDefinitionId(TEXT("BorrowedOwnerRod"));
	const FName OtherRodDefinitionId(TEXT("BorrowedOtherRod"));
	const FName OwnerBaitDefinitionId(TEXT("BorrowedOwnerBait"));
	const FName FisherBaitDefinitionId(TEXT("BorrowedFisherBait"));

	bool SameSnapshot(const FCatEquipmentLoadoutSnapshot& A, const FCatEquipmentLoadoutSnapshot& B)
	{
		return FCatEquipmentLoadoutSnapshot::StaticStruct()->CompareScriptStruct(&A, &B, 0);
	}

	int32 Quantity(const UCatEquipmentComponent* Equipment, const FName DefinitionId)
	{
		int32 Total = 0;
		for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			if (Slot.DefinitionId == DefinitionId) Total += Slot.Quantity;
		return Total;
	}

	struct FFixture
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions = Settings->Definitions;
		ECatDomainPolicy SavedTrust = Settings->ProfileLoadoutTrustPolicy;
		int32 SavedCapacity = Settings->InventorySlotCapacity;
		int32 SavedStackCapacity = Settings->InventoryQuantityStackCapacity;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
		FTestWorldWrapper Wrapper;
		UCatEquipmentComponent* Owner = nullptr;
		UCatEquipmentComponent* Fisher = nullptr;
		FGuid OwnerRodId;
		FGuid FisherRodId;

		~FFixture()
		{
			Settings->Definitions = SavedDefinitions;
			Settings->ProfileLoadoutTrustPolicy = SavedTrust;
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
			Settings->Definitions.Add(Definition);
			return Definition;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			Settings->Definitions.Reset();
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			Settings->InventorySlotCapacity = 12;
			Settings->InventoryQuantityStackCapacity = 20;
			for (const FName Id : {OwnerRodDefinitionId, OtherRodDefinitionId})
			{
				UCatEquipmentDefinition* Rod = AddDefinition(Id, ECatEquipmentKind::Rod);
				Rod->MaximumRodDurability = Id == OwnerRodDefinitionId ? 100.0 : 220.0;
				Rod->MaximumLineLengthCentimeters = 1500.0;
				Rod->HighTensionWearMultiplier = 1.0;
				Rod->UseActorClass = ACatFishingRodActor::StaticClass();
				Rod->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
			}
			for (const FName Id : {OwnerBaitDefinitionId, FisherBaitDefinitionId})
			{
				UCatEquipmentDefinition* Bait = AddDefinition(Id, ECatEquipmentKind::Bait);
				Bait->bRunConsumable = true;
				Bait->BiteRateMultiplier = 1.0;
				Bait->MinimumBiteDelayMultiplier = 1.0;
			}
			AddDefinition(TEXT("BorrowedOwnerFloat"), ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1200.0;
			AddDefinition(TEXT("BorrowedFisherFloat"), ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 700.0;
			for (const auto& Definition : Definitions)
				if (!Test.TestTrue(TEXT("borrowed rod definitions are complete"), Definition->IsRuntimeDefinitionReady())) return false;
			if (!Test.TestTrue(TEXT("creates authority equipment world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
			Wrapper.ForwardErrorMessages(&Test);
			const auto CreateEquipment = [&]() -> UCatEquipmentComponent*
			{
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ACatCharacter* Character = Wrapper.GetTestWorld()->SpawnActor<ACatCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
				ACatfishingPlayerState* Player = Wrapper.GetTestWorld()->SpawnActor<ACatfishingPlayerState>();
				if (!Character || !Player) return nullptr;
				Character->SetPlayerState(Player);
				return Character->GetEquipmentComponent();
			};
			Owner = CreateEquipment();
			Fisher = CreateEquipment();
			if (!Test.TestTrue(TEXT("two distinct authority inventory hosts exist"), Owner && Fisher && Owner != Fisher)) return false;
			for (UCatEquipmentComponent* Equipment : {Owner, Fisher})
			{
				const bool bOwner = Equipment == Owner;
				for (const FName Id : {bOwner ? OwnerRodDefinitionId : OtherRodDefinitionId,
					FName(bOwner ? TEXT("BorrowedOwnerFloat") : TEXT("BorrowedFisherFloat"))})
					if (!Test.TestTrue(TEXT("grants real tool instance"), Equipment->GrantEquipmentFromAuthority(
						FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
				if (!Test.TestTrue(TEXT("grants four player-specific bait portions"), Equipment->GrantInventoryQuantityFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, bOwner ? OwnerBaitDefinitionId : FisherBaitDefinitionId, 4).bCommitted)) return false;
			}
			OwnerRodId = Owner->GetSnapshot().RodItemInstanceId;
			FisherRodId = Fisher->GetSnapshot().RodItemInstanceId;
			return Test.TestTrue(TEXT("owner deploys a physical rod"), Owner->Use(
				FGuid::NewGuid(), Owner->GetSnapshot().Revision, OwnerRodId).bCommitted);
		}

		FCatFishingUseReservationResult Begin(const FGuid SessionId) const
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Fisher->GetSnapshot();
			return Fisher->BeginFishingUse(SessionId, OwnerRodId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				OwnerRodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision,
				Owner, Owner->GetSnapshot().Revision);
		}

		FCatInventoryTransferRequest ActiveTransfer() const
		{
			FCatInventoryTransferRequest Request;
			Request.RequestId = FGuid::NewGuid();
			Request.Initiator = Fisher->GetOwner();
			Request.Source.Host = Owner;
			Request.Source.Channel = TEXT("ActiveUse");
			Request.Source.EntryId = OwnerRodId;
			Request.Target.Host = Fisher;
			Request.ExpectedSourceRevision = Owner->GetSnapshot().Revision;
			Request.ExpectedTargetRevision = Fisher->GetSnapshot().Revision;
			Request.SourceSlotIndex = 0;
			Request.ExpectedSourceItemId = OwnerRodId;
			return Request;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodReservationTest,
	"Catfishing.Unit.Equipment.BorrowedRod.ReservationAndCancellationKeepResourceOwners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodReservationTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	FFixture F;
	if (!F.Initialize(*this)) return false;
	const FCatEquipmentLoadoutSnapshot BeforeOwner = F.Owner->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot BeforeFisher = F.Fisher->GetSnapshot();
	const FGuid SessionId = FGuid::NewGuid();
	const auto Began = F.Begin(SessionId);
	if (!TestTrue(TEXT("fisher without a deployed personal rod reserves owner's rod"), Began.bReserved)) return false;
	TestEqual(TEXT("coordinator freezes original rod host"), F.Fisher->GetFishingRodEquipment(SessionId), F.Owner);
	TestEqual(TEXT("reservation reports actual borrowed rod durability"), Began.RemainingRodDurability, 100.0);
	TestEqual(TEXT("fisher bait is reserved once"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	TestEqual(TEXT("owner bait is untouched"), Quantity(F.Owner, OwnerBaitDefinitionId), 4);
	TestEqual(TEXT("fisher float instance is unchanged"), F.Fisher->GetSnapshot().FloatItemInstanceId, BeforeFisher.FloatItemInstanceId);
	TestEqual(TEXT("owner float instance is unchanged"), F.Owner->GetSnapshot().FloatItemInstanceId, BeforeOwner.FloatItemInstanceId);
	TestEqual(TEXT("caller revision advances for bait"), F.Fisher->GetSnapshot().Revision, BeforeFisher.Revision + 1);
	TestEqual(TEXT("owner revision advances for rod lock"), F.Owner->GetSnapshot().Revision, BeforeOwner.Revision + 1);
	TestTrue(TEXT("both resource hosts report an active fishing use"), F.Owner->HasActiveFishingUse() && F.Fisher->HasActiveFishingUse());
	const auto Replayed = F.Fisher->BeginFishingUse(SessionId, F.OwnerRodId, BeforeFisher.BaitItemInstanceId,
		BeforeFisher.FloatItemInstanceId, OwnerRodDefinitionId, BeforeFisher.BaitDefinitionId, BeforeFisher.FloatDefinitionId,
		BeforeFisher.Revision, F.Owner, BeforeOwner.Revision);
	TestEqual(TEXT("same request has a stable replay"), Replayed.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("replay does not reserve a second bait"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	if (!TestTrue(TEXT("cancelling before bite releases both hosts"), F.Fisher->ReleaseFishingUse(SessionId).bApplied)) return false;
	TestFalse(TEXT("owner lock is released"), F.Owner->HasActiveFishingUse());
	TestFalse(TEXT("caller reservation is released"), F.Fisher->IsFishingUseActive(SessionId));
	TestEqual(TEXT("unused bait returns to fisher"), Quantity(F.Fisher, FisherBaitDefinitionId), 4);
	TestEqual(TEXT("owner never receives fisher bait"), Quantity(F.Owner, FisherBaitDefinitionId), 0);
	const FCatEquipmentLoadoutSnapshot ReleasedOwner = F.Owner->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot ReleasedFisher = F.Fisher->GetSnapshot();
	TestEqual(TEXT("second cancellation is resolved"), F.Fisher->ReleaseFishingUse(SessionId).Error, ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("second cancellation leaves both snapshots unchanged"), SameSnapshot(ReleasedOwner, F.Owner->GetSnapshot())
		&& SameSnapshot(ReleasedFisher, F.Fisher->GetSnapshot()));
	const auto Packed = F.Owner->UnUse(FGuid::NewGuid(), F.OwnerRodId);
	TestTrue(TEXT("original owner can pack rod after cancelled borrow"), Packed.bCommitted);
	TestEqual(TEXT("packing returns original physical rod"), Packed.Item.ItemInstanceId, F.OwnerRodId);
	TestEqual(TEXT("cancellation preserves original durability"), Packed.Item.RodDurability, 100.0);
	TestFalse(TEXT("borrowing never grants rod into fisher inventory"), F.Fisher->GetSnapshot().InventorySlots.ContainsByPredicate(
		[&](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == F.OwnerRodId; }));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodWearTest,
	"Catfishing.Unit.Equipment.BorrowedRod.WearUpdatesExactOwnerInstanceAndCallerReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodWearTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	FFixture F;
	if (!F.Initialize(*this)) return false;
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("reserves borrowed rod"), F.Begin(SessionId).bReserved)
		|| !TestTrue(TEXT("grants owner a different spare"), F.Owner->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), F.Owner->GetSnapshot().Revision, OtherRodDefinitionId).bCommitted)) return false;
	const FCatEquipmentLoadoutSnapshot CurrentOwner = F.Owner->GetSnapshot();
	const FCatRunInventorySlot* Spare = CurrentOwner.InventorySlots.FindByPredicate(
		[](const FCatRunInventorySlot& Slot) { return Slot.DefinitionId == OtherRodDefinitionId; });
	if (!TestNotNull(TEXT("owner spare exists"), Spare)) return false;
	const FGuid SpareId = Spare->ItemInstanceId;
	if (!TestTrue(TEXT("owner may select a different rod while original is borrowed"), F.Owner->ConfigureLoadoutFromAuthority(
		FGuid::NewGuid(), CurrentOwner.Revision, OtherRodDefinitionId, CurrentOwner.BaitDefinitionId, CurrentOwner.FloatDefinitionId,
		CurrentOwner.ScoopNetDefinitionId, CurrentOwner.RodSkinDefinitionId, SpareId, CurrentOwner.BaitItemInstanceId,
		CurrentOwner.FloatItemInstanceId, CurrentOwner.ScoopNetItemInstanceId).bCommitted)
		|| !TestTrue(TEXT("bite consumes fisher's reserved bait"), F.Fisher->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
	const int64 OwnerRevision = F.Owner->GetSnapshot().Revision;
	const FCatEquipmentLoadoutSnapshot BeforeFisher = F.Fisher->GetSnapshot();
	const auto Worn = F.Fisher->ApplyFishingRodWear(SessionId, 1, 12.5);
	TestTrue(TEXT("wear request applies through fisher coordinator"), Worn.bApplied);
	TestEqual(TEXT("wear reports original rod's remaining durability"), Worn.RemainingRodDurability, 87.5);
	TestEqual(TEXT("wear receipt revision is still caller inventory revision"), Worn.EquipmentRevision, BeforeFisher.Revision);
	TestEqual(TEXT("only actual owner revision advances"), F.Owner->GetSnapshot().Revision, OwnerRevision + 1);
	TestTrue(TEXT("fisher inventory and selected rod are unchanged by borrowed wear"), SameSnapshot(BeforeFisher, F.Fisher->GetSnapshot()));
	TestEqual(TEXT("owner's new selected rod is untouched"), F.Owner->GetSnapshot().RodDurability, 220.0);
	TestEqual(TEXT("wear replay cannot deduct again"), F.Fisher->ApplyFishingRodWear(SessionId, 1, 12.5).Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("wear replay preserves owner revision"), F.Owner->GetSnapshot().Revision, OwnerRevision + 1);
	double Durability = 0.0;
	bool bBroken = true;
	TestTrue(TEXT("coordinator reads durability from frozen owner"), F.Fisher->GetFishingRodDurability(SessionId, Durability, bBroken));
	TestEqual(TEXT("bound read finds original instance despite both selections"), Durability, 87.5);
	TestFalse(TEXT("borrowed rod remains usable"), bBroken);
	if (!TestTrue(TEXT("ending a consumed session releases rod lock"), F.Fisher->ReleaseFishingUse(SessionId).bApplied)) return false;
	TestEqual(TEXT("consumed bait is not refunded"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	TestEqual(TEXT("owner bait remains untouched"), Quantity(F.Owner, OwnerBaitDefinitionId), 4);
	const auto Packed = F.Owner->UnUse(FGuid::NewGuid(), F.OwnerRodId);
	TestTrue(TEXT("original owner receives worn physical rod"), Packed.bCommitted);
	TestEqual(TEXT("original item identity survives borrowing"), Packed.Item.ItemInstanceId, F.OwnerRodId);
	TestEqual(TEXT("wear persists in packed item"), Packed.Item.RodDurability, 87.5);
	TestEqual(TEXT("packing borrowed rod preserves owner's valid spare selection"), F.Owner->GetSnapshot().RodItemInstanceId, SpareId);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodRejectionTest,
	"Catfishing.Unit.Equipment.BorrowedRod.RejectsConflictsAndProtectsBothInventoryEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodRejectionTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_session_rejected"), EAutomationExpectedErrorFlags::Contains, 9);
	FFixture F;
	if (!F.Initialize(*this)) return false;
	const FCatEquipmentLoadoutSnapshot OwnerBefore = F.Owner->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot FisherBefore = F.Fisher->GetSnapshot();
	const auto Request = [&](const FGuid SessionId, UCatEquipmentComponent* RodHost, const FGuid RodId,
		const FGuid FloatId, const int64 FisherRevision, const int64 OwnerRevision)
	{
		return F.Fisher->BeginFishingUse(SessionId, RodId, FisherBefore.BaitItemInstanceId, FloatId,
			OwnerRodDefinitionId, FisherBefore.BaitDefinitionId, FisherBefore.FloatDefinitionId,
			FisherRevision, RodHost, OwnerRevision);
	};
	TestEqual(TEXT("stale owner version rejects before bait deduction"), Request(FGuid::NewGuid(), F.Owner, F.OwnerRodId,
		FisherBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision - 1).Error, ECatDomainCommandError::RevisionConflict);
	TestEqual(TEXT("stale caller version rejects before rod lock"), Request(FGuid::NewGuid(), F.Owner, F.OwnerRodId,
		FisherBefore.FloatItemInstanceId, FisherBefore.Revision - 1, OwnerBefore.Revision).Error, ECatDomainCommandError::RevisionConflict);
	F.Owner->GetOwner()->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("client rod host cannot grant a server reservation"), Request(FGuid::NewGuid(), F.Owner, F.OwnerRodId,
		FisherBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision).Error, ECatDomainCommandError::DependencyUnavailable);
	F.Owner->GetOwner()->SetRole(ROLE_Authority);
	TestEqual(TEXT("owner's float cannot replace caller selected float"), Request(FGuid::NewGuid(), F.Owner, F.OwnerRodId,
		OwnerBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision).Error, ECatDomainCommandError::InvalidPayload);
	TestTrue(TEXT("invalid requests preserve both full snapshots"), SameSnapshot(OwnerBefore, F.Owner->GetSnapshot())
		&& SameSnapshot(FisherBefore, F.Fisher->GetSnapshot()));
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("valid request still succeeds after rejections"), F.Begin(SessionId).bReserved)) return false;
	const FCatEquipmentLoadoutSnapshot LockedOwner = F.Owner->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot LockedFisher = F.Fisher->GetSnapshot();
	TestEqual(TEXT("session identity cannot switch owner"), Request(SessionId, F.Fisher, F.OwnerRodId,
		FisherBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("session identity cannot switch rod"), Request(SessionId, F.Owner, FGuid::NewGuid(),
		FisherBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("session identity cannot switch float"), Request(SessionId, F.Owner, F.OwnerRodId,
		OwnerBefore.FloatItemInstanceId, FisherBefore.Revision, OwnerBefore.Revision).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("same resource identities replay after native caller refreshes versions"), Request(SessionId, F.Owner, F.OwnerRodId,
		FisherBefore.FloatItemInstanceId, LockedFisher.Revision, LockedOwner.Revision).Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("refreshed-version replay never reserves another bait"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	TestEqual(TEXT("second caster reservation cannot reuse busy rod"), F.Begin(FGuid::NewGuid()).Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("original owner also cannot bind borrowed rod"), F.Owner->BeginFishingUse(FGuid::NewGuid(), F.OwnerRodId,
		LockedOwner.BaitItemInstanceId, LockedOwner.FloatItemInstanceId, OwnerRodDefinitionId,
		LockedOwner.BaitDefinitionId, LockedOwner.FloatDefinitionId, LockedOwner.Revision).Error, ECatDomainCommandError::InvalidPhase);
	AddExpectedErrorPlain(TEXT("Event=inventory_transfer_rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	UCatInventoryTransferService* Transfers = F.Wrapper.GetTestWorld()->GetSubsystem<UCatInventoryTransferService>();
	if (!TestNotNull(TEXT("generic inventory transfer service is available"), Transfers)) return false;
	TestEqual(TEXT("generic ActiveUse handoff sees the borrowed rod lock"), Transfers->TransferFromAuthority(F.ActiveTransfer()).Error,
		ECatDomainCommandError::InvalidPhase);
	TestFalse(TEXT("original owner's UnUse cannot bypass borrowed lock"), F.Owner->UnUse(FGuid::NewGuid(), F.OwnerRodId).bCommitted);
	TestTrue(TEXT("busy and changed-payload requests preserve both full snapshots"), SameSnapshot(LockedOwner, F.Owner->GetSnapshot())
		&& SameSnapshot(LockedFisher, F.Fisher->GetSnapshot()));
	if (!TestTrue(TEXT("original borrowed session remains independently releasable"), F.Fisher->ReleaseFishingUse(SessionId).bApplied)) return false;
	const auto Handoff = Transfers->TransferFromAuthority(F.ActiveTransfer());
	TestTrue(TEXT("release makes generic physical handoff available again"), Handoff.bCommitted);
	TestEqual(TEXT("handoff returns only the original rod instance"), Handoff.Item.ItemInstanceId, F.OwnerRodId);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodPublicationTest,
	"Catfishing.Unit.Equipment.BorrowedRod.PublicationCommitsBothHostsBeforeReentrantReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodPublicationTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	FFixture F;
	if (!F.Initialize(*this)) return false;
	const FGuid FirstSession = FGuid::NewGuid();
	const FGuid SecondSession = FGuid::NewGuid();
	int32 BeginNotifications = 0;
	const auto ObserveBegin = [&]()
	{
		++BeginNotifications;
		TestTrue(TEXT("each Begin observer sees caller reservation"), F.Fisher->IsFishingUseActive(FirstSession));
		TestTrue(TEXT("each Begin observer sees owner lock"), F.Owner->HasActiveFishingUse());
		TestEqual(TEXT("each Begin observer sees bait already reserved"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	};
	const FDelegateHandle OwnerBeginObserver = F.Owner->OnSnapshotChanged.AddLambda(ObserveBegin);
	const FDelegateHandle FisherBeginObserver = F.Fisher->OnSnapshotChanged.AddLambda(ObserveBegin);
	const auto Began = F.Begin(FirstSession);
	F.Owner->OnSnapshotChanged.Remove(OwnerBeginObserver);
	F.Fisher->OnSnapshotChanged.Remove(FisherBeginObserver);
	if (!TestTrue(TEXT("borrowed Begin commits"), Began.bReserved)) return false;
	TestEqual(TEXT("both changed hosts publish once"), BeginNotifications, 2);
	bool bReentered = false;
	const int64 BeforeRelease = F.Fisher->GetSnapshot().Revision;
	const FDelegateHandle ReleaseObserver = F.Fisher->OnSnapshotChanged.AddLambda([&]()
	{
		if (bReentered) return;
		bReentered = true;
		TestFalse(TEXT("Release observer sees old caller tombstone"), F.Fisher->IsFishingUseActive(FirstSession));
		TestFalse(TEXT("Release observer sees old owner lock removed"), F.Owner->HasActiveFishingUse());
		TestEqual(TEXT("Release observer sees unused bait returned"), Quantity(F.Fisher, FisherBaitDefinitionId), 4);
		TestEqual(TEXT("reentrant release cannot return a second bait"), F.Fisher->ReleaseFishingUse(FirstSession).Error,
			ECatDomainCommandError::AlreadyResolved);
		TestTrue(TEXT("same rod may start a new reservation during notification"), F.Begin(SecondSession).bReserved);
	});
	const auto Released = F.Fisher->ReleaseFishingUse(FirstSession);
	F.Fisher->OnSnapshotChanged.Remove(ReleaseObserver);
	TestTrue(TEXT("Release observer reentered"), bReentered);
	TestTrue(TEXT("first release has a frozen success result"), Released.bApplied);
	TestEqual(TEXT("release receipt excludes later reentrant Begin revision"), Released.EquipmentRevision, BeforeRelease + 1);
	TestFalse(TEXT("outer Release never reactivates old reservation"), F.Fisher->IsFishingUseActive(FirstSession));
	TestTrue(TEXT("outer Release never clears new reservation or its owner lock"), F.Fisher->IsFishingUseActive(SecondSession)
		&& F.Owner->HasActiveFishingUse());
	TestEqual(TEXT("second reservation owns exactly one bait"), Quantity(F.Fisher, FisherBaitDefinitionId), 3);
	TestTrue(TEXT("new reservation remains independently releasable"), F.Fisher->ReleaseFishingUse(SecondSession).bApplied);
	TestEqual(TEXT("all unused bait returns exactly once"), Quantity(F.Fisher, FisherBaitDefinitionId), 4);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodMissingOwnerTest,
	"Catfishing.Unit.Equipment.BorrowedRod.MissingOwnerStillReturnsCasterBaitAndClosesReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodMissingOwnerTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_session_owner_unavailable"), EAutomationExpectedErrorFlags::Contains, 1);
	FFixture F;
	if (!F.Initialize(*this)) return false;
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("reserves borrowed rod before owner disappears"), F.Begin(SessionId).bReserved)) return false;
	F.Owner->DestroyComponent();
	TestFalse(TEXT("original inventory host is destroyed"), IsValid(F.Owner));
	TestNull(TEXT("frozen owner reference no longer resolves"), F.Fisher->GetFishingRodEquipment(SessionId));
	double Durability = 0.0;
	bool bBroken = false;
	TestFalse(TEXT("missing owner's rod is not synthesized"), F.Fisher->GetFishingRodDurability(SessionId, Durability, bBroken));
	TestTrue(TEXT("owner loss does not block caller cleanup"), F.Fisher->ReleaseFishingUse(SessionId).bApplied);
	TestFalse(TEXT("caller reservation closes after owner loss"), F.Fisher->IsFishingUseActive(SessionId));
	TestEqual(TEXT("missing owner still returns caller's unused bait"), Quantity(F.Fisher, FisherBaitDefinitionId), 4);
	TestEqual(TEXT("caller personal rod is unchanged"), F.Fisher->GetSnapshot().RodDurability, 220.0);
	const FCatEquipmentLoadoutSnapshot Released = F.Fisher->GetSnapshot();
	TestEqual(TEXT("owner-loss cleanup can be safely retried"), F.Fisher->ReleaseFishingUse(SessionId).Error,
		ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("owner-loss retry leaves inventory unchanged"), SameSnapshot(Released, F.Fisher->GetSnapshot()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodUnstartedCoordinatorDestroyedTest,
	"Catfishing.Unit.Equipment.BorrowedRod.CoordinatorDestroyedBeforeBeginPlayReleasesOwnerLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodUnstartedCoordinatorDestroyedTest::RunTest(const FString& Parameters)
{
	using namespace CatBorrowedRodTests;
	FFixture F;
	if (!F.Initialize(*this)) return false;
	if (!TestFalse(TEXT("fixture world has not begun play"), F.Wrapper.GetTestWorld()->HasBegunPlay())
		|| !TestFalse(TEXT("coordinator component has not begun play"), F.Fisher->HasBegunPlay())
		|| !TestFalse(TEXT("rod owner's component has not begun play"), F.Owner->HasBegunPlay())) return false;
	const FGuid BorrowedSessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("native reservation succeeds before actor BeginPlay"), F.Begin(BorrowedSessionId).bReserved)) return false;
	TestTrue(TEXT("successful pre-BeginPlay reservation locks original owner rod"), F.Owner->HasActiveFishingUse());
	const int64 LockedOwnerRevision = F.Owner->GetSnapshot().Revision;
	int32 DestroyNotifications = 0;
	bool bReenteredDestroy = false;
	const FDelegateHandle DestroyObserver = F.Fisher->OnSnapshotChanged.AddLambda([&]()
	{
		++DestroyNotifications;
		TestFalse(TEXT("destruction notification sees caller reservation already closed"), F.Fisher->IsFishingUseActive(BorrowedSessionId));
		TestFalse(TEXT("destruction notification sees owner lock already released"), F.Owner->HasActiveFishingUse());
		TestEqual(TEXT("destruction notification sees exactly one bait restored"), Quantity(F.Fisher, FisherBaitDefinitionId), 4);
		if (!bReenteredDestroy)
		{
			bReenteredDestroy = true;
			const FCatEquipmentLoadoutSnapshot OwnerBeforeReentry = F.Owner->GetSnapshot();
			F.Fisher->DestroyComponent();
			TestTrue(TEXT("destruction notification can reenter without changing owner again"),
				SameSnapshot(OwnerBeforeReentry, F.Owner->GetSnapshot()));
		}
	});
	F.Fisher->DestroyComponent();
	F.Fisher->OnSnapshotChanged.Remove(DestroyObserver);
	TestEqual(TEXT("destruction publishes one completed caller cleanup"), DestroyNotifications, 1);
	TestTrue(TEXT("destruction observer exercised reentrant component destruction"), bReenteredDestroy);
	TestFalse(TEXT("coordinator is destroyed without an EndPlay phase"), IsValid(F.Fisher));
	TestFalse(TEXT("component destruction releases original owner rod lock"), F.Owner->HasActiveFishingUse());
	TestEqual(TEXT("owner publishes exactly one lock release revision"), F.Owner->GetSnapshot().Revision, LockedOwnerRevision + 1);
	const FCatEquipmentLoadoutSnapshot ReleasedOwner = F.Owner->GetSnapshot();
	F.Fisher->DestroyComponent();
	TestTrue(TEXT("repeating DestroyComponent cannot release again"), SameSnapshot(ReleasedOwner, F.Owner->GetSnapshot()));

	const FGuid NewSessionId = FGuid::NewGuid();
	const auto Reused = F.Owner->BeginFishingUse(NewSessionId, F.OwnerRodId, ReleasedOwner.BaitItemInstanceId,
		ReleasedOwner.FloatItemInstanceId, OwnerRodDefinitionId, ReleasedOwner.BaitDefinitionId,
		ReleasedOwner.FloatDefinitionId, ReleasedOwner.Revision);
	if (!TestTrue(TEXT("original owner can start a new session on the same physical rod"), Reused.bReserved)) return false;
	TestEqual(TEXT("destroyed borrower does not consume owner's bait"), Quantity(F.Owner, OwnerBaitDefinitionId), 3);
	if (!TestTrue(TEXT("new session can independently release its reservation"), F.Owner->ReleaseFishingUse(NewSessionId).bApplied)) return false;
	TestEqual(TEXT("new cancellation restores original owner's bait"), Quantity(F.Owner, OwnerBaitDefinitionId), 4);
	const auto Packed = F.Owner->UnUse(FGuid::NewGuid(), F.OwnerRodId);
	TestTrue(TEXT("original owner can pack the rod after borrower destruction"), Packed.bCommitted);
	TestEqual(TEXT("destroyed coordinator never duplicates or replaces the rod"), Packed.Item.ItemInstanceId, F.OwnerRodId);
	TestEqual(TEXT("destroyed coordinator never damages the rod"), Packed.Item.RodDurability, 100.0);
	return !HasAnyErrors();
}

#endif
