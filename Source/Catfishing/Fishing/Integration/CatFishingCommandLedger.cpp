#include "Fishing/Integration/CatFishingCommandLedger.h"

namespace
{
	bool HasValidReelingIdentity(const FCatSetReelingCommand& Command)
	{
		return Command.RequestId.IsValid()
			&& Command.FishingSessionId.IsValid()
			&& Command.CastAttemptId.IsValid()
			&& Command.ActivationCorrelationId.IsValid();
	}
}

bool FCatFishingCommandLedger::TryGetTerminalResult(const FString& AuthorityIdentity, const FGuid RequestId,
	FCatFishingCommandResult& OutResult) const
{
	if (AuthorityIdentity.IsEmpty() || !RequestId.IsValid())
	{
		return false;
	}

	const FCatFishingCommandResult* Existing = TerminalResults.Find(MakeTerminalKey(AuthorityIdentity, RequestId));
	if (Existing == nullptr)
	{
		return false;
	}

	OutResult = *Existing;
	return true;
}

bool FCatFishingCommandLedger::RecordTerminalResult(const FString& AuthorityIdentity,
	const FCatFishingCommandResult& Result)
{
	if (AuthorityIdentity.IsEmpty() || !Result.RequestId.IsValid())
	{
		return false;
	}

	const FString Key = MakeTerminalKey(AuthorityIdentity, Result.RequestId);
	if (TerminalResults.Contains(Key))
	{
		return false;
	}

	TerminalResults.Add(Key, Result);
	return true;
}

ECatFishingCommandError FCatFishingCommandLedger::ValidateDiscrete(const FCatFishingSessionCommandContext& Command,
	const FGuid CurrentFishingSessionId, const int64 CurrentRevision, const FGuid CurrentCastAttemptId) const
{
	if (!Command.RequestId.IsValid() || !Command.FishingSessionId.IsValid() || !Command.CastAttemptId.IsValid())
	{
		return ECatFishingCommandError::InvalidPayload;
	}
	if (Command.FishingSessionId != CurrentFishingSessionId)
	{
		return ECatFishingCommandError::SessionNotFound;
	}
	if (Command.CastAttemptId != CurrentCastAttemptId)
	{
		return ECatFishingCommandError::CastAttemptConflict;
	}
	if (Command.ExpectedRevision != CurrentRevision)
	{
		return ECatFishingCommandError::RevisionConflict;
	}

	return ECatFishingCommandError::None;
}

ECatFishingCommandError FCatFishingCommandLedger::ValidateReeling(const FString& AuthorityIdentity,
	const FCatSetReelingCommand& Command, const FGuid CurrentFishingSessionId, const FGuid CurrentCastAttemptId,
	const int64 MaximumInputSequenceAdvance) const
{
	if (AuthorityIdentity.IsEmpty())
	{
		return ECatFishingCommandError::InvalidIdentity;
	}
	if (!HasValidReelingIdentity(Command) || Command.InputSequence <= 0 || MaximumInputSequenceAdvance <= 0)
	{
		return ECatFishingCommandError::InvalidPayload;
	}
	if (Command.FishingSessionId != CurrentFishingSessionId)
	{
		return ECatFishingCommandError::SessionNotFound;
	}
	if (Command.CastAttemptId != CurrentCastAttemptId)
	{
		return ECatFishingCommandError::CastAttemptConflict;
	}

	const FString Key = MakeReelingKey(AuthorityIdentity, Command);
	const int64 LastSequence = ReelingSequences.FindRef(Key);
	if (Command.InputSequence <= LastSequence)
	{
		return ECatFishingCommandError::InputSequenceStale;
	}
	if (Command.InputSequence - LastSequence > MaximumInputSequenceAdvance)
	{
		return ECatFishingCommandError::InputSequenceGapTooLarge;
	}

	return ECatFishingCommandError::None;
}

void FCatFishingCommandLedger::CommitReelingSequence(const FString& AuthorityIdentity,
	const FCatSetReelingCommand& Command)
{
	if (AuthorityIdentity.IsEmpty() || !HasValidReelingIdentity(Command) || Command.InputSequence <= 0)
	{
		return;
	}

	const FString Key = MakeReelingKey(AuthorityIdentity, Command);
	int64& LastSequence = ReelingSequences.FindOrAdd(Key);
	if (Command.InputSequence > LastSequence)
	{
		LastSequence = Command.InputSequence;
	}
}

void FCatFishingCommandLedger::Reset()
{
	TerminalResults.Reset();
	ReelingSequences.Reset();
}

FString FCatFishingCommandLedger::MakeTerminalKey(const FString& AuthorityIdentity, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), *AuthorityIdentity, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

FString FCatFishingCommandLedger::MakeReelingKey(const FString& AuthorityIdentity, const FCatSetReelingCommand& Command)
{
	return FString::Printf(TEXT("%s|%s|%s|%s"), *AuthorityIdentity,
		*Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.ActivationCorrelationId.ToString(EGuidFormats::DigitsWithHyphens));
}
