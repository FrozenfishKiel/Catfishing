#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Fishing/CatFishingSession.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionPublicSnapshotDefaultsTest,
	"Catfishing.Unit.Fishing.Session.PublicSnapshotDefaultsExposeSeparatedConcurrencyIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionPublicSnapshotDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFishingSessionSnapshot Snapshot;
	TestEqual(TEXT("Default phase is Created"), Snapshot.Phase, ECatFishingPhase::Created);
	TestEqual(TEXT("Default outcome is None"), Snapshot.Outcome, ECatFishingOutcome::None);
	TestFalse(TEXT("Default session id is invalid"), Snapshot.FishingSessionId.IsValid());
	TestFalse(TEXT("Default cast attempt id is invalid"), Snapshot.CastAttemptId.IsValid());
	TestEqual(TEXT("Default revision is zero"), Snapshot.Revision, int64{0});
	TestEqual(TEXT("Default snapshot sequence is zero"), Snapshot.SnapshotSequence, int64{0});
	TestEqual(TEXT("Default phase epoch is zero"), Snapshot.PhaseEpoch, int64{0});
	TestEqual(TEXT("Default phase-start time is zero"), Snapshot.PhaseStartedServerTime, 0.0);
	TestEqual(TEXT("Default window-end time is zero"), Snapshot.WindowEndsServerTime, 0.0);
	TestNull(TEXT("Default fisher player state is null"), Snapshot.FisherPlayerState.Get());
	TestNull(TEXT("Default rod actor is null"), Snapshot.RodActor.Get());
	TestNull(TEXT("Default hook actor is null"), Snapshot.HookActor.Get());
	TestNull(TEXT("Default fish encounter actor is null"), Snapshot.FishEncounterActor.Get());
	TestEqual(TEXT("Default normalized fish stamina is zero"), Snapshot.NormalizedFishStamina, 0.0);
	TestFalse(TEXT("Default reeling intent is false"), Snapshot.bReeling);
	TestEqual(TEXT("Default fish motion intent is None"), Snapshot.FishMotionIntent, ECatFishMotionIntent::None);

	for (TFieldIterator<FProperty> It(FCatFishingSessionSnapshot::StaticStruct()); It; ++It)
	{
		const FProperty* Property = *It;
		const bool bFishPositionField = Property->GetName().Contains(TEXT("Fish"))
			&& (Property->GetName().Contains(TEXT("Position")) || Property->GetName().Contains(TEXT("Location"))
				|| Property->GetName().Contains(TEXT("Transform")));
		TestFalse(FString::Printf(TEXT("Snapshot does not duplicate fish world position: %s"), *Property->GetName()), bFishPositionField);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionTerminationOutcomeTest,
	"Catfishing.Unit.Fishing.Session.TerminationRequiresExplicitOutcomeAndIsIrreversible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionTerminationOutcomeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates authoritative test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("Spawns session"), Session);
	if (!Session)
	{
		return false;
	}

	const FCatFishingSessionSnapshot Before = Session->GetSnapshot();
	Session->TerminateSession(ECatFishingOutcome::None, TEXT("Ignored none"));
	Session->TerminateSession(ECatFishingOutcome::Caught, TEXT("Ignored caught"));
	Session->TerminateSession(static_cast<ECatFishingOutcome>(255), TEXT("Ignored invalid outcome"));
	TestEqual(TEXT("Rejected outcomes leave phase unchanged"), Session->GetSnapshot().Phase, Before.Phase);
	TestEqual(TEXT("Rejected outcomes leave version unchanged"), Session->GetSnapshot().Revision, Before.Revision);
	TestEqual(TEXT("Invalid outcome leaves sequence unchanged"), Session->GetSnapshot().SnapshotSequence, Before.SnapshotSequence);
	TestEqual(TEXT("Invalid outcome leaves epoch unchanged"), Session->GetSnapshot().PhaseEpoch, Before.PhaseEpoch);
	TestEqual(TEXT("Invalid outcome leaves explicit outcome unchanged"), Session->GetSnapshot().Outcome, Before.Outcome);

	ACatCharacter* FisherCharacter = World->SpawnActor<ACatCharacter>();
	APlayerState* FisherPlayerState = World->SpawnActor<APlayerState>();
	TestNotNull(TEXT("Spawns private fisher character for terminal cleanup"), FisherCharacter);
	TestNotNull(TEXT("Spawns public fisher player state fact"), FisherPlayerState);
	Session->FisherCharacter = FisherCharacter;
	Session->Snapshot.FisherPlayerState = FisherPlayerState;

	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
	Session->TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Automation invalidated"));
	const FCatFishingSessionSnapshot Terminal = Session->GetSnapshot();
	TestEqual(TEXT("First termination enters Terminated"), Terminal.Phase, ECatFishingPhase::Terminated);
	TestEqual(TEXT("First termination persists explicit outcome"), Terminal.Outcome, ECatFishingOutcome::Invalidated);
	TestEqual(TEXT("First termination advances revision once"), Terminal.Revision, Before.Revision + 1);
	TestEqual(TEXT("First termination advances sequence once"), Terminal.SnapshotSequence, Before.SnapshotSequence + 1);
	TestEqual(TEXT("First termination advances phase epoch once"), Terminal.PhaseEpoch, Before.PhaseEpoch + 1);
	TestFalse(TEXT("Terminal cleanup releases private fisher character weak reference"), Session->FisherCharacter.IsValid());
	TestEqual(TEXT("Terminal snapshot preserves public fisher player state fact"), Terminal.FisherPlayerState.Get(), FisherPlayerState);
	Session->TerminateSession(ECatFishingOutcome::Escaped, TEXT("Ignored replay"));
	const FCatFishingSessionSnapshot Replay = Session->GetSnapshot();
	TestEqual(TEXT("Second termination cannot overwrite outcome"), Replay.Outcome, Terminal.Outcome);
	TestEqual(TEXT("Second termination cannot advance revision"), Replay.Revision, Terminal.Revision);
	TestEqual(TEXT("Second termination cannot advance sequence"), Replay.SnapshotSequence, Terminal.SnapshotSequence);
	TestEqual(TEXT("Second termination cannot advance phase epoch"), Replay.PhaseEpoch, Terminal.PhaseEpoch);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionStateTreeTerminalPhaseTest,
	"Catfishing.Unit.Fishing.Session.StateTreeCannotEnterEitherTerminalPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionStateTreeTerminalPhaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("Spawns session"), Session);
	if (!Session)
	{
		return false;
	}
	const FCatFishingSessionSnapshot Before = Session->GetSnapshot();
	for (const ECatFishingPhase TerminalPhase : { ECatFishingPhase::Resolved, ECatFishingPhase::Terminated })
	{
		const FCatFishingPhaseResult Result = Session->EnterPhaseFromStateTree(TerminalPhase);
		TestFalse(TEXT("StateTree terminal entry is rejected"), Result.bApplied);
		TestEqual(TEXT("StateTree terminal entry reports an already-resolved guard"), Result.Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("StateTree terminal entry leaves phase unchanged"), Session->GetSnapshot().Phase, Before.Phase);
		TestEqual(TEXT("StateTree terminal entry leaves revision unchanged"), Session->GetSnapshot().Revision, Before.Revision);
		TestEqual(TEXT("StateTree terminal entry leaves sequence unchanged"), Session->GetSnapshot().SnapshotSequence, Before.SnapshotSequence);
		TestEqual(TEXT("StateTree terminal entry leaves epoch unchanged"), Session->GetSnapshot().PhaseEpoch, Before.PhaseEpoch);
		TestEqual(TEXT("StateTree terminal entry leaves outcome unchanged"), Session->GetSnapshot().Outcome, Before.Outcome);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionReplicationContractTest,
	"Catfishing.Unit.Fishing.Session.ActorIsAlwaysRelevantAndSnapshotUsesRepNotify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionReplicationContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("Spawns session"), Session);
	if (!Session)
	{
		return false;
	}
	TestTrue(TEXT("Session replicates"), Session->GetIsReplicated());
	TestTrue(TEXT("Session is always relevant"), Session->bAlwaysRelevant);
	TestFalse(TEXT("Session tick is disabled"), Session->PrimaryActorTick.bCanEverTick);
	FProperty* SnapshotProperty = FindFProperty<FProperty>(ACatFishingSession::StaticClass(), TEXT("Snapshot"));
	TestNotNull(TEXT("Snapshot reflected property exists"), SnapshotProperty);
	if (!SnapshotProperty)
	{
		return false;
	}
	TestTrue(TEXT("Snapshot is replicated"), SnapshotProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("Snapshot uses OnRep_Snapshot"), SnapshotProperty->RepNotifyFunc, FName(TEXT("OnRep_Snapshot")));
	UFunction* OnRepFunction = ACatFishingSession::StaticClass()->FindFunctionByName(TEXT("OnRep_Snapshot"));
	TestNotNull(TEXT("OnRep_Snapshot UFunction exists"), OnRepFunction);
	const FCatFishingSessionSnapshot BeforeNotification = Session->GetSnapshot();
	int32 SnapshotSignals = 0;
	const FDelegateHandle DelegateHandle = Session->OnSnapshotChanged.AddLambda([&SnapshotSignals]()
	{
		++SnapshotSignals;
	});
	Session->OnRep_Snapshot();
	Session->OnSnapshotChanged.Remove(DelegateHandle);
	TestEqual(TEXT("OnRep emits exactly one local reread signal"), SnapshotSignals, 1);
	TestEqual(TEXT("OnRep does not change snapshot phase"), Session->GetSnapshot().Phase, BeforeNotification.Phase);
	TestEqual(TEXT("OnRep does not change snapshot revision"), Session->GetSnapshot().Revision, BeforeNotification.Revision);
	TestEqual(TEXT("OnRep does not change snapshot version"), Session->GetSnapshot().SnapshotSequence, BeforeNotification.SnapshotSequence);
	TestEqual(TEXT("OnRep does not change snapshot phase epoch"), Session->GetSnapshot().PhaseEpoch, BeforeNotification.PhaseEpoch);
	TestEqual(TEXT("OnRep does not change snapshot outcome"), Session->GetSnapshot().Outcome, BeforeNotification.Outcome);
	Session->GetClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> LifetimeProps;
	Session->GetLifetimeReplicatedProps(LifetimeProps);
	TestTrue(TEXT("Snapshot has a real lifetime replication registration"), LifetimeProps.ContainsByPredicate(
		[SnapshotProperty](const FLifetimeProperty& LifetimeProperty)
		{
			return LifetimeProperty.RepIndex == SnapshotProperty->RepIndex;
		}));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionSnapshotVersionMutationRulesTest,
	"Catfishing.Unit.Fishing.Session.SnapshotVersionMutationRulesSeparateHighFrequencyDiscreteAndPhaseChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionSnapshotVersionMutationRulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates authoritative test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("Spawns session"), Session);
	if (!Session)
	{
		return false;
	}
	Session->Snapshot.Revision = 10;
	Session->Snapshot.SnapshotSequence = 20;
	Session->Snapshot.PhaseEpoch = 30;
	int32 SnapshotSignals = 0;
	const FDelegateHandle DelegateHandle = Session->OnSnapshotChanged.AddLambda([&SnapshotSignals]()
	{
		++SnapshotSignals;
	});
	Session->PublishSnapshot(ECatFishingSnapshotMutation::HighFrequency);
	TestEqual(TEXT("High frequency preserves revision"), Session->Snapshot.Revision, int64{10});
	TestEqual(TEXT("High frequency increments sequence"), Session->Snapshot.SnapshotSequence, int64{21});
	TestEqual(TEXT("High frequency preserves epoch"), Session->Snapshot.PhaseEpoch, int64{30});
	TestEqual(TEXT("High frequency authority publication emits exactly one local signal"), SnapshotSignals, 1);
	Session->PublishSnapshot(ECatFishingSnapshotMutation::Discrete);
	TestEqual(TEXT("Discrete increments revision"), Session->Snapshot.Revision, int64{11});
	TestEqual(TEXT("Discrete increments sequence"), Session->Snapshot.SnapshotSequence, int64{22});
	TestEqual(TEXT("Discrete preserves epoch"), Session->Snapshot.PhaseEpoch, int64{30});
	TestEqual(TEXT("Discrete authority publication emits exactly one additional local signal"), SnapshotSignals, 2);
	Session->PublishSnapshot(ECatFishingSnapshotMutation::PhaseChange);
	TestEqual(TEXT("Phase change increments revision"), Session->Snapshot.Revision, int64{12});
	TestEqual(TEXT("Phase change increments sequence"), Session->Snapshot.SnapshotSequence, int64{23});
	TestEqual(TEXT("Phase change increments epoch"), Session->Snapshot.PhaseEpoch, int64{31});
	TestEqual(TEXT("Phase change authority publication emits exactly one additional local signal"), SnapshotSignals, 3);
	const int32 BeforeRepNotifySignals = SnapshotSignals;
	const int64 BeforeRepNotifySequence = Session->Snapshot.SnapshotSequence;
	Session->OnRep_Snapshot();
	TestEqual(TEXT("RepNotify emits exactly one additional signal"),
		SnapshotSignals, BeforeRepNotifySignals + 1);
	TestEqual(TEXT("RepNotify never advances authority versions"),
		Session->Snapshot.SnapshotSequence, BeforeRepNotifySequence);
	Session->OnSnapshotChanged.Remove(DelegateHandle);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionExistingCaptureReconciliationTest,
	"Catfishing.Unit.Fishing.Session.ExistingItemsCaptureReconcilesUsingCommittedFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionExistingCaptureReconciliationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates authoritative reconciliation test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	AActor* ContainerHost = World->SpawnActor<AActor>();
	UCatContainerReplicationComponent* ContainerComponent = ContainerHost
		? NewObject<UCatContainerReplicationComponent>(ContainerHost) : nullptr;
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingSession* RejectingSession = World->SpawnActor<ACatFishingSession>();
	ACatFishingSession* NonAuthoritySession = World->SpawnActor<ACatFishingSession>();
	if (!TestNotNull(TEXT("Creates real Items service"), ItemsService)
		|| !TestNotNull(TEXT("Spawns Items container host"), ContainerHost)
		|| !TestNotNull(TEXT("Creates Items replication component"), ContainerComponent)
		|| !TestNotNull(TEXT("Spawns session for committed capture reconciliation"), Session)
		|| !TestNotNull(TEXT("Spawns session for malformed committed capture rejection"), RejectingSession)
		|| !TestNotNull(TEXT("Spawns non-authority session for reconciliation guard"), NonAuthoritySession))
	{
		return false;
	}
	ContainerHost->AddInstanceComponent(ContainerComponent);
	ContainerComponent->RegisterComponent();
	const FString FirstOwnerStableNetId = TEXT("FirstOwner");
	const FGuid ContainerId = FGuid::NewGuid();
	const FGuid FishingSessionId = FGuid::NewGuid();
	TestTrue(TEXT("Registers real personal guard"), ItemsService->RegisterContainer(ContainerComponent, ContainerId,
		ECatContainerKind::PersonalGuard, FirstOwnerStableNetId, 2));
	FCatContainerSnapshot ContainerSnapshot;
	if (!ItemsService->TryGetContainerSnapshot(ContainerId, ContainerSnapshot))
	{
		ItemsService->UnregisterContainer(ContainerComponent);
		return false;
	}
	FCatCaptureCommitCommand FirstCommand;
	FirstCommand.Context.RequestId = FGuid::NewGuid();
	FirstCommand.Context.ExpectedRevision = ContainerSnapshot.Revision;
	FirstCommand.Context.StableNetId = FirstOwnerStableNetId;
	FirstCommand.FishingSessionId = FishingSessionId;
	FirstCommand.FishInstanceId = FGuid::NewGuid();
	FirstCommand.FishDefinitionId = TEXT("ReconciledFish");
	FirstCommand.TargetContainerId = ContainerId;
	FirstCommand.WeightKilograms = 1.25;
	FirstCommand.SacrificeContribution = 3;
	const FCatCaptureCommitResult FirstResult = ItemsService->CommitCapture(FirstCommand);
	TestTrue(TEXT("Commits the real first capture"), FirstResult.Command.bCommitted);

	FCatCaptureCommitCommand ExistingSessionRequest = FirstCommand;
	ExistingSessionRequest.Context.RequestId = FGuid::NewGuid();
	ExistingSessionRequest.FishInstanceId = FGuid::NewGuid();
	const FCatCaptureCommitResult ExistingResult = ItemsService->CommitCapture(ExistingSessionRequest);
	TestFalse(TEXT("Existing session request does not create a second fish"), ExistingResult.Command.bCommitted);
	TestEqual(TEXT("Existing session request exposes committed Items fact"), ExistingResult.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("Existing session result retains original fish instance"), ExistingResult.Committed.FishInstance.FishInstanceId,
		FirstResult.Committed.FishInstance.FishInstanceId);
	TestEqual(TEXT("Existing session result retains original owner"), ExistingResult.Committed.FishInstance.OwnerStableNetId,
		FirstOwnerStableNetId);

	Session->Snapshot.FishingSessionId = FishingSessionId;
	Session->Snapshot.FishDefinitionId = FirstCommand.FishDefinitionId;
	TestTrue(TEXT("Existing committed fact synchronously resolves its fishing session"),
		Session->ReconcileCommittedCapture(ExistingResult.Committed));
	TestTrue(TEXT("Reconciliation marks capture resolved"), Session->bCaptureResolved);
	TestEqual(TEXT("Reconciliation enters Resolved"), Session->Snapshot.Phase, ECatFishingPhase::Resolved);
	TestEqual(TEXT("Reconciliation records Caught"), Session->Snapshot.Outcome, ECatFishingOutcome::Caught);

	NonAuthoritySession->Snapshot.FishingSessionId = FishingSessionId;
	NonAuthoritySession->Snapshot.FishDefinitionId = FirstCommand.FishDefinitionId;
	NonAuthoritySession->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Non-authority cannot reconcile an existing Items capture"),
		NonAuthoritySession->ReconcileCommittedCapture(ExistingResult.Committed));
	TestFalse(TEXT("Non-authority reconciliation leaves capture unresolved"), NonAuthoritySession->bCaptureResolved);
	TestEqual(TEXT("Non-authority reconciliation leaves phase unchanged"), NonAuthoritySession->Snapshot.Phase,
		ECatFishingPhase::Created);

	RejectingSession->Snapshot.FishingSessionId = FishingSessionId;
	RejectingSession->Snapshot.FishDefinitionId = FirstCommand.FishDefinitionId;
	FCatCaptureCommittedResult MismatchedCommitted = ExistingResult.Committed;
	MismatchedCommitted.FishInstance.OwnerStableNetId.Reset();
	TestFalse(TEXT("Incomplete committed DTO is rejected without finalizing"),
		RejectingSession->ReconcileCommittedCapture(MismatchedCommitted));
	TestEqual(TEXT("Rejected committed DTO leaves phase unchanged"), RejectingSession->Snapshot.Phase,
		ECatFishingPhase::Created);
	ItemsService->UnregisterContainer(ContainerComponent);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionRejectedFightSummaryPublicationTest,
	"Catfishing.Unit.Fishing.Session.RejectedFightRefreshPublishesOnlyChangedSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionRejectedFightSummaryPublicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates authoritative fight summary test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	if (!TestNotNull(TEXT("Spawns session"), Session))
	{
		return false;
	}
	Session->Snapshot.Revision = 10;
	Session->Snapshot.SnapshotSequence = 20;
	Session->Snapshot.PhaseEpoch = 30;
	Session->Snapshot.FightParticipantCount = 2;
	Session->Snapshot.CombinedFishingStrength = 8.0;
	Session->Snapshot.CombinedFightStamina = 6.0;
	const bool bSummaryChanged = Session->RefreshFightSummary();
	Session->PublishRefreshedFightSummaryIfChanged(bSummaryChanged);
	TestTrue(TEXT("Invalid participants refresh the stale public summary"), bSummaryChanged);
	TestEqual(TEXT("Changed rejected summary keeps revision"), Session->Snapshot.Revision, int64{10});
	TestEqual(TEXT("Changed rejected summary advances high-frequency sequence"), Session->Snapshot.SnapshotSequence, int64{21});
	TestEqual(TEXT("Changed rejected summary keeps phase epoch"), Session->Snapshot.PhaseEpoch, int64{30});
	TestFalse(TEXT("Unchanged summary does not publish an empty update"), Session->RefreshFightSummary());
	Session->PublishRefreshedFightSummaryIfChanged(false);
	TestEqual(TEXT("Empty refresh does not advance sequence"), Session->Snapshot.SnapshotSequence, int64{21});
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
