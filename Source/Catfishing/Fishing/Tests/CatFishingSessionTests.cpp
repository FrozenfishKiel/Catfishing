#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatFishPickupActor.h"
#include "OnlineSubsystemTypes.h"
#include "StateTree.h"

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
	TestEqual(TEXT("Default fish-line alignment is zero"), Snapshot.FishLineAlignment, 0.0f);
	TestEqual(TEXT("Default normalized line load is zero"), Snapshot.NormalizedLineLoad, 0.0f);
	TestFalse(TEXT("Default strong confrontation is false"), Snapshot.bStrongConfrontation);

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
	FCatFishingSessionLineBreakKeepsRodOperableTest,
	"Catfishing.Unit.Fishing.Session.LineBreakEndsOnlyCurrentSessionAndKeepsRodOperable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionOutcomePresentationTagTest,
	"Catfishing.Unit.Fishing.Session.TerminalLineOutcomesResolveDistinctCatPresentationTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionCutLineCommandTest,
	"Catfishing.Unit.Fishing.Session.CutLineIsRevisionGuardedIdempotentStopLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionGroundedCutLineCommandTest,
	"Catfishing.Unit.Fishing.Session.GroundedRodRetainsNearbyOwnerCutLineAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionOutcomePresentationTagTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("line break resolves the line-broken cat presentation"),
		ACatFishingSession::ResolveTerminalFisherPresentationTag(ECatFishingOutcome::LineBroken)
			== CatFishingAbilityTags::Cosmetic_Fishing_LineBroken);
	TestTrue(TEXT("voluntary line cut has its own server-confirmed presentation event"),
		ACatFishingSession::ResolveTerminalFisherPresentationTag(ECatFishingOutcome::LineCut)
			== CatFishingAbilityTags::Cosmetic_Fishing_LineCut);
	TestTrue(TEXT("cat in water resolves the cat-in-water presentation"),
		ACatFishingSession::ResolveTerminalFisherPresentationTag(ECatFishingOutcome::CatInWater)
			== CatFishingAbilityTags::Cosmetic_Fishing_CatInWater);
	TestFalse(TEXT("ordinary escape does not borrow either cat failure montage"),
		ACatFishingSession::ResolveTerminalFisherPresentationTag(ECatFishingOutcome::Escaped).IsValid());
	TestFalse(TEXT("successful catch does not borrow either cat failure montage"),
		ACatFishingSession::ResolveTerminalFisherPresentationTag(ECatFishingOutcome::Caught).IsValid());
	return !HasAnyErrors();
}

bool FCatFishingSessionCutLineCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates cut-line test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	if (!TestNotNull(TEXT("Spawns cut-line session"), Session)
		|| !TestNotNull(TEXT("Spawns cut-line rod"), Rod)
		|| !TestNotNull(TEXT("Spawns cut-line controller"), Controller)
		|| !TestNotNull(TEXT("Spawns cut-line player state"), PlayerState))
	{
		return false;
	}

	Controller->PlayerState = PlayerState;
	const FGuid RodActorId = FGuid::NewGuid();
	TestTrue(TEXT("Initializes held rod identity"), Rod->InitializeAuthoritativeIdentity(
		RodActorId, FGuid::NewGuid(), TEXT("Rod_CutLine"), TEXT("Skin_CutLine"),
		PlayerState, PlayerState, true, false));
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.CastAttemptId = FGuid::NewGuid();
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->Snapshot.Revision = 17;
	Session->Snapshot.SnapshotSequence = 23;
	Session->Snapshot.PhaseEpoch = 4;
	Session->Snapshot.FisherPlayerState = PlayerState;
	Session->Snapshot.RodActor = Rod;
	Session->Snapshot.RodDurabilityRemaining = 42.5;
	Session->Snapshot.NormalizedLineLoad = 0.83f;
	Session->Snapshot.bReeling = true;

	FCatFishingSessionCommandContext Context;
	Context.RequestId = FGuid::NewGuid();
	Context.FishingSessionId = Session->Snapshot.FishingSessionId;
	Context.CastAttemptId = Session->Snapshot.CastAttemptId;
	Context.ExpectedRevision = Session->Snapshot.Revision;
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCatFishingCommandResult First = Session->CutLineFromAuthority(Controller, Context);
	TestTrue(TEXT("Current fisher can commit cut line"), First.bCommitted);
	TestEqual(TEXT("Cut line returns its distinct command type"), First.CommandType, ECatFishingCommandType::CutLine);
	TestEqual(TEXT("Cut line terminates the session"), Session->Snapshot.Phase, ECatFishingPhase::Terminated);
	TestEqual(TEXT("Cut line persists its distinct outcome"), Session->Snapshot.Outcome, ECatFishingOutcome::LineCut);
	TestEqual(TEXT("Cut line preserves accumulated line durability without extra wear"),
		Session->Snapshot.RodDurabilityRemaining, 42.5);
	TestFalse(TEXT("Cut line clears stale reel input"), Session->Snapshot.bReeling);
	TestFalse(TEXT("Cut line does not break the reusable rod"), Rod->GetPresentationState().bBroken);

	const FCatFishingCommandResult Replay = Session->CutLineFromAuthority(Controller, Context);
	TestTrue(TEXT("Same request replays committed result"), Replay.bCommitted);
	TestEqual(TEXT("Replay returns the first terminal revision"), Replay.Revision, First.Revision);
	TestEqual(TEXT("Replay cannot advance terminal revision"), Session->Snapshot.Revision, First.Revision);
	return !HasAnyErrors();
}

bool FCatFishingSessionGroundedCutLineCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates grounded cut-line test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	if (!Session || !Rod || !Controller || !PlayerState || !Character)
	{
		AddError(TEXT("Grounded cut-line fixtures must spawn"));
		return false;
	}
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	Rod->SetActorLocation(Character->GetActorLocation() + FVector(100.0, 0.0, 0.0));
	TestTrue(TEXT("Initializes unattended grounded rod"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Rod_GroundedCut"), TEXT("Skin_GroundedCut"),
		PlayerState, nullptr, true, false));
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.CastAttemptId = FGuid::NewGuid();
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->Snapshot.Revision = 9;
	Session->Snapshot.RodActor = Rod;
	Session->Snapshot.FisherPlayerState = nullptr;
	Session->LastSuspendedFisherPlayerState = PlayerState;

	FCatFishingSessionCommandContext Context;
	Context.RequestId = FGuid::NewGuid();
	Context.FishingSessionId = Session->Snapshot.FishingSessionId;
	Context.CastAttemptId = Session->Snapshot.CastAttemptId;
	Context.ExpectedRevision = Session->Snapshot.Revision;
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCatFishingCommandResult Result = Session->CutLineFromAuthority(Controller, Context);
	TestTrue(TEXT("Nearby last holder can cut unattended grounded line"), Result.bCommitted);
	TestEqual(TEXT("Grounded cut keeps distinct terminal outcome"),
		Session->Snapshot.Outcome, ECatFishingOutcome::LineCut);
	TestEqual(TEXT("Cutting does not pick the rod up"), Rod->GetPresentationState().PoseMode,
		ECatFishingRodPoseMode::Grounded);
	return !HasAnyErrors();
}

bool FCatFishingSessionLineBreakKeepsRodOperableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates line-break test world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	APlayerState* Owner = World ? World->SpawnActor<APlayerState>() : nullptr;
	if (!TestNotNull(TEXT("Spawns line-break session"), Session)
		|| !TestNotNull(TEXT("Spawns reusable rod"), Rod)
		|| !TestNotNull(TEXT("Spawns rod owner"), Owner))
	{
		return false;
	}
	TestTrue(TEXT("Initializes deployed operable rod"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Rod_Test"), TEXT("Skin_Test"), Owner, Owner, true, false));
	const int64 RodRevisionBefore = Rod->GetPresentationState().RodActorRevision;
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->Snapshot.RodActor = Rod;
	Session->Snapshot.bReeling = true;
	Session->Snapshot.bSlacking = true;
	Session->FightRunner = NewObject<UCatFishingFightRunner>(Session);

	FCatFightStepResult Step;
	Step.Outcome = ECatFightStepOutcome::LineBroken;
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
	Session->HandleFightRunnerStepFromAuthority(Step, 50.0,
		ECatFishMotionIntent::StrugglingOutward, 0.0);

	TestEqual(TEXT("Line break terminates only the current session"),
		Session->Snapshot.Phase, ECatFishingPhase::Terminated);
	TestEqual(TEXT("Public outcome explicitly reports line break"),
		Session->Snapshot.Outcome, ECatFishingOutcome::LineBroken);
	TestTrue(TEXT("Rod remains deployed after line break"), Rod->GetPresentationState().bDeployed);
	TestFalse(TEXT("Line break never marks rod broken"), Rod->GetPresentationState().bBroken);
	TestEqual(TEXT("Line break does not mutate rod presentation revision"),
		Rod->GetPresentationState().RodActorRevision, RodRevisionBefore);
	TestEqual(TEXT("Current operator remains on the reusable rod"),
		Rod->GetPresentationState().OperatorPlayerState.Get(), Owner);
	TestFalse(TEXT("terminal line break clears stale reeling presentation"), Session->Snapshot.bReeling);
	TestFalse(TEXT("terminal line break clears stale slack presentation"), Session->Snapshot.bSlacking);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionLandedTerminalVisibilityTest,
	"Catfishing.Unit.Fishing.Session.LandedPickupHidesEncounterDuringTerminalReplicationWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionLandedTerminalVisibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("Creates landed terminal presentation world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishEncounterActor* Encounter = World ? World->SpawnActor<ACatFishEncounterActor>() : nullptr;
	if (!TestNotNull(TEXT("Spawns session"), Session)
		|| !TestNotNull(TEXT("Spawns encounter"), Encounter))
	{
		return false;
	}

	Encounter->SetActorHiddenInGame(false);
	Encounter->SetActorEnableCollision(true);
	Session->Snapshot.FishEncounterActor = Encounter;
	Session->FinalizeSession(ECatFishingPhase::Resolved, ECatFishingOutcome::Landed,
		TEXT("Automation landed handoff"));

	TestTrue(TEXT("Landed encounter becomes hidden immediately"), Encounter->IsHidden());
	TestFalse(TEXT("Landed encounter collision is disabled immediately"), Encounter->GetActorEnableCollision());
	TestFalse(TEXT("Encounter remains alive for terminal replication"), Encounter->IsActorBeingDestroyed());
	TestTrue(TEXT("Encounter has a bounded terminal lifespan"), Encounter->GetLifeSpan() > 0.0f);
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
	FCatFishingScoopFacingUsesCharacterTest,
	"Catfishing.Unit.Fishing.Session.ScoopFacingUsesCharacterInsteadOfController",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingScoopFacingUsesCharacterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建抄网朝向测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	if (!TestNotNull(TEXT("生成测试 Controller"), Controller)
		|| !TestNotNull(TEXT("生成测试 Character"), Character))
	{
		return false;
	}

	Controller->Possess(Character);
	Controller->SetControlRotation(FRotator(0.0, -90.0, 0.0));
	Character->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	const FVector Facing = UCatFishingAimLibrary::ResolveScoopFacingHorizontal(Character);
	TestTrue(TEXT("抄网水平朝向等于 Character Actor Forward"),
		Facing.Equals(FVector::YAxisVector, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("抄网水平朝向不跟随相反的 Controller/Camera Forward"),
		Facing.Equals(FVector(0.0, -1.0, 0.0), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("空 Character 返回退化朝向并安全拒绝"),
		UCatFishingAimLibrary::ResolveScoopFacingHorizontal(nullptr).IsNearlyZero());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionScoopMouthCarryTest,
	"Catfishing.Unit.Fishing.Session.ScoopFullStaminaFishDirectlyIntoMouthCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingSessionScoopMouthCarryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建满体力抄网嘴叼测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	ACatfishingPlayerState* PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishEncounterActor* Encounter = World ? World->SpawnActor<ACatFishEncounterActor>() : nullptr;
	UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
	if (!TestNotNull(TEXT("生成项目 Controller"), Controller)
		|| !TestNotNull(TEXT("生成项目 PlayerState"), PlayerState)
		|| !TestNotNull(TEXT("生成抄网角色"), Character)
		|| !TestNotNull(TEXT("生成钓鱼会话"), Session)
		|| !TestNotNull(TEXT("生成水中鱼"), Encounter)
		|| !TestNotNull(TEXT("创建鱼定义"), Definition))
	{
		return false;
	}

	Definition->bEnableRuntimeDefinition = true;
	Definition->FishDefinitionId = TEXT("FullStaminaScoopFish");
	Definition->BodyClass = ECatFishBodyClass::Standard;
	Definition->SacrificeContribution = 1;
	Definition->RarityTierId = TEXT("Common");
	Definition->RegionIds = {TEXT("LakeA")};
	Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
	Definition->Weather = {ECatEnvironmentWeather::Clear};
	Definition->SpawnWeight = 1.0;
	Definition->MinimumWeightKilograms = 0.5;
	Definition->MaximumWeightKilograms = 8.0;
	Definition->MinimumFightParticipants = 1;
	Definition->FishStrength = 1.0;
	Definition->FishFightStamina = 100.0;
	Definition->BitePersonalityId = TEXT("Nibble");
	Definition->FightPersonalityId = TEXT("Steady");
	Definition->FoodSafety = ECatFishFoodSafety::Safe;
	Definition->EatingExperience = 1.0;
	UCatFishPresentationDefinition* FishPresentation = LoadObject<UCatFishPresentationDefinition>(nullptr,
		TEXT("/Game/Catfishing/Data/Fish/Presentation/FishPresentation_RiverPattern.FishPresentation_RiverPattern"));
	if (!TestNotNull(TEXT("加载正式鱼表现资产作为抄网交接资源"), FishPresentation))
	{
		return false;
	}
	Definition->PresentationDefinition = FishPresentation;
	TestTrue(TEXT("测试鱼定义满足正式运行校验"), Definition->IsRuntimeDefinitionReady());

	const FString StableNetId(TEXT("ScoopMouthCarryPlayer"));
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId, FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	if (!Character->HasActorBegunPlay())
	{
		Character->DispatchBeginPlay();
	}

	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.FishDefinitionId = Definition->FishDefinitionId;
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->Snapshot.FishEncounterActor = Encounter;
	Session->Snapshot.FishFightStaminaRemaining = Definition->FishFightStamina;
	Session->Snapshot.NormalizedFishStamina = 1.0;
	Session->FishDefinition = Definition;
	Session->FishWeightKilograms = 2.5;
	Session->FishVisualScale = 1.0;
	Session->FisherStableNetId = TEXT("OriginalFisher");
	Session->FightParticipantIds.Add(Session->FisherStableNetId);
	Session->AttemptSnapshot.WaterRegion.RegionId = TEXT("LakeA");
	Session->AttemptSnapshot.WaterRegion.GeometryRevision = 1;

	TestTrue(TEXT("鱼满体力时抄网交接仍然成功"), Session->SpawnScoopedFishPickupFromAuthority(
		Character, PlayerState, StableNetId));
	ACatFishPickupActor* CarriedFish = ACatFishPickupActor::FindCarriedFish(Character);
	if (TestNotNull(TEXT("抄网成功后角色嘴上存在世界鱼"), CarriedFish))
	{
		TestEqual(TEXT("抄网鱼进入与 E 拾鱼相同的 Carried 状态"),
			CarriedFish->GetPresentationState().State, ECatFishPickupState::Carried);
		TestEqual(TEXT("嘴叼鱼保留来源会话"), CarriedFish->GetPresentationState().FishingSessionId,
			Session->Snapshot.FishingSessionId);
		TestEqual(TEXT("嘴叼鱼保留鱼种"), CarriedFish->GetPresentationState().FishDefinitionId,
			Definition->FishDefinitionId);
		TestEqual(TEXT("嘴叼鱼附着在抄手角色下"), CarriedFish->GetAttachParentActor(),
			static_cast<AActor*>(Character));
	}
	TestEqual(TEXT("满体力没有被当作抄网门槛或强制清零"), Session->Snapshot.FishFightStaminaRemaining, 100.0);
	TestTrue(TEXT("抄网交接关闭本会话"), Session->bCaptureResolved);
	TestEqual(TEXT("抄网交接进入 Resolved"), Session->Snapshot.Phase, ECatFishingPhase::Resolved);
	TestEqual(TEXT("抄网交接结果为 Caught"), Session->Snapshot.Outcome, ECatFishingOutcome::Caught);
	TestTrue(TEXT("水中的旧 Encounter 在嘴叼交接后立即销毁"), Encounter->IsActorBeingDestroyed());
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
