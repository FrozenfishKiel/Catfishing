#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "GameFramework/PlayerController.h"
#include "Character/CatCharacter.h"
#include "Environment/CatChumPlacementService.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
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

void UCatFishingCommandComponent::DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.Command.RequestId.IsValid()) return;
	if (Controller->IsLocalController()) ReceiveBeginCastResultLocally(Result);
	else ClientReceiveBeginCastResult(Result);
}

void UCatFishingCommandComponent::SubmitBeginCast(const FCatBeginCastCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitBeginCast_Implementation(Command);
	else ServerSubmitBeginCast(Command);
}

void UCatFishingCommandComponent::SubmitPlaceRod(const FCatPlaceRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPlaceRod_Implementation(Command); else ServerSubmitPlaceRod(Command);
}

void UCatFishingCommandComponent::SubmitOperateRod(const FCatOperateRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitOperateRod_Implementation(Command); else ServerSubmitOperateRod(Command);
}

void UCatFishingCommandComponent::SubmitLeaveRod(const FCatLeaveRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitLeaveRod_Implementation(Command); else ServerSubmitLeaveRod(Command);
}

void UCatFishingCommandComponent::SubmitPackRod(const FCatPackRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPackRod_Implementation(Command); else ServerSubmitPackRod(Command);
}

bool UCatFishingCommandComponent::TryGetBeginCastResult(const FGuid RequestId, FCatBeginCastResult& OutResult) const
{
	OutResult = FCatBeginCastResult{};
	if (!IsSupportedOwner() || !RequestId.IsValid()) return false;
	const FCatBeginCastResult* Found = BeginCastResultsByRequestId.Find(RequestId);
	if (!Found) return false;
	OutResult = *Found;
	return true;
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
	BeginCastResultsByRequestId.Reset();
	BeginCastResultOrder.Reset();
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
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		FGuid SessionId;
		FCatFishingSessionSnapshot Snapshot;
		if (Fishing->TryGetActiveSessionForController(Controller, SessionId, Snapshot))
		{
			if (ACatFishingSession* Session = Fishing->FindSession(SessionId))
			{
				if (CommandType == ECatFishingCommandType::RequestHook)
				{
					DeliverResultFromAuthority(Session->RequestHookFromAuthority(Edge.RequestId));
					return;
				}
				if (CommandType == ECatFishingCommandType::CancelFishing)
				{
					DeliverResultFromAuthority(Session->CancelFromAuthority(Edge.RequestId));
					return;
				}
			}
		}
	}
	// Stage B establishes the single command edge. Payload/session/rod resolution lands in C/D;
	// until then every input command has an explicit terminal refusal and never fabricates success.
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	DeliverResultFromAuthority(Result);
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

void UCatFishingCommandComponent::ServerSubmitBeginCast_Implementation(const FCatBeginCastCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Controller->CanForwardGameplayCommand()) return;
	FCatBeginCastResult Result;
	Result.Command.CommandType = ECatFishingCommandType::BeginCast;
	Result.Command.RequestId = Command.RequestId;
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Result = Fishing->BeginCast(Controller, Command);
	}
	DeliverBeginCastResultFromAuthority(Result);
}

void UCatFishingCommandComponent::ServerSubmitPlaceRod_Implementation(const FCatPlaceRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->PlaceRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitOperateRod_Implementation(const FCatOperateRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->OperateRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitLeaveRod_Implementation(const FCatLeaveRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->LeaveRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitPackRod_Implementation(const FCatPackRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->PackRod(Controller, Command));
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

void UCatFishingCommandComponent::ClientReceiveBeginCastResult_Implementation(const FCatBeginCastResult& Result)
{
	ReceiveBeginCastResultLocally(Result);
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

void UCatFishingCommandComponent::ReceiveBeginCastResultLocally(const FCatBeginCastResult& Result)
{
	const FGuid RequestId = Result.Command.RequestId;
	if (!IsSupportedOwner() || !RequestId.IsValid() || BeginCastResultsByRequestId.Contains(RequestId)) return;
	BeginCastResultsByRequestId.Add(RequestId, Result);
	BeginCastResultOrder.Add(RequestId);
	if (BeginCastResultOrder.Num() > MaxStoredResults)
	{
		const FGuid Evicted = BeginCastResultOrder[0];
		BeginCastResultOrder.RemoveAt(0);
		BeginCastResultsByRequestId.Remove(Evicted);
	}
	ReceiveResultLocally(Result.Command);
}
