#include "Fishing/Integration/CatFishingCommandTypes.h"

ECatFishingCommandError MapDomainCommandError(const ECatDomainCommandError Error)
{
	switch (Error)
	{
	case ECatDomainCommandError::None: return ECatFishingCommandError::None;
	case ECatDomainCommandError::InvalidPayload: return ECatFishingCommandError::InvalidPayload;
	case ECatDomainCommandError::InvalidIdentity: return ECatFishingCommandError::InvalidIdentity;
	case ECatDomainCommandError::InvalidPhase: return ECatFishingCommandError::InvalidPhase;
	case ECatDomainCommandError::NotFound: return ECatFishingCommandError::SessionNotFound;
	case ECatDomainCommandError::RevisionConflict: return ECatFishingCommandError::RevisionConflict;
	case ECatDomainCommandError::AlreadyResolved: return ECatFishingCommandError::AlreadyResolved;
	case ECatDomainCommandError::CommandsClosed: return ECatFishingCommandError::CommandsClosed;
	default: return ECatFishingCommandError::DependencyUnavailable;
	}
}

FCatFishingCommandResult MakeFishingCommandResult(const FCatFishingStartResult& LegacyResult)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::BeginCast;
	Result.bCommitted = LegacyResult.bStarted;
	Result.Error = MapDomainCommandError(LegacyResult.Error);
	Result.RequestId = LegacyResult.RequestId;
	Result.FishingSessionId = LegacyResult.FishingSessionId;
	return Result;
}

FCatFishingSessionCommandContext MakeFishingSessionCommandContext(const FGuid FishingSessionId,
	const FCatScoopCommand& LegacyCommand)
{
	FCatFishingSessionCommandContext Context;
	Context.RequestId = LegacyCommand.Context.RequestId;
	Context.FishingSessionId = FishingSessionId;
	Context.ExpectedRevision = LegacyCommand.Context.ExpectedRevision;
	return Context;
}
