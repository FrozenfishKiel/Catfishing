#include "Environment/CatChumPlacementService.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace CatChumPlacementServicePrivate
{
	static FCatPlaceChumResult MakeError(const FGuid RequestId, const ECatChumFieldError Error)
	{
		FCatPlaceChumResult Result;
		Result.RequestId = RequestId;
		Result.Error = Error;
		return Result;
	}

	static ECatChumFieldError MapWaterError(const ECatWaterQueryError Error)
	{
		return Error == ECatWaterQueryError::StaleGeometry
			? ECatChumFieldError::StaleGeometry : ECatChumFieldError::InvalidWaterTarget;
	}

	static ECatChumFieldError MapEquipmentError(const ECatDomainCommandError Error)
	{
		return Error == ECatDomainCommandError::RevisionConflict
			? ECatChumFieldError::EquipmentRevisionConflict : ECatChumFieldError::EquipmentUnavailable;
	}
}

FCatPlaceChumResult UCatChumPlacementService::PlaceChum(APlayerController* RequestingController,
	const FCatPlaceChumCommand& Command)
{
	using namespace CatChumPlacementServicePrivate;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !RequestingController
		|| !RequestingController->HasAuthority())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable);
	}
	const APlayerState* PlayerState = RequestingController->PlayerState;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	if (StableNetId.IsEmpty())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::InvalidIdentity);
	}
	if (!Command.RequestId.IsValid())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload);
	}
	UCatChumFieldSubsystem* Fields = World->GetSubsystem<UCatChumFieldSubsystem>();
	if (!Fields)
	{
		return MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable);
	}
	FCatPlaceChumResult Replay;
	if (Fields->TryGetTerminalResult(StableNetId, Command.RequestId, Replay))
	{
		return Replay;
	}
	auto FinalizeFirstResult = [Fields, &StableNetId](const FCatPlaceChumResult& Candidate)
	{
		Fields->StoreTerminalResult(StableNetId, Candidate);
		FCatPlaceChumResult Frozen;
		return Fields->TryGetTerminalResult(StableNetId, Candidate.RequestId, Frozen) ? Frozen : Candidate;
	};
	const ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!GameMode || !GameMode->CanAcceptFishingCommand(RequestingController))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::CommandsClosed));
	}
	const UCatChumFieldSettings* Settings = GetDefault<UCatChumFieldSettings>();
	if (!Settings->IsRuntimeReady())
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::FeatureDisabled));
	}
	if (!Command.ExpectedWaterRegionHandle.IsValid() || Command.ChumDefinitionId.IsNone()
		|| Command.Quantity <= 0 || !FMath::IsFinite(Command.ClientCandidateWorldPoint.X)
		|| !FMath::IsFinite(Command.ClientCandidateWorldPoint.Y)
		|| !FMath::IsFinite(Command.ClientCandidateWorldPoint.Z))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload));
	}
	ACatCharacter* Character = Cast<ACatCharacter>(RequestingController->GetPawn());
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
		Command.ChumDefinitionId);
	if (!Character || !Conditions || Conditions->GetSnapshot().bDowned || !Equipment || !Definition
		|| Definition->Kind != ECatEquipmentKind::Chum
		|| !Definition->IsRuntimeDefinitionReady()
		|| Command.Quantity > Definition->ChumInfluence.MaximumQuantityPerPlacement)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DefinitionUnavailable));
	}
	UCatWaterQuerySubsystem* WaterQuery = World->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	const FCatWaterSpatialResult Water = WaterQuery->ResolveCandidatePointToWater(
		Command.ClientCandidateWorldPoint, Command.ExpectedWaterRegionHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, MapWaterError(Water.Error)));
	}
	const FVector ViewOrigin = Character->GetPawnViewLocation();
	const FVector ToTarget = Water.WaterSurfaceWorldPoint - ViewOrigin;
	const double Distance = ToTarget.Length();
	const FVector ViewDirection = RequestingController->GetControlRotation().Vector();
	const double MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(Settings->MaxAimDeviationDegrees));
	if (!FMath::IsFinite(Distance) || Distance > Settings->MaxPlacementRangeCentimeters
		|| ToTarget.IsNearlyZero() || FVector::DotProduct(ViewDirection, ToTarget.GetSafeNormal()) < MinimumAimDot)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::PlacementOutOfRange));
	}
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CatPlaceChumLineOfSight), true);
	TraceParams.AddIgnoredActor(Character);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ViewOrigin, Water.WaterSurfaceWorldPoint,
		Settings->PlacementLineOfSightChannel, TraceParams))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::PlacementOccluded));
	}
	const FCatRunConsumableUseResult Reserved = Equipment->BeginRunConsumableUse(Command.RequestId,
		Command.ChumDefinitionId, Command.Quantity, Command.ExpectedEquipmentRevision);
	if (!Reserved.bReserved)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, MapEquipmentError(Reserved.Error)));
	}
	FCatPrepareChumFieldRequest PrepareRequest;
	PrepareRequest.StableNetId = StableNetId;
	PrepareRequest.Command = Command;
	PrepareRequest.ServerCorrectedCenter = Water.WaterSurfaceWorldPoint;
	PrepareRequest.Influence = Definition->ChumInfluence;
	PrepareRequest.ServerTime = World->GetTimeSeconds();
	const FCatPrepareChumFieldResult Prepared = Fields->PrepareField(PrepareRequest);
	if (!Prepared.bPrepared)
	{
		Equipment->ReleaseRunConsumableUse(Command.RequestId);
		return FinalizeFirstResult(MakeError(Command.RequestId, Prepared.Error));
	}
	const FCatRunConsumableUseResult Committed = Equipment->CommitRunConsumableUseDeferred(Command.RequestId);
	if (!Committed.bCommitted)
	{
		Fields->AbortPreparedField(Prepared.CommitToken);
		return FinalizeFirstResult(MakeError(Command.RequestId, MapEquipmentError(Committed.Error)));
	}
	const FCatPlaceChumResult Activated = Fields->ActivatePreparedFieldDeferred(
		Prepared.CommitToken, Committed.EquipmentRevision);
	if (!Activated.bCommitted)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, Activated.Error));
	}
	const FCatPlaceChumResult Frozen = FinalizeFirstResult(Activated);
	Equipment->PublishDeferredRunConsumableUse(Command.RequestId);
	Fields->PublishActivatedField(Frozen.FieldId);
	return Frozen;
}
