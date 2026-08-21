#include "Equipment/CatTeamEquipmentLibrary.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"

bool UCatTeamEquipmentLibrary::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UCatTeamEquipmentLibrary::Deinitialize()
{
	CloseCommands();
	Snapshot = FCatTeamEquipmentLibrarySnapshot();
	InstanceIdBySourceTransaction.Reset();
	TakenInstanceById.Reset();
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	Super::Deinitialize();
}

const FCatTeamEquipmentLibrarySnapshot& UCatTeamEquipmentLibrary::GetSnapshot() const
{
	return Snapshot;
}

ECatDomainCommandError UCatTeamEquipmentLibrary::ValidateShopOrderGrant(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!bCommandsOpen)
	{
		return ECatDomainCommandError::CommandsClosed;
	}
	if (!GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId))
	{
		return ECatDomainCommandError::DependencyUnavailable;
	}
	return ECatDomainCommandError::None;
}

FCatTeamEquipmentGrantResult UCatTeamEquipmentLibrary::GrantFromShopOrder(
	const FCatTeamEquipmentGrantCommand& Command)
{
	FCatTeamEquipmentGrantResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Revision = Snapshot.Revision;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.SourceTransactionId.IsValid() || Command.DefinitionId.IsNone())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId,
		TEXT("TeamEquipmentGrant"), Command.Context.RequestId);
	const FString PayloadSignature = MakePayloadSignature(Command);
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature,
		Result, [](FCatTeamEquipmentGrantResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}

	const auto CacheAndReturn = [this, &TerminalKey, &PayloadSignature](FCatTeamEquipmentGrantResult& Terminal)
	{
		Terminal.Command.Revision = Snapshot.Revision;
		TerminalCache.Add(TerminalKey, Terminal);
		TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
		return Terminal;
	};

	const ECatDomainCommandError GrantRejection = ValidateShopOrderGrant(Command.DefinitionId);
	const UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Command.DefinitionId);
	if (GrantRejection != ECatDomainCommandError::None || !Definition)
	{
		Result.Command.Error = GrantRejection != ECatDomainCommandError::None
			? GrantRejection : ECatDomainCommandError::DependencyUnavailable;
		return CacheAndReturn(Result);
	}
	if (TryFindInstanceBySourceTransaction(Command.SourceTransactionId, Result.Instance))
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return CacheAndReturn(Result);
	}
	if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
		return CacheAndReturn(Result);
	}

	FCatTeamEquipmentInstance& Instance = Snapshot.Instances.AddDefaulted_GetRef();
	Instance.InstanceId = FGuid::NewGuid();
	Instance.DefinitionId = Command.DefinitionId;
	Instance.Kind = Definition->Kind;
	Instance.SourceTransactionId = Command.SourceTransactionId;
	++Snapshot.Revision;
	InstanceIdBySourceTransaction.Add(Command.SourceTransactionId, Instance.InstanceId);
	Result.Instance = Instance;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	CacheAndReturn(Result);
	OnLibraryChanged.Broadcast();
	return Result;
}

ECatDomainCommandError UCatTeamEquipmentLibrary::EvaluateTakeAdmission(
	const FCatTeamEquipmentTakeCommand& Command, int32& OutInstanceIndex) const
{
	OutInstanceIndex = INDEX_NONE;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.InstanceId.IsValid())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!bCommandsOpen)
	{
		return ECatDomainCommandError::CommandsClosed;
	}
	const int32 InstanceIndex = Snapshot.Instances.IndexOfByPredicate(
		[&Command](const FCatTeamEquipmentInstance& Candidate)
		{
			return Candidate.InstanceId == Command.InstanceId;
		});
	if (InstanceIndex == INDEX_NONE)
	{
		return ECatDomainCommandError::NotFound;
	}
	if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	OutInstanceIndex = InstanceIndex;
	return ECatDomainCommandError::None;
}

ECatDomainCommandError UCatTeamEquipmentLibrary::ValidateTake(
	const FCatTeamEquipmentTakeCommand& Command, FCatTeamEquipmentInstance& OutInstance) const
{
	OutInstance = FCatTeamEquipmentInstance();
	int32 InstanceIndex = INDEX_NONE;
	const ECatDomainCommandError Admission = EvaluateTakeAdmission(Command, InstanceIndex);
	if (Admission == ECatDomainCommandError::None)
	{
		OutInstance = Snapshot.Instances[InstanceIndex];
	}
	return Admission;
}

FCatTeamEquipmentGrantResult UCatTeamEquipmentLibrary::TakeInstance(
	const FCatTeamEquipmentTakeCommand& Command)
{
	FCatTeamEquipmentGrantResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Revision = Snapshot.Revision;
	int32 InstanceIndex = INDEX_NONE;
	const ECatDomainCommandError Admission = EvaluateTakeAdmission(Command, InstanceIndex);
	if (Admission == ECatDomainCommandError::InvalidPayload)
	{
		Result.Command.Error = Admission;
		return Result;
	}

	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId,
		TEXT("TeamEquipmentTake"), Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Expected=%lld|Instance=%s"),
		Command.Context.ExpectedRevision, *Command.InstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature,
		Result, [](FCatTeamEquipmentGrantResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}

	const auto CacheAndReturn = [this, &TerminalKey, &PayloadSignature](FCatTeamEquipmentGrantResult& Terminal)
	{
		Terminal.Command.Revision = Snapshot.Revision;
		TerminalCache.Add(TerminalKey, Terminal);
		TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
		return Terminal;
	};
	if (Admission != ECatDomainCommandError::None)
	{
		Result.Command.Error = Admission;
		return CacheAndReturn(Result);
	}

	Result.Instance = Snapshot.Instances[InstanceIndex];
	Snapshot.Instances.RemoveAt(InstanceIndex);
	TakenInstanceById.Add(Result.Instance.InstanceId, Result.Instance);
	++Snapshot.Revision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	CacheAndReturn(Result);
	OnLibraryChanged.Broadcast();
	return Result;
}

bool UCatTeamEquipmentLibrary::TryFindInstanceBySourceTransaction(const FGuid SourceTransactionId,
	FCatTeamEquipmentInstance& OutInstance) const
{
	OutInstance = FCatTeamEquipmentInstance();
	const FGuid* InstanceId = InstanceIdBySourceTransaction.Find(SourceTransactionId);
	if (!InstanceId)
	{
		return false;
	}
	const FCatTeamEquipmentInstance* Instance = Snapshot.Instances.FindByPredicate(
		[InstanceId](const FCatTeamEquipmentInstance& Candidate)
		{
			return Candidate.InstanceId == *InstanceId;
		});
	if (!Instance)
	{
		Instance = TakenInstanceById.Find(*InstanceId);
	}
	if (!Instance)
	{
		return false;
	}
	OutInstance = *Instance;
	return true;
}

void UCatTeamEquipmentLibrary::CloseCommands()
{
	bCommandsOpen = false;
}

FString UCatTeamEquipmentLibrary::MakeTerminalKey(const FString& StableNetId,
	const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

FString UCatTeamEquipmentLibrary::MakePayloadSignature(const FCatTeamEquipmentGrantCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Transaction=%s|Definition=%s"),
		Command.Context.ExpectedRevision,
		*Command.SourceTransactionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.DefinitionId.ToString());
}
