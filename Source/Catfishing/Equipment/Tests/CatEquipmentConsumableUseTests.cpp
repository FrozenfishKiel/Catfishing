#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Pawn.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace CatEquipmentConsumableUseTest
{
	static const FName RodId(TEXT("RunUseRod"));
	static const FName BaitId(TEXT("RunUseBait"));
	static const FName FloatId(TEXT("RunUseFloat"));
	static const FName ConsumableId(TEXT("RunUseHerb"));

	/** 定义构造流程：创建最小 transient 装备定义，并让 Bait/Herb 都满足正式局内消耗品 gate。 */
	static UCatEquipmentDefinition* MakeDefinition(FName Id, ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = Id;
		Definition->Kind = Kind;
		Definition->FunctionalRouteId = *FString::Printf(TEXT("%sRoute"), *Id.ToString());
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
		if (Kind == ECatEquipmentKind::Bait)
		{
			Definition->bRunConsumable = true;
			Definition->BiteRateMultiplier = 1.0;
			Definition->MinimumBiteDelayMultiplier = 1.0;
		}
		if (Kind == ECatEquipmentKind::Float)
		{
			Definition->MaximumCastDistanceCentimeters = 1500.0;
			Definition->CastErrorStandardDeviationCentimeters = 10.0;
			Definition->MaximumCastErrorRadiusCentimeters = 30.0;
			Definition->BiteSignalStability = 1.0;
		}
		if (Kind == ECatEquipmentKind::Herb)
		{
			Definition->bRunConsumable = true;
		}
		return Definition;
	}

	static int32 QuantityOf(const FCatEquipmentLoadoutSnapshot& Snapshot)
	{
		const FCatRunConsumableStack* Stack = Snapshot.Consumables.FindByPredicate([](const FCatRunConsumableStack& Item)
		{
			return Item.DefinitionId == ConsumableId;
		});
		return Stack ? Stack->Quantity : 0;
	}

	struct FFixture
	{
		UCatEquipmentSettings* Settings = nullptr;
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;
		ECatDomainPolicy SavedTrust = ECatDomainPolicy::Unset;
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
		FTestWorldWrapper World;
		APawn* Pawn = nullptr;
		UCatEquipmentComponent* Component = nullptr;

		bool Create(FAutomationTestBase& Test, int32 Quantity = 3)
		{
			Settings = GetMutableDefault<UCatEquipmentSettings>();
			SavedDefinitions = Settings->Definitions;
			SavedTrust = Settings->ProfileLoadoutTrustPolicy;
			Definitions.Emplace(MakeDefinition(RodId, ECatEquipmentKind::Rod));
			Definitions.Emplace(MakeDefinition(BaitId, ECatEquipmentKind::Bait));
			Definitions.Emplace(MakeDefinition(FloatId, ECatEquipmentKind::Float));
			Definitions.Emplace(MakeDefinition(ConsumableId, ECatEquipmentKind::Herb));
			Settings->Definitions = {Definitions[0].Get(), Definitions[1].Get(), Definitions[2].Get(), Definitions[3].Get()};
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			if (!World.CreateTestWorld(EWorldType::Game)) return false;
			UWorld* TestWorld = World.GetTestWorld();
			Pawn = TestWorld->SpawnActor<APawn>();
			ACatfishingPlayerState* PlayerState = TestWorld->SpawnActor<ACatfishingPlayerState>();
			Pawn->SetPlayerState(PlayerState);
			Component = NewObject<UCatEquipmentComponent>(Pawn);
			Pawn->AddInstanceComponent(Component);
			Component->RegisterComponent();
			const FCatDomainCommandResult Configure = Component->ConfigureLoadoutFromAuthority(
				FGuid::NewGuid(), 0, RodId, BaitId, FloatId);
			const FCatDomainCommandResult Grant = Component->GrantRunConsumableFromAuthority(
				FGuid::NewGuid(), Component->GetSnapshot().Revision, ConsumableId, Quantity);
			Test.TestTrue(TEXT("fixture configured"), Configure.bCommitted && Grant.bCommitted);
			return Configure.bCommitted && Grant.bCommitted;
		}

		~FFixture()
		{
			if (Settings)
			{
				Settings->Definitions = SavedDefinitions;
				Settings->ProfileLoadoutTrustPolicy = SavedTrust;
			}
		}
	};
}

#define CAT_RUN_USE_TEST(ClassName, Suffix) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "Catfishing.Unit.Equipment.ConsumableUse." Suffix, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

CAT_RUN_USE_TEST(FCatRunUseBeginTest, "BeginReservesExactQuantityWithoutPublishingSnapshot")
CAT_RUN_USE_TEST(FCatRunUseCommitTest, "CommitConsumesOnceAndReplayReturnsFrozenObservables")
CAT_RUN_USE_TEST(FCatRunUseDeferredTest, "DeferredCommitIsInvisibleUntilIdempotentPublish")
CAT_RUN_USE_TEST(FCatRunUseReleaseTest, "ReleasePreservesInventoryAndRejectsLateCommit")
CAT_RUN_USE_TEST(FCatRunUseMutationGateTest, "ActiveReservationBlocksConflictingMutations")
CAT_RUN_USE_TEST(FCatRunUseFishingMutexTest, "FishingAndRunReservationsAreMutuallyExclusive")
CAT_RUN_USE_TEST(FCatRunUseLegacySpendTest, "ReservedQuantityCannotBeDoubleSpentByLegacyConsume")

bool FCatRunUseBeginTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FCatEquipmentLoadoutSnapshot Before = F.Component->GetSnapshot();
	const FCatRunConsumableUseResult Result = F.Component->BeginRunConsumableUse(FGuid::NewGuid(), ConsumableId, 2, Before.Revision);
	TestTrue(TEXT("exact quantity reserved"), Result.bReserved);
	TestEqual(TEXT("result freezes quantity"), Result.Quantity, 2);
	TestEqual(TEXT("begin does not change revision"), F.Component->GetSnapshot().Revision, Before.Revision);
	TestEqual(TEXT("begin does not consume"), QuantityOf(F.Component->GetSnapshot()), QuantityOf(Before));
	return !HasAnyErrors();
}

bool FCatRunUseCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FGuid Id = FGuid::NewGuid();
	F.Component->BeginRunConsumableUse(Id, ConsumableId, 2, F.Component->GetSnapshot().Revision);
	const FCatRunConsumableUseResult First = F.Component->CommitRunConsumableUse(Id);
	const FCatRunConsumableUseResult Replay = F.Component->CommitRunConsumableUse(Id);
	TestTrue(TEXT("first commit consumes"), First.bCommitted);
	TestEqual(TEXT("exact quantity consumed once"), QuantityOf(F.Component->GetSnapshot()), 1);
	TestEqual(TEXT("replay definition frozen"), Replay.DefinitionId, First.DefinitionId);
	TestEqual(TEXT("replay quantity frozen"), Replay.Quantity, First.Quantity);
	TestEqual(TEXT("replay revision frozen"), Replay.EquipmentRevision, First.EquipmentRevision);
	TestTrue(TEXT("replay retains committed terminal state"), Replay.bCommitted);
	return !HasAnyErrors();
}

bool FCatRunUseDeferredTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this)) return false;
	int32 Published = 0; F.Component->OnSnapshotChanged.AddLambda([&Published]() { ++Published; });
	const FGuid Id = FGuid::NewGuid();
	F.Component->BeginRunConsumableUse(Id, ConsumableId, 1, F.Component->GetSnapshot().Revision);
	const FCatRunConsumableUseResult Commit = F.Component->CommitRunConsumableUseDeferred(Id);
	TestTrue(TEXT("deferred commit succeeds"), Commit.bCommitted);
	TestEqual(TEXT("deferred commit does not publish"), Published, 0);
	F.Component->PublishDeferredRunConsumableUse(Id);
	F.Component->PublishDeferredRunConsumableUse(Id);
	TestEqual(TEXT("publish is idempotent"), Published, 1);
	return !HasAnyErrors();
}

bool FCatRunUseReleaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this)) return false;
	const FGuid Id = FGuid::NewGuid(); const int32 Before = QuantityOf(F.Component->GetSnapshot());
	F.Component->BeginRunConsumableUse(Id, ConsumableId, 2, F.Component->GetSnapshot().Revision);
	const FCatRunConsumableUseResult Released = F.Component->ReleaseRunConsumableUse(Id);
	const FCatRunConsumableUseResult Late = F.Component->CommitRunConsumableUse(Id);
	TestTrue(TEXT("release terminal is frozen"), Released.bReleased);
	TestFalse(TEXT("late commit cannot consume"), Late.bCommitted);
	TestTrue(TEXT("late replay remains released"), Late.bReleased);
	TestEqual(TEXT("release preserves quantity"), QuantityOf(F.Component->GetSnapshot()), Before);
	return !HasAnyErrors();
}

bool FCatRunUseMutationGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this)) return false;
	F.Component->BeginRunConsumableUse(FGuid::NewGuid(), ConsumableId, 2, F.Component->GetSnapshot().Revision);
	const FCatEquipmentLoadoutSnapshot Before = F.Component->GetSnapshot();
	const FCatDomainCommandResult Configure = F.Component->ConfigureLoadoutFromAuthority(FGuid::NewGuid(), Before.Revision, RodId, BaitId, FloatId);
	const FCatDomainCommandResult Grant = F.Component->GrantRunConsumableFromAuthority(FGuid::NewGuid(), Before.Revision, ConsumableId, 1);
	const FCatFishingFailureResult Failure = F.Component->CommitFishingFailure(FGuid::NewGuid(), Before.Revision, ECatFishingFailurePenalty::DamageRod);
	const FCatDomainCommandResult Repair = F.Component->RepairRodAtCamp(FGuid::NewGuid(), Before.Revision, true);
	TestFalse(TEXT("configure blocked"), Configure.bCommitted);
	TestFalse(TEXT("grant blocked"), Grant.bCommitted);
	TestFalse(TEXT("failure blocked"), Failure.Command.bCommitted);
	TestFalse(TEXT("repair blocked"), Repair.bCommitted);
	TestEqual(TEXT("configure uses reservation gate"), Configure.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("grant uses reservation gate"), Grant.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("failure uses reservation gate"), Failure.Command.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("repair uses reservation gate"), Repair.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("conflicts preserve revision"), F.Component->GetSnapshot().Revision, Before.Revision);
	return !HasAnyErrors();
}

bool FCatRunUseFishingMutexTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture RunFirst; if (!RunFirst.Create(*this)) return false;
	RunFirst.Component->BeginRunConsumableUse(FGuid::NewGuid(), ConsumableId, 1, RunFirst.Component->GetSnapshot().Revision);
	const FCatFishingUseReservationResult FishingBlocked = RunFirst.Component->BeginFishingUse(
		FGuid::NewGuid(), RodId, BaitId, FloatId, RunFirst.Component->GetSnapshot().Revision);
	TestNotEqual(TEXT("run reservation blocks fishing begin"), FishingBlocked.Error, ECatDomainCommandError::None);

	FFixture FishingFirst; if (!FishingFirst.Create(*this)) return false;
	const FCatDomainCommandResult BaitGrant = FishingFirst.Component->GrantRunConsumableFromAuthority(
		FGuid::NewGuid(), FishingFirst.Component->GetSnapshot().Revision, BaitId, 1);
	TestTrue(TEXT("fishing-first fixture grants bait inventory"), BaitGrant.bCommitted);
	FishingFirst.Component->BeginFishingUse(FGuid::NewGuid(), RodId, BaitId, FloatId, FishingFirst.Component->GetSnapshot().Revision);
	const FCatRunConsumableUseResult RunBlocked = FishingFirst.Component->BeginRunConsumableUse(
		FGuid::NewGuid(), ConsumableId, 1, FishingFirst.Component->GetSnapshot().Revision);
	TestFalse(TEXT("fishing reservation blocks run begin"), RunBlocked.bReserved);
	return !HasAnyErrors();
}

bool FCatRunUseLegacySpendTest::RunTest(const FString& Parameters)
{
	(void)Parameters; using namespace CatEquipmentConsumableUseTest;
	FFixture F; if (!F.Create(*this, 2)) return false;
	F.Component->BeginRunConsumableUse(FGuid::NewGuid(), ConsumableId, 2, F.Component->GetSnapshot().Revision);
	const FCatDomainCommandResult Legacy = F.Component->ConsumeRunConsumableFromAuthority(
		FGuid::NewGuid(), F.Component->GetSnapshot().Revision, ConsumableId);
	TestFalse(TEXT("legacy consume cannot spend reserved quantity"), Legacy.bCommitted);
	TestEqual(TEXT("legacy consume uses reservation gate"), Legacy.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("reserved inventory remains intact"), QuantityOf(F.Component->GetSnapshot()), 2);
	return !HasAnyErrors();
}

#undef CAT_RUN_USE_TEST

#endif
