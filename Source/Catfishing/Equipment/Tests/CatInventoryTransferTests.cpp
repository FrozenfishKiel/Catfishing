#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/Inventory/CatInventoryTransferService.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "UObject/StrongObjectPtr.h"

namespace CatInventoryTransferTests
{
	const FName RodId(TEXT("TransferTestRod"));
	const FName FloatId(TEXT("TransferTestFloat"));
	const FName ScoopId(TEXT("TransferTestScoop"));
	const FName AlternateFloatId(TEXT("TransferTestAlternateFloat"));
	const FName AlternateScoopId(TEXT("TransferTestAlternateScoop"));
	const FName BaitId(TEXT("TransferTestBait"));

	bool SameEquipment(const FCatEquipmentLoadoutSnapshot& A, const FCatEquipmentLoadoutSnapshot& B)
	{
		return FCatEquipmentLoadoutSnapshot::StaticStruct()->CompareScriptStruct(&A, &B, 0);
	}

	bool SameCamp(const FCatCampInventorySnapshot& A, const FCatCampInventorySnapshot& B)
	{
		return FCatCampInventorySnapshot::StaticStruct()->CompareScriptStruct(&A, &B, 0);
	}

	int32 FindItem(const UCatEquipmentComponent* Equipment, const FGuid ItemId)
	{
		return Equipment->GetSnapshot().InventorySlots.IndexOfByPredicate(
			[ItemId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == ItemId && Slot.Quantity > 0; });
	}

	int32 FindDefinition(const UCatEquipmentComponent* Equipment, const FName DefinitionId)
	{
		return Equipment->GetSnapshot().InventorySlots.IndexOfByPredicate(
			[DefinitionId](const FCatRunInventorySlot& Slot) { return Slot.DefinitionId == DefinitionId && Slot.Quantity > 0; });
	}

	int32 Quantity(const UCatEquipmentComponent* Equipment, const FName DefinitionId)
	{
		int32 Total = 0;
		for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
		{
			if (Slot.DefinitionId == DefinitionId) Total += Slot.Quantity;
		}
		return Total;
	}

	// 真实端点和事务入口，瞬态目录只固定容量与初始物品数值，不伪造库存或活动记录。
	struct FFixture
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions = Settings->Definitions;
		int32 SavedCapacity = Settings->InventorySlotCapacity;
		int32 SavedStack = Settings->InventoryQuantityStackCapacity;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
		FTestWorldWrapper WorldWrapper;
		UCatEquipmentComponent* A = nullptr;
		UCatEquipmentComponent* B = nullptr;
		ACatCampInventoryActor* Camp = nullptr;
		UCatInventoryTransferService* Service = nullptr;

		~FFixture()
		{
			Settings->Definitions = SavedDefinitions;
			Settings->InventorySlotCapacity = SavedCapacity;
			Settings->InventoryQuantityStackCapacity = SavedStack;
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
			Settings->InventorySlotCapacity = 4;
			Settings->InventoryQuantityStackCapacity = 10;
			UCatEquipmentDefinition* Rod = AddDefinition(RodId, ECatEquipmentKind::Rod);
			Rod->MaximumRodDurability = 100.0;
			Rod->MaximumLineLengthCentimeters = 1500.0;
			Rod->HighTensionWearMultiplier = 1.0;
			Rod->UseActorClass = ACatFishingRodActor::StaticClass();
			Rod->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
			AddDefinition(FloatId, ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1000.0;
			AddDefinition(AlternateFloatId, ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1200.0;
			AddDefinition(ScoopId, ECatEquipmentKind::ScoopNet)->ScoopReachCentimeters = 150.0;
			AddDefinition(AlternateScoopId, ECatEquipmentKind::ScoopNet)->ScoopReachCentimeters = 180.0;
			UCatEquipmentDefinition* Bait = AddDefinition(BaitId, ECatEquipmentKind::Bait);
			Bait->bRunConsumable = true;
			Bait->BiteRateMultiplier = 1.0;
			Bait->MinimumBiteDelayMultiplier = 1.0;
			Bait->MaxStackSize = 10;
			for (const auto& Definition : Definitions)
			{
				if (!Test.TestTrue(TEXT("transfer test definition is runtime ready"), Definition->IsRuntimeDefinitionReady())) return false;
			}
			if (!Test.TestTrue(TEXT("creates transfer authority world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			UWorld* World = WorldWrapper.GetTestWorld();
			ACatCharacter* CharacterA = World->SpawnActor<ACatCharacter>();
			ACatCharacter* CharacterB = World->SpawnActor<ACatCharacter>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
			Camp = World->SpawnActor<ACatCampInventoryActor>();
			Service = World->GetSubsystem<UCatInventoryTransferService>();
			A = CharacterA ? CharacterA->GetEquipmentComponent() : nullptr;
			B = CharacterB ? CharacterB->GetEquipmentComponent() : nullptr;
			return Test.TestTrue(TEXT("creates two real Equipment endpoints, Camp and transfer service"), A && B && Camp && Service);
		}

		bool Grant(FAutomationTestBase& Test, UCatEquipmentComponent* Equipment, const FName Id, const int32 Count = 1)
		{
			const FCatDomainCommandResult Result = Id == BaitId
				? Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id, Count)
				: Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id);
			return Test.TestTrue(TEXT("grants real inventory contents"), Result.bCommitted);
		}

		FCatInventoryTransferRequest Request(UCatEquipmentComponent* Source, UCatEquipmentComponent* Target,
			const int32 SourceSlot, const int32 TargetSlot = INDEX_NONE, const int32 Count = 1) const
		{
			FCatInventoryTransferRequest Result;
			Result.RequestId = FGuid::NewGuid();
			Result.Initiator = Source->GetOwner();
			Result.Source.Host = Source;
			Result.Target.Host = Target;
			Result.ExpectedSourceRevision = Source->GetSnapshot().Revision;
			Result.ExpectedTargetRevision = Target->GetSnapshot().Revision;
			Result.SourceSlotIndex = SourceSlot;
			Result.TargetSlotIndex = TargetSlot;
			Result.Quantity = Count;
			if (Source->GetSnapshot().InventorySlots.IsValidIndex(SourceSlot))
			{
				Result.ExpectedSourceItemId = Source->GetSnapshot().InventorySlots[SourceSlot].ItemInstanceId;
			}
			return Result;
		}

		FCatInventoryTransferRequest ActiveRequest(UCatEquipmentComponent* Source, UCatEquipmentComponent* Target,
			const FGuid ItemId) const
		{
			FCatInventoryTransferRequest Result = Request(Source, Target, 0);
			Result.Source.Channel = TEXT("ActiveUse");
			Result.Source.EntryId = ItemId;
			Result.ExpectedSourceItemId = ItemId;
			return Result;
		}

		FCatFishingUseReservationResult Begin(UCatEquipmentComponent* Equipment, const FGuid SessionId, const FGuid ItemId) const
		{
			const FCatEquipmentLoadoutSnapshot Current = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, ItemId, Current.BaitItemInstanceId, Current.FloatItemInstanceId,
				RodId, Current.BaitDefinitionId, Current.FloatDefinitionId, Current.Revision);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferStoredSafetyTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.StoredIdentityRevisionCapacityAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferStoredSafetyTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	AddExpectedErrorPlain(TEXT("Event=inventory_transfer_rejected"), EAutomationExpectedErrorFlags::Contains, 4);
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, RodId)) return false;
	for (int32 Index = 0; Index < 4; ++Index) if (!F.Grant(*this, F.B, FloatId)) return false;
	const FCatRunInventorySlot Rod = F.A->GetSnapshot().InventorySlots[0];
	const FCatEquipmentLoadoutSnapshot BeforeA = F.A->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot BeforeB = F.B->GetSnapshot();
	for (const bool bStaleSource : {true, false})
	{
		FCatInventoryTransferRequest Request = F.Request(F.A, F.B, 0);
		if (bStaleSource) ++Request.ExpectedSourceRevision;
		else ++Request.ExpectedTargetRevision;
		const FCatInventoryTransferResult Rejected = F.Service->TransferFromAuthority(Request);
		TestFalse(TEXT("either stale endpoint revision rejects transfer"), Rejected.bCommitted);
		TestEqual(TEXT("revision rejection is explicit"), Rejected.Error, ECatDomainCommandError::RevisionConflict);
	}
	const FCatInventoryTransferResult Full = F.Service->TransferFromAuthority(F.Request(F.A, F.B, 0));
	TestFalse(TEXT("full target cannot receive physical rod"), Full.bCommitted);
	TestEqual(TEXT("full target reports capacity"), Full.Error, ECatDomainCommandError::CapacityExceeded);
	TestTrue(TEXT("all rejected transfers leave source snapshot unchanged"), SameEquipment(BeforeA, F.A->GetSnapshot()));
	TestTrue(TEXT("all rejected transfers leave target snapshot unchanged"), SameEquipment(BeforeB, F.B->GetSnapshot()));

	FCatInventoryTransferRequest FreeSlot = F.Request(F.B, F.A, 0);
	FreeSlot.Target.Host = F.Camp;
	FreeSlot.ExpectedTargetRevision = F.Camp->GetSnapshot().Revision;
	if (!TestTrue(TEXT("moves a target item to Camp to free one slot"), F.Service->TransferFromAuthority(FreeSlot).bCommitted)) return false;
	const FCatInventoryTransferRequest Request = F.Request(F.A, F.B, 0);
	const FCatInventoryTransferResult Moved = F.Service->TransferFromAuthority(Request);
	if (!TestTrue(TEXT("moves physical rod between players"), Moved.bCommitted)) return false;
	TestEqual(TEXT("full transfer preserves instance ID"), Moved.Item.ItemInstanceId, Rod.ItemInstanceId);
	const int32 ReceivedIndex = FindItem(F.B, Rod.ItemInstanceId);
	if (!TestTrue(TEXT("target stores exact original rod"), ReceivedIndex != INDEX_NONE)) return false;
	TestEqual(TEXT("target preserves rod durability"), F.B->GetSnapshot().InventorySlots[ReceivedIndex].RodDurability, Rod.RodDurability);
	TestEqual(TEXT("source no longer stores rod"), FindItem(F.A, Rod.ItemInstanceId), INDEX_NONE);
	TestEqual(TEXT("source revision advances once"), Moved.SourceRevision, Request.ExpectedSourceRevision + 1);
	TestEqual(TEXT("target revision advances once"), Moved.TargetRevision, Request.ExpectedTargetRevision + 1);
	const FCatInventoryTransferResult Replayed = F.Service->TransferFromAuthority(Request);
	TestTrue(TEXT("successful request replays even after source is empty"), Replayed.bReplayed);
	TestFalse(TEXT("replay does not execute again"), Replayed.bCommitted);
	TestEqual(TEXT("replay reports already resolved"), Replayed.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("replay preserves original resulting source revision"), Replayed.SourceRevision, Moved.SourceRevision);
	TestEqual(TEXT("replay preserves original resulting target revision"), Replayed.TargetRevision, Moved.TargetRevision);
	FCatInventoryTransferRequest ChangedTarget = Request;
	ChangedTarget.Target.Host = F.Camp;
	TestEqual(TEXT("same GUID cannot be redirected to another target"), F.Service->TransferFromAuthority(ChangedTarget).Error,
		ECatDomainCommandError::InvalidPayload);
	FCatInventoryTransferRequest OtherInitiator = F.Request(F.B, F.A, ReceivedIndex);
	OtherInitiator.RequestId = Request.RequestId;
	TestTrue(TEXT("another authority initiator may reuse the same request GUID"), F.Service->TransferFromAuthority(OtherInitiator).bCommitted);
	TestTrue(TEXT("second initiator returns only one original instance"), FindItem(F.A, Rod.ItemInstanceId) != INDEX_NONE
		&& FindItem(F.B, Rod.ItemInstanceId) == INDEX_NONE);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferDragAndQuantityTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.DragMergeSwapCapacityAndQuantityIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferDragAndQuantityTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	AddExpectedErrorPlain(TEXT("Event=inventory_transfer_rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, BaitId, 6) || !F.Grant(*this, F.B, BaitId, 8)) return false;
	const FGuid SourceId = F.A->GetSnapshot().InventorySlots[0].ItemInstanceId;
	const FGuid TargetId = F.B->GetSnapshot().InventorySlots[0].ItemInstanceId;
	FCatInventoryTransferRequest Drag = F.Request(F.A, F.B, 0, 0);
	Drag.Mode = ECatInventoryTransferMode::DragToSlot;
	const FCatInventoryTransferResult Merged = F.Service->TransferFromAuthority(Drag);
	if (!TestTrue(TEXT("drag partially merges into matching stack"), Merged.bCommitted)) return false;
	TestEqual(TEXT("partial merge moves only available capacity"), Merged.MovedQuantity, 2);
	TestEqual(TEXT("source remainder keeps instance ID"), F.A->GetSnapshot().InventorySlots[0].ItemInstanceId, SourceId);
	TestEqual(TEXT("source remainder is four"), Quantity(F.A, BaitId), 4);
	TestEqual(TEXT("target stack keeps target ID"), F.B->GetSnapshot().InventorySlots[0].ItemInstanceId, TargetId);
	TestEqual(TEXT("target reaches stack limit"), Quantity(F.B, BaitId), 10);
	const FCatEquipmentLoadoutSnapshot FullA = F.A->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot FullB = F.B->GetSnapshot();
	Drag = F.Request(F.A, F.B, 0, 0);
	Drag.Mode = ECatInventoryTransferMode::DragToSlot;
	TestFalse(TEXT("dragging onto full stack has no commit"), F.Service->TransferFromAuthority(Drag).bCommitted);
	TestTrue(TEXT("full merge leaves both snapshots unchanged"), SameEquipment(FullA, F.A->GetSnapshot()) && SameEquipment(FullB, F.B->GetSnapshot()));
	if (!TestTrue(TEXT("grants Camp rod for cross-definition swap"), F.Camp->AddItemFromAuthority(
		FGuid::NewGuid(), F.Camp->GetSnapshot().Revision, RodId, 1).bCommitted)) return false;
	const FGuid CampRodId = F.Camp->GetSnapshot().InventorySlots[0].ItemInstanceId;
	FCatInventoryTransferRequest Swap = F.Request(F.A, F.B, 0, 0);
	Swap.Target.Host = F.Camp;
	Swap.ExpectedTargetRevision = F.Camp->GetSnapshot().Revision;
	Swap.Mode = ECatInventoryTransferMode::DragToSlot;
	const FCatEquipmentLoadoutSnapshot BeforeSwapA = F.A->GetSnapshot();
	const FCatCampInventorySnapshot BeforeSwapCamp = F.Camp->GetSnapshot();
	{
		// 模拟物品堆叠上限调低后的既有大栈；交换不能把超过接收上限的整栈送入目标。
		TGuardValue<int32> ReducedStackLimit(F.Settings->FindRuntimeDefinition(BaitId)->MaxStackSize, 3);
		const FCatInventoryTransferResult TooLarge = F.Service->TransferFromAuthority(Swap);
		TestFalse(TEXT("swap refuses whole source stack above receiving limit"), TooLarge.bCommitted);
		TestTrue(TEXT("oversized existing stack is rejected before swap"), TooLarge.Error == ECatDomainCommandError::CapacityExceeded
			|| TooLarge.Error == ECatDomainCommandError::InvalidPayload);
		TestTrue(TEXT("swap capacity failure leaves both hosts unchanged"), SameEquipment(BeforeSwapA, F.A->GetSnapshot())
			&& SameCamp(BeforeSwapCamp, F.Camp->GetSnapshot()));
	}
	Swap.RequestId = FGuid::NewGuid();
	if (!TestTrue(TEXT("drag exchanges unlike whole instances"), F.Service->TransferFromAuthority(Swap).bCommitted)) return false;
	TestEqual(TEXT("swap receives original Camp rod"), F.A->GetSnapshot().InventorySlots[0].ItemInstanceId, CampRodId);
	TestEqual(TEXT("swap leaves original bait instance in Camp"), F.Camp->GetSnapshot().InventorySlots[0].ItemInstanceId, SourceId);
	FCatInventoryTransferRequest Empty = F.Request(F.A, F.B, 0, 1);
	Empty.Mode = ECatInventoryTransferMode::DragToSlot;
	if (!TestTrue(TEXT("drag moves an instance into an empty slot"), F.Service->TransferFromAuthority(Empty).bCommitted)) return false;
	TestEqual(TEXT("empty slot drag preserves identity"), F.B->GetSnapshot().InventorySlots[1].ItemInstanceId, CampRodId);
	const FCatEquipmentLoadoutSnapshot BeforeBoundsA = F.A->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot BeforeBoundsB = F.B->GetSnapshot();
	FCatInventoryTransferRequest BeyondCapacity = F.Request(F.B, F.A, 1, F.Settings->InventorySlotCapacity);
	BeyondCapacity.Mode = ECatInventoryTransferMode::DragToSlot;
	TestFalse(TEXT("explicit target slot at capacity boundary is rejected"), F.Service->TransferFromAuthority(BeyondCapacity).bCommitted);
	TestTrue(TEXT("out of bounds target never writes either endpoint"), SameEquipment(BeforeBoundsA, F.A->GetSnapshot())
		&& SameEquipment(BeforeBoundsB, F.B->GetSnapshot()));

	FCatInventoryTransferRequest Partial = F.Request(F.B, F.A, 0, INDEX_NONE, 2);
	Partial.Source.Host = F.Camp;
	Partial.SourceSlotIndex = 0;
	Partial.ExpectedSourceItemId = SourceId;
	Partial.ExpectedSourceRevision = F.Camp->GetSnapshot().Revision;
	const FCatInventoryTransferResult Split = F.Service->TransferFromAuthority(Partial);
	if (!TestTrue(TEXT("quantity request splits part of a stack"), Split.bCommitted)) return false;
	TestTrue(TEXT("partial transfer creates distinct moved identity"), Split.Item.ItemInstanceId.IsValid() && Split.Item.ItemInstanceId != SourceId);
	TestEqual(TEXT("partial transfer leaves original source ID"), F.Camp->GetSnapshot().InventorySlots[0].ItemInstanceId, SourceId);
	TestEqual(TEXT("partial transfer leaves exact quantity"), F.Camp->GetSnapshot().InventorySlots[0].Quantity, 2);
	FCatInventoryTransferRequest Remaining = Partial;
	Remaining.RequestId = FGuid::NewGuid();
	Remaining.ExpectedSourceRevision = F.Camp->GetSnapshot().Revision;
	Remaining.ExpectedTargetRevision = F.A->GetSnapshot().Revision;
	const FCatInventoryTransferResult Full = F.Service->TransferFromAuthority(Remaining);
	if (!TestTrue(TEXT("remaining complete stack transfers"), Full.bCommitted)) return false;
	TestEqual(TEXT("full quantity transfer keeps original ID"), Full.Item.ItemInstanceId, SourceId);
	TestTrue(TEXT("automatic receipt retains separate same-definition identities"), FindItem(F.A, SourceId) != INDEX_NONE
		&& FindItem(F.A, Split.Item.ItemInstanceId) != INDEX_NONE);
	TestEqual(TEXT("split then full transfer conserves total quantity"), Quantity(F.A, BaitId), 4);
	FCatInventoryTransferRequest Internal = F.Request(F.A, F.A, FindItem(F.A, SourceId), 2);
	Internal.Mode = ECatInventoryTransferMode::DragToSlot;
	const int64 BeforeInternal = F.A->GetSnapshot().Revision;
	TestTrue(TEXT("same-host drag moves between stored slots"), F.Service->TransferFromAuthority(Internal).bCommitted);
	TestEqual(TEXT("same-host transaction increments revision only once"), F.A->GetSnapshot().Revision, BeforeInternal + 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferObserverTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.ObserversSeeBothCommittedAndReentryReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferObserverTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, RodId)) return false;
	const FGuid ItemId = F.A->GetSnapshot().InventorySlots[0].ItemInstanceId;
	const FCatInventoryTransferRequest Request = F.Request(F.A, F.B, 0);
	int32 SourceNotifications = 0;
	int32 TargetNotifications = 0;
	bool bEveryObserverSawBothCommitted = true;
	bool bEveryNestedRequestReplayed = true;
	const auto Observe = [&]()
	{
		bEveryObserverSawBothCommitted &= FindItem(F.A, ItemId) == INDEX_NONE && FindItem(F.B, ItemId) != INDEX_NONE
			&& F.A->GetSnapshot().Revision == Request.ExpectedSourceRevision + 1
			&& F.B->GetSnapshot().Revision == Request.ExpectedTargetRevision + 1;
		const FCatInventoryTransferResult Nested = F.Service->TransferFromAuthority(Request);
		bEveryNestedRequestReplayed &= Nested.bReplayed && !Nested.bCommitted && Nested.Error == ECatDomainCommandError::AlreadyResolved;
	};
	const FDelegateHandle SourceObserver = F.A->OnSnapshotChanged.AddLambda([&]() { ++SourceNotifications; Observe(); });
	const FDelegateHandle TargetObserver = F.B->OnSnapshotChanged.AddLambda([&]() { ++TargetNotifications; Observe(); });
	const FCatInventoryTransferResult Result = F.Service->TransferFromAuthority(Request);
	F.A->OnSnapshotChanged.Remove(SourceObserver);
	F.B->OnSnapshotChanged.Remove(TargetObserver);
	TestTrue(TEXT("observed transfer commits"), Result.bCommitted);
	TestEqual(TEXT("source publishes once"), SourceNotifications, 1);
	TestEqual(TEXT("target publishes once"), TargetNotifications, 1);
	TestTrue(TEXT("every observer sees both committed endpoint revisions and contents"), bEveryObserverSawBothCommitted);
	TestTrue(TEXT("terminal cache is ready before either observer reenters"), bEveryNestedRequestReplayed);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferCampCompatibilityTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.CampWrappersPreserveIdentityAndReplayAfterEmptySource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferCampCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, RodId)) return false;
	const FGuid ItemId = F.A->GetSnapshot().InventorySlots[0].ItemInstanceId;
	const FGuid DepositId = FGuid::NewGuid();
	const int64 DepositCampRevision = F.Camp->GetSnapshot().Revision;
	const int64 DepositEquipmentRevision = F.A->GetSnapshot().Revision;
	if (!TestTrue(TEXT("legacy deposit wrapper delegates a real transfer"), F.Camp->DepositFromEquipmentSlotFromAuthority(
		DepositId, DepositCampRevision, 0, F.A, DepositEquipmentRevision, 0).bCommitted)) return false;
	TestEqual(TEXT("Camp receives same physical rod"), F.Camp->GetSnapshot().InventorySlots[0].ItemInstanceId, ItemId);
	TestEqual(TEXT("deposit replays after source slot empties"), F.Camp->DepositFromEquipmentSlotFromAuthority(
		DepositId, DepositCampRevision, 0, F.A, DepositEquipmentRevision, 0).Error, ECatDomainCommandError::AlreadyResolved);
	const FGuid DragId = FGuid::NewGuid();
	const int64 DragCampRevision = F.Camp->GetSnapshot().Revision;
	const int64 DragEquipmentRevision = F.B->GetSnapshot().Revision;
	if (!TestTrue(TEXT("legacy drag withdrawal transfers exact rod"), F.Camp->WithdrawToEquipmentSlotFromAuthority(
		DragId, DragCampRevision, 0, F.B, DragEquipmentRevision, 0).bCommitted)) return false;
	TestEqual(TEXT("drag wrapper preserves rod ID"), F.B->GetSnapshot().InventorySlots[0].ItemInstanceId, ItemId);
	TestEqual(TEXT("drag withdrawal replays after Camp source empties"), F.Camp->WithdrawToEquipmentSlotFromAuthority(
		DragId, DragCampRevision, 0, F.B, DragEquipmentRevision, 0).Error, ECatDomainCommandError::AlreadyResolved);
	if (!TestTrue(TEXT("seeds Camp quantity stack"), F.Camp->AddItemFromAuthority(
		FGuid::NewGuid(), F.Camp->GetSnapshot().Revision, BaitId, 6).bCommitted)) return false;
	const FGuid BaitInstanceId = F.Camp->GetSnapshot().InventorySlots[0].ItemInstanceId;
	const FGuid QuantityId = FGuid::NewGuid();
	const int64 QuantityCampRevision = F.Camp->GetSnapshot().Revision;
	const int64 QuantityEquipmentRevision = F.A->GetSnapshot().Revision;
	if (!TestTrue(TEXT("legacy quantity withdrawal splits requested quantity"), F.Camp->WithdrawToEquipmentFromAuthority(
		QuantityId, QuantityCampRevision, 0, 2, F.A, QuantityEquipmentRevision).bCommitted)) return false;
	TestEqual(TEXT("partial wrapper receipt has exact quantity"), Quantity(F.A, BaitId), 2);
	TestTrue(TEXT("partial wrapper receipt gets a new identity"), F.A->GetSnapshot().InventorySlots[FindDefinition(F.A, BaitId)].ItemInstanceId != BaitInstanceId);
	TestEqual(TEXT("quantity wrapper retries do not take more bait"), F.Camp->WithdrawToEquipmentFromAuthority(
		QuantityId, QuantityCampRevision, 0, 2, F.A, QuantityEquipmentRevision).Error, ECatDomainCommandError::AlreadyResolved);
	const FGuid FullId = FGuid::NewGuid();
	const int64 FullCampRevision = F.Camp->GetSnapshot().Revision;
	const int64 FullEquipmentRevision = F.B->GetSnapshot().Revision;
	if (!TestTrue(TEXT("legacy quantity withdrawal takes full remaining stack"), F.Camp->WithdrawToEquipmentFromAuthority(
		FullId, FullCampRevision, 0, 4, F.B, FullEquipmentRevision).bCommitted)) return false;
	TestTrue(TEXT("full wrapper receipt preserves original stack identity"), FindItem(F.B, BaitInstanceId) != INDEX_NONE);
	TestEqual(TEXT("full quantity wrapper replays after source becomes empty"), F.Camp->WithdrawToEquipmentFromAuthority(
		FullId, FullCampRevision, 0, 4, F.B, FullEquipmentRevision).Error, ECatDomainCommandError::AlreadyResolved);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferActiveUseTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.ActiveUseLocksCrossOwnerHandoffAndBrokenUnUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferActiveUseTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	AddExpectedErrorPlain(TEXT("Event=inventory_transfer_rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, RodId) || !F.Grant(*this, F.A, FloatId)
		|| !F.Grant(*this, F.A, BaitId, 4)) return false;
	const FGuid ItemId = F.A->GetSnapshot().RodItemInstanceId;
	if (!TestTrue(TEXT("Use moves a real rod into active record"), F.A->Use(
		FGuid::NewGuid(), F.A->GetSnapshot().Revision, ItemId).bCommitted)) return false;
	const FGuid FirstSession = FGuid::NewGuid();
	if (!TestTrue(TEXT("active rod binds fishing reservation"), F.Begin(F.A, FirstSession, ItemId).bReserved)) return false;
	const FCatEquipmentLoadoutSnapshot LockedA = F.A->GetSnapshot();
	const FCatEquipmentLoadoutSnapshot LockedB = F.B->GetSnapshot();
	const FCatInventoryTransferResult Locked = F.Service->TransferFromAuthority(F.ActiveRequest(F.A, F.B, ItemId));
	TestFalse(TEXT("active fishing reservation blocks handoff"), Locked.bCommitted);
	TestEqual(TEXT("locked source reports invalid phase"), Locked.Error, ECatDomainCommandError::InvalidPhase);
	const FCatInventoryItemUseResult LockedUnUse = F.A->UnUse(FGuid::NewGuid(), ItemId);
	TestFalse(TEXT("legacy UnUse cannot bypass an active fishing reservation"), LockedUnUse.bCommitted);
	TestTrue(TEXT("locked transfers preserve both full snapshots"), SameEquipment(LockedA, F.A->GetSnapshot())
		&& SameEquipment(LockedB, F.B->GetSnapshot()));
	if (!TestTrue(TEXT("commits first session bait"), F.A->CommitFishingBaitDeferred(FirstSession).bApplied)
		|| !TestTrue(TEXT("wears only active physical instance"), F.A->ApplyFishingRodWear(FirstSession, 1, 17.0).bApplied)
		|| !TestTrue(TEXT("releases first fishing reservation"), F.A->ReleaseFishingUse(FirstSession).bApplied)) return false;
	const FCatInventoryTransferResult Handoff = F.Service->TransferFromAuthority(F.ActiveRequest(F.A, F.B, ItemId));
	if (!TestTrue(TEXT("server-native active endpoint can hand off to another Equipment"), Handoff.bCommitted)) return false;
	TestEqual(TEXT("active handoff preserves physical identity"), Handoff.Item.ItemInstanceId, ItemId);
	const int32 ReceivedIndex = FindItem(F.B, ItemId);
	if (!TestTrue(TEXT("recipient holds physical rod exactly once"), ReceivedIndex != INDEX_NONE)) return false;
	TestEqual(TEXT("handoff preserves accumulated wear"), F.B->GetSnapshot().InventorySlots[ReceivedIndex].RodDurability, 83.0);
	TestFalse(TEXT("old owner cannot UnUse handed off record again"), F.A->UnUse(FGuid::NewGuid(), ItemId).bCommitted);
	TestEqual(TEXT("old owner has no duplicate inventory instance"), FindItem(F.A, ItemId), INDEX_NONE);

	if (!TestTrue(TEXT("recipient can Use transferred instance"), F.B->Use(
		FGuid::NewGuid(), F.B->GetSnapshot().Revision, ItemId).bCommitted)
		|| !F.Grant(*this, F.B, FloatId) || !F.Grant(*this, F.B, BaitId, 2)) return false;
	const FGuid SecondSession = FGuid::NewGuid();
	if (!TestTrue(TEXT("recipient binds transferred physical instance"), F.Begin(F.B, SecondSession, ItemId).bReserved)
		|| !TestTrue(TEXT("recipient commits own bait"), F.B->CommitFishingBaitDeferred(SecondSession).bApplied)
		|| !TestTrue(TEXT("recipient can break transferred rod"), F.B->ApplyFishingRodWear(SecondSession, 1, 84.0).bRodBroken)
		|| !TestTrue(TEXT("broken rod reservation releases"), F.B->ReleaseFishingUse(SecondSession).bApplied)) return false;
	const int64 BeforeUnUse = F.B->GetSnapshot().Revision;
	int32 Notifications = 0;
	const FDelegateHandle Observer = F.B->OnSnapshotChanged.AddLambda([&]() { ++Notifications; });
	const FGuid UnUseId = FGuid::NewGuid();
	const FCatInventoryItemUseResult Returned = F.B->UnUse(UnUseId, ItemId);
	F.B->OnSnapshotChanged.Remove(Observer);
	if (!TestTrue(TEXT("same-host UnUse returns broken physical rod"), Returned.bCommitted)) return false;
	TestEqual(TEXT("ActiveUse to Stored increments one host revision once"), F.B->GetSnapshot().Revision, BeforeUnUse + 1);
	TestEqual(TEXT("same-host UnUse publishes one complete snapshot"), Notifications, 1);
	const int32 ReturnedIndex = FindItem(F.B, ItemId);
	if (!TestTrue(TEXT("broken rod returns with original ID"), ReturnedIndex != INDEX_NONE)) return false;
	TestTrue(TEXT("UnUse preserves broken state"), F.B->GetSnapshot().InventorySlots[ReturnedIndex].bRodBroken);
	TestEqual(TEXT("UnUse never repairs returned rod"), F.B->GetSnapshot().InventorySlots[ReturnedIndex].RodDurability, 0.0);
	TestEqual(TEXT("legacy UnUse replay reports already resolved"), F.B->UnUse(UnUseId, ItemId).Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("UnUse replay never increments revision"), F.B->GetSnapshot().Revision, BeforeUnUse + 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferSelectionRepairTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.FloatAndScoopRepairMissingSelectionAndPreserveValidChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatInventoryTransferSelectionRepairTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	for (const FName DefinitionId : {FloatId, ScoopId})
	{
		FFixture F;
		if (!F.Initialize(*this) || !F.Grant(*this, F.A, DefinitionId) || !F.Grant(*this, F.B, DefinitionId)) return false;
		const auto SelectedItem = [DefinitionId](const UCatEquipmentComponent* Equipment)
		{
			return DefinitionId == FloatId ? Equipment->GetSnapshot().FloatItemInstanceId
				: Equipment->GetSnapshot().ScoopNetItemInstanceId;
		};
		const FGuid FirstId = F.A->GetSnapshot().InventorySlots[FindDefinition(F.A, DefinitionId)].ItemInstanceId;
		const FGuid SecondId = F.B->GetSnapshot().InventorySlots[FindDefinition(F.B, DefinitionId)].ItemInstanceId;
		TestEqual(TEXT("first tool is selected by concrete instance"), SelectedItem(F.A), FirstId);
		TestEqual(TEXT("other player selects its own distinct instance"), SelectedItem(F.B), SecondId);
		if (!TestTrue(TEXT("transfers selected first tool away"), F.Service->TransferFromAuthority(
			F.Request(F.A, F.B, FindItem(F.A, FirstId))).bCommitted)) return false;
		TestEqual(TEXT("receiving same definition does not replace a valid selection"), SelectedItem(F.B), SecondId);
		if (!TestTrue(TEXT("receives a different instance of the same tool definition"), F.Service->TransferFromAuthority(
			F.Request(F.B, F.A, FindItem(F.B, SecondId))).bCommitted)) return false;
		TestEqual(TEXT("missing selected instance is repaired to received same-definition instance"), SelectedItem(F.A), SecondId);
		if (!TestTrue(TEXT("receives original instance while replacement is still valid"), F.Service->TransferFromAuthority(
			F.Request(F.B, F.A, FindItem(F.B, FirstId))).bCommitted)) return false;
		TestEqual(TEXT("another same-definition receipt preserves valid replacement selection"), SelectedItem(F.A), SecondId);
		TestTrue(TEXT("both distinct received instances remain in real inventory"), FindItem(F.A, FirstId) != INDEX_NONE
			&& FindItem(F.A, SecondId) != INDEX_NONE);

		// 当前选中实例转出，但同种旧实例仍在背包；收到另一型号时应先恢复玩家原来选择的种类。
		if (!TestTrue(TEXT("transfers selected replacement while original same-kind spare remains"), F.Service->TransferFromAuthority(
			F.Request(F.A, F.B, FindItem(F.A, SecondId))).bCommitted)) return false;
		const FName AlternateId = DefinitionId == FloatId ? AlternateFloatId : AlternateScoopId;
		if (!F.Grant(*this, F.B, AlternateId)) return false;
		const FGuid IncomingId = F.B->GetSnapshot().InventorySlots[FindDefinition(F.B, AlternateId)].ItemInstanceId;
		if (!TestTrue(TEXT("receives an alternate tool definition"), F.Service->TransferFromAuthority(
			F.Request(F.B, F.A, FindItem(F.B, IncomingId))).bCommitted)) return false;
		TestEqual(TEXT("selection recovery prefers remaining original definition over newly received alternative"), SelectedItem(F.A), FirstId);
		TestTrue(TEXT("alternate received tool is still available without replacing the old choice"), FindItem(F.A, IncomingId) != INDEX_NONE);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFormalInventoryFishingTransferTest,
	"Catfishing.Unit.Equipment.InventoryTransfer.FormalObjectIdentityAndAtomicObservers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFormalInventoryFishingTransferTest::RunTest(const FString& Parameters)
{
	using namespace CatInventoryTransferTests;
	FFixture F;
	if (!F.Initialize(*this) || !F.Grant(*this, F.A, RodId)) return false;
	UCatInventoryComponent* Source = F.A->GetInventoryTransferInventory();
	UCatInventoryComponent* Target = F.B->GetInventoryTransferInventory();
	const int32 InitialIndex = FindDefinition(F.A, RodId);
	const FGuid ItemId = F.A->GetSnapshot().InventorySlots[InitialIndex].ItemInstanceId;
	UCatInventoryItemInstance* Original = Source->GetInventoryEntryAtSlot(
		Source->FindInventorySlotIndexFromInstanceId(ItemId))->Instance;
	const FCatInventoryTransferRequest Request = F.Request(F.A, F.B, InitialIndex);
	int32 Observations = 0;
	const auto Observe = [&]()
	{
		++Observations;
		TestEqual(TEXT("formal observer sees source already empty"), Source->FindInventorySlotIndexFromInstanceId(ItemId), INDEX_NONE);
		const int32 Index = Target->FindInventorySlotIndexFromInstanceId(ItemId);
		TestTrue(TEXT("formal observer sees destination already committed"), Index != INDEX_NONE);
		if (Index != INDEX_NONE) TestTrue(TEXT("formal transfer preserves exact UObject"), Target->GetInventoryEntryAtSlot(Index)->Instance == Original);
		TestEqual(TEXT("formal observer reentry replays frozen terminal"), F.Service->TransferFromAuthority(Request).Error, ECatDomainCommandError::AlreadyResolved);
	};
	const FDelegateHandle SourceObserver = Source->OnInventoryObservedChanged.AddLambda(Observe);
	const FDelegateHandle TargetObserver = Target->OnInventoryObservedChanged.AddLambda(Observe);
	const FCatInventoryTransferResult Moved = F.Service->TransferFromAuthority(Request);
	Source->OnInventoryObservedChanged.Remove(SourceObserver);
	Target->OnInventoryObservedChanged.Remove(TargetObserver);
	if (!TestTrue(TEXT("formal transfer commits"), Moved.bCommitted)) return false;
	TestEqual(TEXT("each formal endpoint publishes once"), Observations, 2);
	if (!F.Grant(*this, F.B, FloatId) || !F.Grant(*this, F.B, BaitId, 2)) return false;
	if (!TestTrue(TEXT("formal transferred rod deploys"), F.B->Use(FGuid::NewGuid(),
		F.B->GetSnapshot().Revision, ItemId, 1, Target->GetInventoryRevision()).bCommitted)) return false;
	const FCatInventoryEntry* Held = Target->FindHeldInventoryEntryFromAuthority(ItemId);
	if (!TestNotNull(TEXT("formal inventory holds deployed object"), Held)) return false;
	TestTrue(TEXT("deployment keeps exact UObject"), Held->Instance == Original);
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("transferred rod supports reservation"), F.Begin(F.B, SessionId, ItemId).bReserved)
		|| !TestTrue(TEXT("bait commits"), F.B->CommitFishingBaitDeferred(SessionId).bApplied)
		|| !TestTrue(TEXT("wear commits"), F.B->ApplyFishingRodWear(SessionId, 1, 17.0).bApplied)) return false;
	TestEqual(TEXT("wear is authoritative on original UObject"), CastChecked<UCatEquipmentInventoryItemInstance>(Original)->GetRodDurability(), 83.0);
	if (!TestTrue(TEXT("fishing releases"), F.B->ReleaseFishingUse(SessionId).bApplied)
		|| !TestTrue(TEXT("same instance returns"), F.B->UnUse(FGuid::NewGuid(), ItemId).bCommitted)) return false;
	TestNull(TEXT("returned object no longer has held owner"), Target->FindHeldInventoryEntryFromAuthority(ItemId));
	const int32 ReturnedIndex = Target->FindInventorySlotIndexFromInstanceId(ItemId);
	if (!TestTrue(TEXT("formal returned object exists"), ReturnedIndex != INDEX_NONE)) return false;
	TestTrue(TEXT("return preserves UObject and does not recreate by definition"), Target->GetInventoryEntryAtSlot(ReturnedIndex)->Instance == Original);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
