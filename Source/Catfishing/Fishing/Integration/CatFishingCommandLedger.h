#pragma once

#include "CoreMinimal.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"

class FCatFishingCommandLedger
{
public:
	bool TryGetTerminalResult(const FString& AuthorityIdentity, FGuid RequestId,
		FCatFishingCommandResult& OutResult) const;
	bool RecordTerminalResult(const FString& AuthorityIdentity,
		const FCatFishingCommandResult& Result);
	ECatFishingCommandError ValidateDiscrete(
		const FCatFishingSessionCommandContext& Command,
		FGuid CurrentFishingSessionId,
		int64 CurrentRevision,
		FGuid CurrentCastAttemptId) const;
	ECatFishingCommandError ValidateReeling(
		const FString& AuthorityIdentity,
		const FCatSetReelingCommand& Command,
		FGuid CurrentFishingSessionId,
		FGuid CurrentCastAttemptId,
		int64 MaximumInputSequenceAdvance = 1024) const;
	void CommitReelingSequence(const FString& AuthorityIdentity,
		const FCatSetReelingCommand& Command);
	void Reset();

private:
	static FString MakeTerminalKey(const FString& AuthorityIdentity, FGuid RequestId);
	static FString MakeReelingKey(const FString& AuthorityIdentity, const FCatSetReelingCommand& Command);

	TMap<FString, FCatFishingCommandResult> TerminalResults;
	TMap<FString, int64> ReelingSequences;
};
