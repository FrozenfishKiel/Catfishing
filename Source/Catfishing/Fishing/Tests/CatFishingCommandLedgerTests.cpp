#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/Integration/CatFishingCommandLedger.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandLedgerFirstTerminalResultTest,
	"Catfishing.Unit.Fishing.CommandLedger.FirstTerminalResultWinsAndReplayIsSideEffectFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Catches a duplicate terminal command replacing the original authoritative result or accepting invalid cache keys.
bool FCatFishingCommandLedgerFirstTerminalResultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingCommandLedger Ledger;
	const FString AuthorityIdentity = TEXT("AuthorityA");
	const FGuid RequestId = FGuid::NewGuid();
	FCatFishingCommandResult First;
	First.RequestId = RequestId;
	First.bCommitted = true;
	First.Error = ECatFishingCommandError::None;
	First.Revision = 41;
	TestTrue(TEXT("First terminal result is recorded"), Ledger.RecordTerminalResult(AuthorityIdentity, First));

	FCatFishingCommandResult Duplicate = First;
	Duplicate.bCommitted = false;
	Duplicate.Error = ECatFishingCommandError::RevisionConflict;
	Duplicate.Revision = 42;
	TestFalse(TEXT("Duplicate terminal result cannot replace first result"), Ledger.RecordTerminalResult(AuthorityIdentity, Duplicate));

	FCatFishingCommandResult Replay;
	TestTrue(TEXT("Recorded result can be replayed"), Ledger.TryGetTerminalResult(AuthorityIdentity, RequestId, Replay));
	TestTrue(TEXT("Replay preserves first committed value"), Replay.bCommitted);
	TestEqual(TEXT("Replay preserves first error"), Replay.Error, ECatFishingCommandError::None);
	TestEqual(TEXT("Replay preserves first revision"), Replay.Revision, int64{ 41 });

	FCatFishingCommandResult InvalidRequest = First;
	InvalidRequest.RequestId.Invalidate();
	TestFalse(TEXT("Invalid request id is not recorded"), Ledger.RecordTerminalResult(AuthorityIdentity, InvalidRequest));
	TestFalse(TEXT("Empty identity is not recorded"), Ledger.RecordTerminalResult(TEXT(""), First));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandLedgerDiscreteValidationTest,
	"Catfishing.Unit.Fishing.CommandLedger.StaleRevisionAndAttemptAreRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Catches accepting a command for a stale cast attempt or revision, including malformed discrete identities.
bool FCatFishingCommandLedgerDiscreteValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingCommandLedger Ledger;
	const FGuid SessionId = FGuid::NewGuid();
	const FGuid AttemptId = FGuid::NewGuid();
	FCatFishingSessionCommandContext Command;
	Command.RequestId = FGuid::NewGuid();
	Command.FishingSessionId = SessionId;
	Command.CastAttemptId = AttemptId;
	Command.ExpectedRevision = 17;
	TestEqual(TEXT("Matching discrete command is accepted"), Ledger.ValidateDiscrete(Command, SessionId, 17, AttemptId), ECatFishingCommandError::None);

	Command.ExpectedRevision = 16;
	TestEqual(TEXT("Stale revision is rejected"), Ledger.ValidateDiscrete(Command, SessionId, 17, AttemptId), ECatFishingCommandError::RevisionConflict);
	Command.ExpectedRevision = 17;
	TestEqual(TEXT("Stale cast attempt is rejected"), Ledger.ValidateDiscrete(Command, SessionId, 17, FGuid::NewGuid()), ECatFishingCommandError::CastAttemptConflict);
	Command.RequestId.Invalidate();
	TestEqual(TEXT("Invalid request id is rejected before other comparisons"), Ledger.ValidateDiscrete(Command, SessionId, 17, AttemptId), ECatFishingCommandError::InvalidPayload);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandLedgerReelingSequenceTest,
	"Catfishing.Unit.Fishing.CommandLedger.ReelingSequenceIsMonotonicBoundedAndRevisionIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Catches duplicate, backwards, or oversized reeling inputs advancing the sequence ledger.
bool FCatFishingCommandLedgerReelingSequenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingCommandLedger Ledger;
	const FString AuthorityIdentity = TEXT("AuthorityA");
	const FGuid SessionId = FGuid::NewGuid();
	const FGuid AttemptId = FGuid::NewGuid();
	FCatSetReelingCommand Command;
	Command.RequestId = FGuid::NewGuid();
	Command.FishingSessionId = SessionId;
	Command.CastAttemptId = AttemptId;
	Command.ActivationCorrelationId = FGuid::NewGuid();

	Command.InputSequence = 1;
	TestEqual(TEXT("Sequence one is accepted"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::None);
	Ledger.CommitReelingSequence(AuthorityIdentity, Command);
	TestEqual(TEXT("Duplicate sequence one is rejected"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::InputSequenceStale);
	Command.InputSequence = 0;
	TestEqual(TEXT("Non-positive sequence is rejected"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::InvalidPayload);
	Command.InputSequence = 1025;
	TestEqual(TEXT("Maximum allowed advance is accepted"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::None);
	Ledger.CommitReelingSequence(AuthorityIdentity, Command);
	Command.InputSequence = 2050;
	TestEqual(TEXT("Oversized advance is rejected"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::InputSequenceGapTooLarge);
	Command.InputSequence = 1026;
	TestEqual(TEXT("Rejected oversized advance does not pollute ledger"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::None);
	Ledger.CommitReelingSequence(AuthorityIdentity, Command);

	Command.InputSequence = 1027;
	Command.RequestId.Invalidate();
	TestEqual(TEXT("Invalid reeling request id is rejected"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::InvalidPayload);
	Command.RequestId = FGuid::NewGuid();
	TestEqual(TEXT("Rejected invalid request does not pollute ledger"), Ledger.ValidateReeling(AuthorityIdentity, Command, SessionId, AttemptId), ECatFishingCommandError::None);
	TestEqual(TEXT("Empty reeling identity is rejected"), Ledger.ValidateReeling(TEXT(""), Command, SessionId, AttemptId), ECatFishingCommandError::InvalidIdentity);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
