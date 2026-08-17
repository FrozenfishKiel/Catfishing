#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "GameFramework/PlayerController.h"
#include "Character/CatCharacter.h"
#include "Environment/CatChumPlacementService.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatGameplayTypes.h"
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

void UCatFishingCommandComponent::DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.RequestId.IsValid()) return;
	if (Controller->IsLocalController()) ReceivePlaceChumResultLocally(Result);
	else ClientReceivePlaceChumResult(Result);
}

void UCatFishingCommandComponent::SubmitPlaceChum(const FCatPlaceChumCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPlaceChum_Implementation(Command);
	else ServerSubmitPlaceChum(Command);
}

bool UCatFishingCommandComponent::TryGetPlaceChumResult(const FGuid RequestId,
	FCatPlaceChumResult& OutResult) const
{
	OutResult = FCatPlaceChumResult();
	if (!IsSupportedOwner() || !RequestId.IsValid()) return false;
	const FCatPlaceChumResult* Result = PlaceChumResultsByRequestId.Find(RequestId);
	if (!Result) return false;
	OutResult = *Result;
	return true;
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
	PlaceChumResultsByRequestId.Reset();
	PlaceChumResultOrder.Reset();
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
	DispatchAbilityCommand(ECatFishingCommandType::PlaceChum, Edge);
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

void UCatFishingCommandComponent::ServerSubmitPlaceChum_Implementation(const FCatPlaceChumCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Controller->CanForwardGameplayCommand()) return;
	UCatChumPlacementService* Service = GetWorld()
		? GetWorld()->GetSubsystem<UCatChumPlacementService>() : nullptr;
	FCatPlaceChumResult Result;
	Result.RequestId = Command.RequestId;
	if (Service) Result = Service->PlaceChum(Controller, Command);
	DeliverPlaceChumResultFromAuthority(Result);
}

void UCatFishingCommandComponent::ClientReceiveFishingCommandResult_Implementation(
	const FCatFishingCommandResult& Result)
{
	ReceiveResultLocally(Result);
}

void UCatFishingCommandComponent::ClientReceivePlaceChumResult_Implementation(
	const FCatPlaceChumResult& Result)
{
	ReceivePlaceChumResultLocally(Result);
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

void UCatFishingCommandComponent::ReceivePlaceChumResultLocally(const FCatPlaceChumResult& Result)
{
	if (!IsSupportedOwner() || !Result.RequestId.IsValid()
		|| PlaceChumResultsByRequestId.Contains(Result.RequestId)) return;
	PlaceChumResultsByRequestId.Add(Result.RequestId, Result);
	PlaceChumResultOrder.Add(Result.RequestId);
	if (PlaceChumResultOrder.Num() > MaxStoredResults)
	{
		const FGuid Evicted = PlaceChumResultOrder[0];
		PlaceChumResultOrder.RemoveAt(0);
		PlaceChumResultsByRequestId.Remove(Evicted);
	}
	FCatFishingCommandResult Common;
	Common.CommandType = ECatFishingCommandType::PlaceChum;
	Common.bCommitted = Result.bCommitted;
	Common.RequestId = Result.RequestId;
	Common.EquipmentRevision = Result.EquipmentRevision;
	Common.Revision = Result.ChumFieldSetRevision;
	switch (Result.Error)
	{
	case ECatChumFieldError::None: Common.Error = ECatFishingCommandError::None; break;
	case ECatChumFieldError::FeatureDisabled: Common.Error = ECatFishingCommandError::FeatureDisabled; break;
	case ECatChumFieldError::CommandsClosed: Common.Error = ECatFishingCommandError::CommandsClosed; break;
	case ECatChumFieldError::InvalidIdentity: Common.Error = ECatFishingCommandError::InvalidIdentity; break;
	case ECatChumFieldError::InvalidPayload: Common.Error = ECatFishingCommandError::InvalidPayload; break;
	case ECatChumFieldError::InvalidWaterTarget: Common.Error = ECatFishingCommandError::InvalidWaterTarget; break;
	case ECatChumFieldError::StaleGeometry: Common.Error = ECatFishingCommandError::RevisionConflict; break;
	case ECatChumFieldError::PlacementOutOfRange: Common.Error = ECatFishingCommandError::CastOutOfRange; break;
	case ECatChumFieldError::EquipmentRevisionConflict: Common.Error = ECatFishingCommandError::EquipmentRevisionConflict; break;
	case ECatChumFieldError::AlreadyResolved: Common.Error = ECatFishingCommandError::AlreadyResolved; break;
	default: Common.Error = ECatFishingCommandError::DependencyUnavailable; break;
	}
	ReceiveResultLocally(Common);
}
