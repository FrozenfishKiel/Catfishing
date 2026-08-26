#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Pawn.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseBeginTest,
	"Catfishing.Unit.Equipment.FishingUse.BeginIsAtomicExclusiveAndReplaySafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseBaitTest,
	"Catfishing.Unit.Equipment.FishingUse.BaitCommitAndReleaseAreIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseWearTest,
	"Catfishing.Unit.Equipment.FishingUse.WearSequenceIsAbsoluteMonotonicAndCommittedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseBreakTest,
	"Catfishing.Unit.Equipment.FishingUse.RodBreakOverridesWearAndCommitsZeroOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseLegacyGateTest,
	"Catfishing.Unit.Equipment.FishingUse.ActiveReservationBlocksLegacyMutationsAndProtectsReservedBait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentFishingUseDeferredBaitTest,
	"Catfishing.Unit.Equipment.FishingUse.DeferredBaitCommitPublishesExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatEquipmentFishingUseTest
{
	static const FName RodId(TEXT("FishingUseRod"));
	static const FName SpecialBaitId(TEXT("FishingUseSpecialBait"));
	static const FName NormalBaitId(TEXT("FishingUseNormalBait"));
	static const FName FloatId(TEXT("FishingUseFloat"));
	static const FName DriftwoodId(TEXT("FishingUseDriftwood"));

	/** 定义构造流程：创建最小 transient 钓鱼装备定义，并让 Bait 满足正式局内数量栈 gate。 */
	static UCatEquipmentDefinition* MakeDefinition(const FName Id, const ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = Id;
		Definition->Kind = Kind;
		Definition->FunctionalRouteId = *FString::Printf(TEXT("%sRoute"), *Id.ToString());
		Definition->RequiredUnlockId = NAME_None;
		if (Kind == ECatEquipmentKind::Rod || Kind == ECatEquipmentKind::Bait || Kind == ECatEquipmentKind::Float)
		{
			Definition->LoadoutSlotId = *FString::Printf(TEXT("%sSlot"), *Id.ToString());
		}
		if (Kind == ECatEquipmentKind::Rod)
		{
			Definition->MaximumRodDurability = 100.0;
			Definition->FishingStrength = 1.0;
			Definition->MaximumLineLengthCentimeters = 2000.0;
			Definition->HighTensionWearMultiplier = 1.0;
		}
		else if (Kind == ECatEquipmentKind::Bait)
		{
			Definition->bRunConsumable = true;
			Definition->BiteRateMultiplier = 1.0;
			Definition->MinimumBiteDelayMultiplier = 1.0;
		}
		else if (Kind == ECatEquipmentKind::Float)
		{
			Definition->MaximumCastDistanceCentimeters = 1500.0;
			Definition->CastErrorStandardDeviationCentimeters = 10.0;
			Definition->MaximumCastErrorRadiusCentimeters = 30.0;
			Definition->BiteSignalStability = 1.0;
		}
		return Definition;
	}

	static int32 QuantityOf(const FCatEquipmentLoadoutSnapshot& Snapshot, const FName DefinitionId)
	{
		const FCatRunConsumableStack* Stack = Snapshot.Consumables.FindByPredicate([DefinitionId](const FCatRunConsumableStack& Candidate)
		{
			return Candidate.DefinitionId == DefinitionId;
		});
		return Stack ? Stack->Quantity : 0;
	}

	struct FFishingUseFixture
	{
		UCatEquipmentSettings* Settings = nullptr;
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;
		ECatDomainPolicy SavedTrustPolicy = ECatDomainPolicy::Unset;
		FName SavedDriftwoodDefinitionId = NAME_None;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> RuntimeDefinitions;
		FTestWorldWrapper WorldWrapper;
		APawn* Pawn = nullptr;
		ACatfishingPlayerState* PlayerState = nullptr;
		UCatEquipmentComponent* Component = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			SavedDefinitions = Settings->Definitions;
			SavedTrustPolicy = Settings->ProfileLoadoutTrustPolicy;
			SavedDriftwoodDefinitionId = Settings->DriftwoodDefinitionId;
			RuntimeDefinitions.Reserve(5);

			TStrongObjectPtr<UCatEquipmentDefinition>& Rod = RuntimeDefinitions.Emplace_GetRef(
				MakeDefinition(RodId, ECatEquipmentKind::Rod));
			TStrongObjectPtr<UCatEquipmentDefinition>& SpecialBait = RuntimeDefinitions.Emplace_GetRef(
				MakeDefinition(SpecialBaitId, ECatEquipmentKind::Bait));
			SpecialBait->bSpecialBait = true;
			SpecialBait->bRunConsumable = true;
			TStrongObjectPtr<UCatEquipmentDefinition>& NormalBait = RuntimeDefinitions.Emplace_GetRef(
				MakeDefinition(NormalBaitId, ECatEquipmentKind::Bait));
			TStrongObjectPtr<UCatEquipmentDefinition>& Float = RuntimeDefinitions.Emplace_GetRef(
				MakeDefinition(FloatId, ECatEquipmentKind::Float));
			TStrongObjectPtr<UCatEquipmentDefinition>& Driftwood = RuntimeDefinitions.Emplace_GetRef(
				MakeDefinition(DriftwoodId, ECatEquipmentKind::Driftwood));
			Driftwood->bRunConsumable = true;
			Settings->Definitions = { Rod.Get(), SpecialBait.Get(), NormalBait.Get(), Float.Get(), Driftwood.Get() };
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			Settings->DriftwoodDefinitionId = DriftwoodId;

			if (!Test.TestTrue(TEXT("Create authority fishing-use Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
			{
				return false;
			}
			UWorld* World = WorldWrapper.GetTestWorld();
			Pawn = World ? World->SpawnActor<APawn>() : nullptr;
			PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
			Test.TestNotNull(TEXT("Spawn authority fishing-use Pawn"), Pawn);
			Test.TestNotNull(TEXT("Spawn fishing-use PlayerState"), PlayerState);
			if (!Pawn || !PlayerState)
			{
				return false;
			}
			Pawn->SetPlayerState(PlayerState);
			Component = NewObject<UCatEquipmentComponent>(Pawn);
			Pawn->AddInstanceComponent(Component);
			Component->RegisterComponent();
			Test.TestNotNull(TEXT("Create fishing-use EquipmentComponent"), Component);
			return Component != nullptr;
		}

		bool ConfigureSpecial(FAutomationTestBase& Test, const int32 SpecialBaitQuantity = 1)
		{
			if (!Component)
			{
				return false;
			}
			const FCatDomainCommandResult Configure = Component->ConfigureLoadoutFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, RodId, SpecialBaitId, FloatId);
			Test.TestTrue(TEXT("Configure special-bait fishing loadout"), Configure.bCommitted);
			if (!Configure.bCommitted)
			{
				return false;
			}
			const FCatDomainCommandResult Grant = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, SpecialBaitId, SpecialBaitQuantity);
			Test.TestTrue(TEXT("Grant special bait through public authority API"), Grant.bCommitted);
			return Grant.bCommitted;
		}

		bool ConfigureNormal(FAutomationTestBase& Test, const int32 NormalBaitQuantity = 1)
		{
			if (!Component)
			{
				return false;
			}
			const FCatDomainCommandResult Configure = Component->ConfigureLoadoutFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, RodId, NormalBaitId, FloatId);
			Test.TestTrue(TEXT("Configure normal-bait fishing loadout"), Configure.bCommitted);
			if (!Configure.bCommitted || NormalBaitQuantity <= 0)
			{
				return Configure.bCommitted;
			}
			const FCatDomainCommandResult Grant = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, NormalBaitId, NormalBaitQuantity);
			Test.TestTrue(TEXT("Grant normal bait through public authority API"), Grant.bCommitted);
			return Grant.bCommitted;
		}

		~FFishingUseFixture()
		{
			if (Settings)
			{
				Settings->Definitions = SavedDefinitions;
				Settings->ProfileLoadoutTrustPolicy = SavedTrustPolicy;
				Settings->DriftwoodDefinitionId = SavedDriftwoodDefinitionId;
			}
		}
	};

	static void TestSnapshotUnchanged(FAutomationTestBase& Test, const TCHAR* Label,
		const FCatEquipmentLoadoutSnapshot& Before, const FCatEquipmentLoadoutSnapshot& After)
	{
		Test.TestEqual(FString::Printf(TEXT("%s leaves revision unchanged"), Label), After.Revision, Before.Revision);
		Test.TestEqual(FString::Printf(TEXT("%s leaves rod durability unchanged"), Label), After.RodDurability, Before.RodDurability);
		Test.TestEqual(FString::Printf(TEXT("%s leaves rod broken unchanged"), Label), After.bRodBroken, Before.bRodBroken);
		Test.TestEqual(FString::Printf(TEXT("%s leaves special bait inventory unchanged"), Label), QuantityOf(After, SpecialBaitId), QuantityOf(Before, SpecialBaitId));
		Test.TestEqual(FString::Printf(TEXT("%s leaves normal bait inventory unchanged"), Label), QuantityOf(After, NormalBaitId), QuantityOf(Before, NormalBaitId));
	}
}

bool FCatEquipmentFishingUseBeginTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	{
	FFishingUseFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
	{
		return false;
	}

	const FCatEquipmentLoadoutSnapshot Before = Fixture.Component->GetSnapshot();
	const FGuid RetriedSessionId = FGuid::NewGuid();
	const FCatFishingUseReservationResult Stale = Fixture.Component->BeginFishingUse(
		RetriedSessionId, RodId, SpecialBaitId, FloatId, Before.Revision - 1);
	TestFalse(TEXT("Stale Begin is rejected without a tombstone"), Stale.bReserved);
	TestEqual(TEXT("Stale Begin reports revision conflict"), Stale.Error, ECatDomainCommandError::RevisionConflict);
	TestFalse(TEXT("Stale Begin does not activate its session"), Fixture.Component->IsFishingUseActive(RetriedSessionId));
	TestSnapshotUnchanged(*this, TEXT("Stale begin"), Before, Fixture.Component->GetSnapshot());
	const FCatFishingUseReservationResult Retried = Fixture.Component->BeginFishingUse(
		RetriedSessionId, RodId, SpecialBaitId, FloatId, Before.Revision);
	TestTrue(TEXT("Same session can retry Begin after a revision conflict"), Retried.bReserved);
	Fixture.Component->ReleaseFishingUse(RetriedSessionId);

	const FGuid SessionId = FGuid::NewGuid();
	const FCatFishingUseReservationResult Begin = Fixture.Component->BeginFishingUse(
		SessionId, RodId, SpecialBaitId, FloatId, Before.Revision);
	TestTrue(TEXT("Begin reserves matching special-bait fishing use"), Begin.bReserved);
	TestEqual(TEXT("Begin reservation preserves public revision"), Begin.EquipmentRevision, Before.Revision);
	TestTrue(TEXT("Begin creates active session"), Fixture.Component->IsFishingUseActive(SessionId));
	const FCatFishingUseOperationResult SetWear = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 12.5);
	TestTrue(TEXT("Set wear records reservation observable before Begin replay"), SetWear.bApplied);
	TestEqual(TEXT("Set wear does not publish Equipment revision before Begin replay"), Fixture.Component->GetSnapshot().Revision, Before.Revision);

	const FCatFishingUseReservationResult Replay = Fixture.Component->BeginFishingUse(
		SessionId, RodId, SpecialBaitId, FloatId, Before.Revision);
	TestTrue(TEXT("Special-bait Begin replay returns its existing reservation"), Replay.bReserved);
	TestEqual(TEXT("Begin replay is already resolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("Begin replay returns current reservation revision"), Replay.EquipmentRevision, Fixture.Component->GetSnapshot().Revision);
	TestEqual(TEXT("Begin replay returns current wear sequence"), Replay.WearSequence, int64{1});
	TestEqual(TEXT("Begin replay returns current absolute rod wear"), Replay.AbsoluteRodWear, 12.5);
	TestEqual(TEXT("Begin replay keeps public revision"), Fixture.Component->GetSnapshot().Revision, Before.Revision);

	const FCatFishingUseReservationResult Exclusive = Fixture.Component->BeginFishingUse(
		FGuid::NewGuid(), RodId, SpecialBaitId, FloatId, Before.Revision);
	TestFalse(TEXT("Second session is rejected while a reservation is active"), Exclusive.bReserved);
	TestTrue(TEXT("First reservation remains active after exclusive rejection"), Fixture.Component->IsFishingUseActive(SessionId));

	const FCatFishingUseOperationResult Release = Fixture.Component->ReleaseFishingUse(SessionId);
	TestTrue(TEXT("Release succeeds for active reservation"), Release.bApplied);
	const FCatFishingUseReservationResult Tombstone = Fixture.Component->BeginFishingUse(
		SessionId, RodId, SpecialBaitId, FloatId, Before.Revision);
	TestFalse(TEXT("Released session cannot begin a second reservation"), Tombstone.bReserved);
	TestEqual(TEXT("Released begin reports tombstone"), Tombstone.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("Released Begin replay fails closed for wear sequence"), Tombstone.WearSequence, int64{0});
	TestEqual(TEXT("Released Begin replay fails closed for absolute wear"), Tombstone.AbsoluteRodWear, 0.0);

	const FCatEquipmentLoadoutSnapshot AtomicBefore = Fixture.Component->GetSnapshot();
	const FCatFishingUseReservationResult Invalid = Fixture.Component->BeginFishingUse(
		FGuid::NewGuid(), RodId, NormalBaitId, FloatId, AtomicBefore.Revision);
	TestFalse(TEXT("Begin with mismatched bait leaves no reservation"), Invalid.bReserved);
	TestFalse(TEXT("Invalid begin leaves no active reservation"), Fixture.Component->HasActiveFishingUse());
	TestSnapshotUnchanged(*this, TEXT("Invalid begin"), AtomicBefore, Fixture.Component->GetSnapshot());
	const FGuid UnknownSessionId = FGuid::NewGuid();
	const FCatFishingUseOperationResult UnknownBait = Fixture.Component->CommitFishingBait(UnknownSessionId);
	TestEqual(TEXT("Unknown bait commit fails closed"), UnknownBait.Error, ECatDomainCommandError::NotFound);
	TestSnapshotUnchanged(*this, TEXT("Unknown bait commit"), AtomicBefore, Fixture.Component->GetSnapshot());
	Fixture.WorldWrapper.ForwardErrorMessages(this);
	}

	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureNormal(*this))
		{
			return false;
		}

		const FCatEquipmentLoadoutSnapshot Before = Fixture.Component->GetSnapshot();
		const FGuid SessionId = FGuid::NewGuid();
		const FCatFishingUseReservationResult Begin = Fixture.Component->BeginFishingUse(
			SessionId, RodId, NormalBaitId, FloatId, Before.Revision);
		TestTrue(TEXT("Normal-bait Begin reserves one run-consumable bait"), Begin.bReserved);
		const FCatFishingUseOperationResult SetWear = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 12.5);
		TestTrue(TEXT("Normal-bait session records pending wear"), SetWear.bApplied);
		const FCatFishingUseReservationResult Replay = Fixture.Component->BeginFishingUse(
			SessionId, RodId, NormalBaitId, FloatId, Before.Revision);
		TestTrue(TEXT("Normal-bait Begin replay keeps the reserved bait"), Replay.bReserved);
		TestEqual(TEXT("Normal-bait Begin replay is already resolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("Normal-bait Begin replay returns current wear sequence"), Replay.WearSequence, int64{1});
		TestEqual(TEXT("Normal-bait Begin replay returns current absolute wear"), Replay.AbsoluteRodWear, 12.5);
		TestEqual(TEXT("Normal-bait Begin replay keeps public revision"), Fixture.Component->GetSnapshot().Revision, Before.Revision);
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureNormal(*this, 0))
		{
			return false;
		}

		const FCatEquipmentLoadoutSnapshot Before = Fixture.Component->GetSnapshot();
		const FCatFishingUseReservationResult Begin = Fixture.Component->BeginFishingUse(
			FGuid::NewGuid(), RodId, NormalBaitId, FloatId, Before.Revision);
		TestFalse(TEXT("Normal-bait Begin without inventory is rejected"), Begin.bReserved);
		TestEqual(TEXT("Normal-bait Begin without inventory reports capacity"), Begin.Error, ECatDomainCommandError::CapacityExceeded);
		TestFalse(TEXT("Normal-bait rejection does not activate fishing use"), Fixture.Component->HasActiveFishingUse());
		TestSnapshotUnchanged(*this, TEXT("Normal-bait missing inventory begin"), Before, Fixture.Component->GetSnapshot());
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

bool FCatEquipmentFishingUseBaitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
		{
			return false;
		}

		const FGuid SessionId = FGuid::NewGuid();
		Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
		const FCatEquipmentLoadoutSnapshot BeforeCommit = Fixture.Component->GetSnapshot();
		const FCatFishingUseOperationResult First = Fixture.Component->CommitFishingBait(SessionId);
		TestTrue(TEXT("Special bait commit applies once"), First.bApplied);
		TestEqual(TEXT("Special bait commit removes exactly one bait"), QuantityOf(Fixture.Component->GetSnapshot(), SpecialBaitId), 0);
		TestEqual(TEXT("Special bait commit advances revision once"), Fixture.Component->GetSnapshot().Revision, BeforeCommit.Revision + 1);
		const FCatFishingUseOperationResult Replay = Fixture.Component->CommitFishingBait(SessionId);
		TestFalse(TEXT("Special bait replay does not apply again"), Replay.bApplied);
		TestEqual(TEXT("Special bait replay is already resolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("Special bait replay keeps revision"), Fixture.Component->GetSnapshot().Revision, First.EquipmentRevision);

		const FCatFishingUseOperationResult Release = Fixture.Component->ReleaseFishingUse(SessionId);
		TestTrue(TEXT("Release after bait commit succeeds"), Release.bApplied);
		const FCatFishingUseOperationResult Late = Fixture.Component->CommitFishingBait(SessionId);
		TestFalse(TEXT("Released bait write cannot apply"), Late.bApplied);
		TestEqual(TEXT("Released bait write is tombstoned"), Late.Error, ECatDomainCommandError::AlreadyResolved);
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureNormal(*this))
		{
			return false;
		}
		const FGuid SessionId = FGuid::NewGuid();
		Fixture.Component->BeginFishingUse(SessionId, RodId, NormalBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
		const int64 BeforeCommitRevision = Fixture.Component->GetSnapshot().Revision;
		const FCatFishingUseOperationResult Commit = Fixture.Component->CommitFishingBait(SessionId);
		TestTrue(TEXT("Normal bait records its commit"), Commit.bApplied);
		TestEqual(TEXT("Normal bait commit removes exactly one bait"), QuantityOf(Fixture.Component->GetSnapshot(), NormalBaitId), 0);
		TestEqual(TEXT("Normal bait commit advances revision once"), Fixture.Component->GetSnapshot().Revision, BeforeCommitRevision + 1);
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

bool FCatEquipmentFishingUseWearTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	FFishingUseFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
	{
		return false;
	}

	const FGuid SessionId = FGuid::NewGuid();
	Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
	const FCatEquipmentLoadoutSnapshot BeforeUnsetCommit = Fixture.Component->GetSnapshot();
	const FCatFishingUseOperationResult UnsetCommit = Fixture.Component->CommitFishingRodWear(SessionId);
	TestFalse(TEXT("Wear commit without Set fails closed"), UnsetCommit.bApplied);
	TestSnapshotUnchanged(*this, TEXT("Unset wear commit"), BeforeUnsetCommit, Fixture.Component->GetSnapshot());
	const FCatFishingUseOperationResult ZeroSequence = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 0, 0.0);
	TestFalse(TEXT("First wear sequence must be one"), ZeroSequence.bApplied);
	TestEqual(TEXT("First zero wear sequence is invalid"), ZeroSequence.Error, ECatDomainCommandError::InvalidPayload);
	TestSnapshotUnchanged(*this, TEXT("Zero first wear sequence"), BeforeUnsetCommit, Fixture.Component->GetSnapshot());
	const int64 BeforeWearRevision = Fixture.Component->GetSnapshot().Revision;
	const FCatFishingUseOperationResult First = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 12.5);
	TestTrue(TEXT("First absolute wear sequence is accepted"), First.bApplied);
	TestEqual(TEXT("Wear updates do not publish revision"), Fixture.Component->GetSnapshot().Revision, BeforeWearRevision);
	const FCatFishingUseOperationResult Replay = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 12.5);
	TestFalse(TEXT("Equal sequence and total is a replay"), Replay.bApplied);
	TestEqual(TEXT("Equal sequence and total replay is resolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);

	const FCatEquipmentLoadoutSnapshot BeforeRejectedWear = Fixture.Component->GetSnapshot();
	const FCatFishingUseOperationResult SameSequenceDifferentValue = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 13.0);
	TestFalse(TEXT("Same sequence with another total is rejected"), SameSequenceDifferentValue.bApplied);
	TestSnapshotUnchanged(*this, TEXT("Same-sequence different-value wear"), BeforeRejectedWear, Fixture.Component->GetSnapshot());
	const FCatFishingUseOperationResult Skipped = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 3, 20.0);
	TestFalse(TEXT("Skipped wear sequence is rejected"), Skipped.bApplied);
	const FCatFishingUseOperationResult Decreasing = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 2, 10.0);
	TestFalse(TEXT("Decreasing absolute wear is rejected"), Decreasing.bApplied);
	const FCatFishingUseOperationResult Negative = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 2, -1.0);
	TestFalse(TEXT("Negative absolute wear is rejected"), Negative.bApplied);
	const FCatFishingUseOperationResult NotFinite = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 2, std::numeric_limits<double>::infinity());
	TestFalse(TEXT("Infinite absolute wear is rejected"), NotFinite.bApplied);
	const FCatFishingUseOperationResult NotANumber = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 2, std::numeric_limits<double>::quiet_NaN());
	TestFalse(TEXT("NaN absolute wear is rejected"), NotANumber.bApplied);
	const FCatFishingUseOperationResult Second = Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 2, 20.0);
	TestTrue(TEXT("Next monotonic absolute wear is accepted after rejections"), Second.bApplied);

	const FCatFishingUseOperationResult Commit = Fixture.Component->CommitFishingRodWear(SessionId);
	TestTrue(TEXT("Accumulated wear commits exactly once"), Commit.bApplied);
	TestEqual(TEXT("Absolute 20 wear leaves 80 durability"), Fixture.Component->GetSnapshot().RodDurability, 80.0);
	const FCatFishingUseOperationResult CommitReplay = Fixture.Component->CommitFishingRodWear(SessionId);
	TestFalse(TEXT("Committed wear cannot apply twice"), CommitReplay.bApplied);
	TestEqual(TEXT("Committed wear replay preserves 80 durability"), Fixture.Component->GetSnapshot().RodDurability, 80.0);

	Fixture.WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

bool FCatEquipmentFishingUseBreakTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
		{
			return false;
		}

		const FGuid SessionId = FGuid::NewGuid();
		Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
		Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 100.0);
		const FCatFishingUseOperationResult Wear = Fixture.Component->CommitFishingRodWear(SessionId);
		TestFalse(TEXT("Wear reaching remaining durability requires break"), Wear.bApplied);
		TestEqual(TEXT("Rejected terminal wear keeps rod intact"), Fixture.Component->GetSnapshot().RodDurability, 100.0);
		const int64 BeforeBreakRevision = Fixture.Component->GetSnapshot().Revision;
		const FCatFishingUseOperationResult Break = Fixture.Component->CommitFishingRodBreak(SessionId);
		TestTrue(TEXT("Rod break overrides pending wear"), Break.bApplied);
		TestEqual(TEXT("Rod break leaves exactly zero durability"), Fixture.Component->GetSnapshot().RodDurability, 0.0);
		TestTrue(TEXT("Rod break records broken snapshot"), Fixture.Component->GetSnapshot().bRodBroken);
		TestEqual(TEXT("Rod break advances revision exactly once"), Fixture.Component->GetSnapshot().Revision, BeforeBreakRevision + 1);
		const FCatFishingUseOperationResult BreakReplay = Fixture.Component->CommitFishingRodBreak(SessionId);
		TestFalse(TEXT("Rod break replay does not apply twice"), BreakReplay.bApplied);
		TestEqual(TEXT("Rod break replay keeps zero durability"), Fixture.Component->GetSnapshot().RodDurability, 0.0);
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	{
		FFishingUseFixture Fixture;
		if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
		{
			return false;
		}

		const FGuid SessionId = FGuid::NewGuid();
		Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
		Fixture.Component->SetAccumulatedFishingRodWear(SessionId, 1, 10.0);
		const FCatFishingUseOperationResult Wear = Fixture.Component->CommitFishingRodWear(SessionId);
		TestTrue(TEXT("Wear below durability commits"), Wear.bApplied);
		const FCatEquipmentLoadoutSnapshot AfterWearCommit = Fixture.Component->GetSnapshot();
		const FCatFishingUseOperationResult LateBreak = Fixture.Component->CommitFishingRodBreak(SessionId);
		TestFalse(TEXT("Break after committed wear cannot apply"), LateBreak.bApplied);
		TestEqual(TEXT("Break after committed wear is already resolved"), LateBreak.Error, ECatDomainCommandError::AlreadyResolved);
		TestSnapshotUnchanged(*this, TEXT("Late break after committed wear"), AfterWearCommit, Fixture.Component->GetSnapshot());
		Fixture.WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

bool FCatEquipmentFishingUseLegacyGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	FFishingUseFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this))
	{
		return false;
	}

	const FGuid SessionId = FGuid::NewGuid();
	Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId, Fixture.Component->GetSnapshot().Revision);
	const FCatEquipmentLoadoutSnapshot BeforeLegacy = Fixture.Component->GetSnapshot();
	const FCatDomainCommandResult Configure = Fixture.Component->ConfigureLoadoutFromAuthority(
		FGuid::NewGuid(), BeforeLegacy.Revision, RodId, SpecialBaitId, FloatId);
	TestFalse(TEXT("Active reservation blocks configure"), Configure.bCommitted);
	TestSnapshotUnchanged(*this, TEXT("Blocked configure"), BeforeLegacy, Fixture.Component->GetSnapshot());
	const FCatDomainCommandResult Repair = Fixture.Component->RepairRodAtCamp(FGuid::NewGuid(), BeforeLegacy.Revision, true);
	TestFalse(TEXT("Active reservation blocks repair"), Repair.bCommitted);
	TestSnapshotUnchanged(*this, TEXT("Blocked repair"), BeforeLegacy, Fixture.Component->GetSnapshot());
	const FCatFishingFailureResult Failure = Fixture.Component->CommitFishingFailure(
		FGuid::NewGuid(), BeforeLegacy.Revision, ECatFishingFailurePenalty::DamageRod);
	TestFalse(TEXT("Active reservation blocks legacy fishing failure"), Failure.Command.bCommitted);
	TestSnapshotUnchanged(*this, TEXT("Blocked fishing failure"), BeforeLegacy, Fixture.Component->GetSnapshot());
	const FCatDomainCommandResult ConsumeReserved = Fixture.Component->ConsumeRunConsumableFromAuthority(
		FGuid::NewGuid(), BeforeLegacy.Revision, SpecialBaitId);
	TestFalse(TEXT("Consume cannot steal last reserved special bait"), ConsumeReserved.bCommitted);
	TestSnapshotUnchanged(*this, TEXT("Blocked reserved bait consume"), BeforeLegacy, Fixture.Component->GetSnapshot());

	const FCatDomainCommandResult GrantSecond = Fixture.Component->GrantRunConsumableFromAuthority(
		FGuid::NewGuid(), Fixture.Component->GetSnapshot().Revision, SpecialBaitId, 1);
	TestTrue(TEXT("Granting another special bait remains allowed"), GrantSecond.bCommitted);
	const FCatDomainCommandResult ConsumeUnreserved = Fixture.Component->ConsumeRunConsumableFromAuthority(
		FGuid::NewGuid(), Fixture.Component->GetSnapshot().Revision, SpecialBaitId);
	TestTrue(TEXT("Consume may deduct only the unreserved second bait"), ConsumeUnreserved.bCommitted);
	TestEqual(TEXT("Reserved bait remains for commit"), QuantityOf(Fixture.Component->GetSnapshot(), SpecialBaitId), 1);
	const FCatFishingUseOperationResult CommitReserved = Fixture.Component->CommitFishingBait(SessionId);
	TestTrue(TEXT("Reserved last bait can still commit once"), CommitReserved.bApplied);
	TestEqual(TEXT("Reservation commit consumes final protected bait"), QuantityOf(Fixture.Component->GetSnapshot(), SpecialBaitId), 0);
	Fixture.WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

bool FCatEquipmentFishingUseDeferredBaitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatEquipmentFishingUseTest;
	FFishingUseFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.ConfigureSpecial(*this)) return false;
	const FGuid SessionId = FGuid::NewGuid();
	Fixture.Component->BeginFishingUse(SessionId, RodId, SpecialBaitId, FloatId,
		Fixture.Component->GetSnapshot().Revision);
	int32 PublishCount = 0;
	Fixture.Component->OnSnapshotChanged.AddLambda([&PublishCount]() { ++PublishCount; });
	const FCatFishingUseOperationResult Commit = Fixture.Component->CommitFishingBaitDeferred(SessionId);
	TestTrue(TEXT("deferred special bait commit succeeds"), Commit.bApplied);
	TestEqual(TEXT("deferred commit has not published"), PublishCount, 0);
	Fixture.Component->PublishDeferredFishingBait(SessionId);
	Fixture.Component->PublishDeferredFishingBait(SessionId);
	TestEqual(TEXT("idempotent publish emits once"), PublishCount, 1);
	Fixture.WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
