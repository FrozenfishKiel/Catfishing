#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "GameFramework/PlayerController.h"
#include "Character/CatCharacter.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingService.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerState.h"

UCatFishingCommandComponent::UCatFishingCommandComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UCatFishingCommandComponent::DeliverResultFromAuthority(const FCatFishingCommandResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.RequestId.IsValid())
	{
		return;
	}

	if (Controller->IsLocalController())
	{
		ReceiveResultLocally(Result);
	}
	else
	{
		ClientReceiveFishingCommandResult(Result);
	}
}

bool UCatFishingCommandComponent::TryGetResult(const FGuid RequestId,
	FCatFishingCommandResult& OutResult) const
{
	OutResult = FCatFishingCommandResult();
	if (!IsSupportedOwner() || !RequestId.IsValid())
	{
		return false;
	}

	const FCatFishingCommandResult* Result = ResultsByRequestId.Find(RequestId);
	if (!Result)
	{
		return false;
	}

	OutResult = *Result;
	return true;
}

void UCatFishingCommandComponent::ConsumeResult(const FGuid RequestId)
{
	if (!IsSupportedOwner() || !RequestId.IsValid())
	{
		return;
	}

	if (ResultsByRequestId.Remove(RequestId) > 0)
	{
		ResultOrder.RemoveSingle(RequestId);
	}
}

void UCatFishingCommandComponent::ResetTransientCommandState()
{
	if (!IsSupportedOwner())
	{
		return;
	}

	ResultsByRequestId.Reset();
	ResultOrder.Reset();
	PrimaryActivationCorrelationId.Invalidate();
	NextInputSequence = 0;
}

FCatFishingInputEdge UCatFishingCommandComponent::MakeDiscreteEdge()
{
	FCatFishingInputEdge Edge;
	Edge.RequestId = FGuid::NewGuid();
	Edge.InputSequence = ++NextInputSequence;
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitRodInteract()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::OperateRod, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitPrimaryPressed()
{
	PrimaryActivationCorrelationId = FGuid::NewGuid();
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	Edge.ActivationCorrelationId = PrimaryActivationCorrelationId;
	DispatchAbilityCommand(ECatFishingCommandType::RequestHook, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitPrimaryReleased()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	Edge.ActivationCorrelationId = PrimaryActivationCorrelationId;
	DispatchAbilityCommand(ECatFishingCommandType::PrimaryReleased, Edge);
	PrimaryActivationCorrelationId.Invalidate();
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitCancel()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::CancelFishing, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitScoop()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::RequestScoop, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitChum()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::ContributeChum, Edge);
	return Edge;
}

void UCatFishingCommandComponent::DispatchAbilityCommand(const ECatFishingCommandType CommandType,
	const FCatFishingInputEdge& Edge)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !GetWorld() || !Edge.RequestId.IsValid())
	{
		return;
	}
	if (Controller->HasAuthority())
	{
		HandleAbilityCommandFromAuthority(CommandType, Edge);
	}
	else
	{
		ServerSubmitFishingAbilityCommand(CommandType, Edge);
	}
}

void UCatFishingCommandComponent::ServerSubmitFishingAbilityCommand_Implementation(
	const ECatFishingCommandType CommandType, const FCatFishingInputEdge Edge)
{
	HandleAbilityCommandFromAuthority(CommandType, Edge);
}

void UCatFishingCommandComponent::HandleAbilityCommandFromAuthority(const ECatFishingCommandType CommandType,
	const FCatFishingInputEdge& Edge)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Edge.RequestId.IsValid())
	{
		return;
	}
	FCatFishingCommandResult Result;
	Result.CommandType = CommandType;
	Result.RequestId = Edge.RequestId;
	Result.bCommitted = false;
	// Stage B establishes the single command edge. Payload/session/rod resolution lands in C/D;
	// until then every input command has an explicit terminal refusal and never fabricates success.
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	DeliverResultFromAuthority(Result);
}

void UCatFishingCommandComponent::ForwardLegacyStart(const FGuid RequestId)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->HasAuthority())
	{
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->StartFishingSession(Controller, RequestId);
		}
	}
}

void UCatFishingCommandComponent::ForwardLegacyAssist(const FGuid FishingSessionId, const FGuid RequestId,
	const int64 ExpectedRevision)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->HasAuthority())
	{
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->SubmitFightAssist(FishingSessionId, Controller, RequestId, ExpectedRevision);
		}
	}
}

void UCatFishingCommandComponent::ForwardLegacyScoop(const FGuid FishingSessionId, FCatScoopCommand Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
	Command.TargetGuardContainerId = Character ? Character->GetPersonalFishGuardId() : FGuid();
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->RequestScoop(FishingSessionId, Controller, Command);
	}
}

FCatAggregationResult UCatFishingCommandComponent::MakeInvalidChumResult(const FGuid RequestId,
	const ACatWaterRegion* WaterRegion)
{
	FCatAggregationResult Result;
	Result.Command.RequestId = RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	if (WaterRegion)
	{
		Result.AggregationRevision = WaterRegion->MakeSnapshot().AggregationRevision;
		Result.Command.Revision = Result.AggregationRevision;
	}
	return Result;
}

void UCatFishingCommandComponent::ForwardLegacyChum(ACatWaterRegion* WaterRegion, const FGuid RequestId,
	const int64 ExpectedEquipmentRevision, const int64 ExpectedAggregationRevision,
	const FName ChumDefinitionId)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority())
	{
		return;
	}
	if (const FCatAggregationResult* Cached = ChumTerminalCache.Find(RequestId))
	{
		UE_LOG(LogCatItems, Log, TEXT("Event=player_chum_replayed RequestId=%s Error=%s Revision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Cached->Command.Error),
			Cached->Command.Revision);
		return;
	}
	FCatAggregationResult Result;
	Result.Command.RequestId = RequestId;
	ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const APlayerState* PlayerState = Controller->PlayerState;
	UCatEquipmentDefinition* ChumDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(ChumDefinitionId);
	if (!RequestId.IsValid() || !WaterRegion || !Character || !Equipment || !PlayerState
		|| !PlayerState->GetUniqueId().IsValid() || !ChumDefinition
		|| ChumDefinition->Kind != ECatEquipmentKind::Chum || !ChumDefinition->ChumContribution.IsValidContribution()
		|| !WaterRegion->ContainsWorldPoint(Character->GetActorLocation()))
	{
		Result = MakeInvalidChumResult(RequestId, WaterRegion);
	}
	else
	{
		FCatAggregationCommand Command;
		Command.Context.RequestId = RequestId;
		Command.ExpectedAggregationRevision = ExpectedAggregationRevision;
		Command.Context.StableNetId = PlayerState->GetUniqueId()->ToString();
		Command.RegionId = WaterRegion->RegionId;
		Command.Contribution = ChumDefinition->ChumContribution;
		Command.Source = ECatAggregationSource::PlayerChum;
		Result.Command.Error = WaterRegion->ValidateAggregation(Command);
		Result.AggregationRevision = WaterRegion->MakeSnapshot().AggregationRevision;
		Result.Command.Revision = Result.AggregationRevision;
		if (Result.Command.Error == ECatDomainCommandError::None)
		{
			const FCatDomainCommandResult ConsumeResult = Equipment->ConsumeRunConsumableFromAuthority(
				RequestId, ExpectedEquipmentRevision, ChumDefinitionId);
			if (!ConsumeResult.bCommitted)
			{
				Result.Command.Error = ConsumeResult.Error;
				Result.AggregationRevision = WaterRegion->MakeSnapshot().AggregationRevision;
				Result.Command.Revision = Result.AggregationRevision;
			}
			else
			{
				Result = WaterRegion->ContributeAggregation(Command);
			}
		}
	}
	ChumTerminalCache.Add(RequestId, Result);
	UE_LOG(LogCatItems, Log, TEXT("Event=player_chum_terminal RequestId=%s Definition=%s Region=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *ChumDefinitionId.ToString(),
		WaterRegion ? *WaterRegion->RegionId.ToString() : TEXT("None"), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
}

void UCatFishingCommandComponent::ClientReceiveFishingCommandResult_Implementation(
	const FCatFishingCommandResult& Result)
{
	ReceiveResultLocally(Result);
}

bool UCatFishingCommandComponent::IsSupportedOwner() const
{
	return Cast<APlayerController>(GetOwner()) != nullptr;
}

void UCatFishingCommandComponent::ReceiveResultLocally(const FCatFishingCommandResult& Result)
{
	if (!IsSupportedOwner() || !Result.RequestId.IsValid()
		|| ResultsByRequestId.Contains(Result.RequestId))
	{
		return;
	}

	ResultsByRequestId.Add(Result.RequestId, Result);
	ResultOrder.Add(Result.RequestId);
	if (ResultOrder.Num() > MaxStoredResults)
	{
		const FGuid EvictedRequestId = ResultOrder[0];
		ResultOrder.RemoveAt(0);
		ResultsByRequestId.Remove(EvictedRequestId);
	}

	OnResultReceived.Broadcast(Result);
}
