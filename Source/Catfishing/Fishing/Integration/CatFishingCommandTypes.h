#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingCommandTypes.generated.h"

UENUM(BlueprintType)
enum class ECatFishingCommandType : uint8
{
	None, PlaceRod, OperateRod, LeaveRod, PackRod, ChangeRodSkin, BeginCast, RequestHook, SetReeling,
	PrimaryReleased, CancelFishing, RequestScoop, AssistFight, PlaceChum, TailRescue
};

UENUM(BlueprintType)
enum class ECatFishingCommandError : uint8
{
	None, FeatureDisabled, RunClosed, CommandsClosed, InvalidIdentity, InvalidPayload, DependencyUnavailable,
	NoRod, RodOccupied, RodBroken, EquipmentRevisionConflict, RodActorRevisionConflict, InvalidWaterTarget,
	CastOutOfRange, WaterNotFound, AmbiguousWater, ActiveSessionExists, SessionNotFound, NotFisher,
	RevisionConflict, CastAttemptConflict, InputSequenceStale, InputSequenceGapTooLarge, InvalidPhase,
	WindowClosed, AlreadyResolved, NotNearShore, StaleScoopTarget, ScoopGeometryFailed, CooldownActive,
	GuardCapacityExceeded, CaptureAlreadyCommitted
};

USTRUCT(BlueprintType)
struct FCatRodCommandContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) FGuid RodActorId;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedEquipmentRevision = 0;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedRodActorRevision = 0;
};

USTRUCT(BlueprintType)
struct FCatFishingSessionCommandContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedRevision = 0;
	UPROPERTY(BlueprintReadWrite) FGuid CastAttemptId;
};

USTRUCT(BlueprintType)
struct FCatPlaceRodCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedEquipmentRevision = 0;
};

USTRUCT(BlueprintType)
struct FCatOperateRodCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatRodCommandContext Context; };
USTRUCT(BlueprintType)
struct FCatLeaveRodCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatRodCommandContext Context; };
USTRUCT(BlueprintType)
struct FCatPackRodCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatRodCommandContext Context; };

USTRUCT(BlueprintType)
struct FCatChangeRodSkinCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FCatRodCommandContext Context;
	UPROPERTY(BlueprintReadWrite) FName RodSkinDefinitionId = NAME_None;
};

USTRUCT(BlueprintType)
struct FCatBeginCastCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) FGuid RodActorId;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedEquipmentRevision = 0;
	UPROPERTY(BlueprintReadWrite) int64 ExpectedRodActorRevision = 0;
	UPROPERTY(BlueprintReadWrite) FVector ClientCandidateWorldPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite) FCatWaterRegionHandle ExpectedWaterRegionHandle;
};

USTRUCT(BlueprintType)
struct FCatRequestHookCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context; };

USTRUCT(BlueprintType)
struct FCatSetReelingCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadWrite) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadWrite) FGuid ActivationCorrelationId;
	UPROPERTY(BlueprintReadWrite) int64 InputSequence = 0;
	UPROPERTY(BlueprintReadWrite) bool bReeling = false;
};

USTRUCT(BlueprintType)
struct FCatPrimaryReleasedCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	UPROPERTY(BlueprintReadWrite) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadWrite) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadWrite) FGuid ActivationCorrelationId;
	UPROPERTY(BlueprintReadWrite) int64 InputSequence = 0;
};

USTRUCT(BlueprintType)
struct FCatCancelFishingCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context; };

USTRUCT(BlueprintType)
struct FCatRequestScoopCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context;
	UPROPERTY(BlueprintReadWrite) FVector ClientCandidateWorldPoint = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FCatAssistFightCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context; };
USTRUCT(BlueprintType)
struct FCatTailRescueCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context; };

USTRUCT(BlueprintType)
struct FCatFishingCommandResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) ECatFishingCommandType CommandType = ECatFishingCommandType::None;
	UPROPERTY(BlueprintReadOnly) bool bCommitted = false;
	UPROPERTY(BlueprintReadOnly) ECatFishingCommandError Error = ECatFishingCommandError::DependencyUnavailable;
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly) int64 SnapshotSequence = 0;
	UPROPERTY(BlueprintReadOnly) int64 PhaseEpoch = 0;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) FGuid RodActorId;
	UPROPERTY(BlueprintReadOnly) int64 RodActorRevision = 0;
	UPROPERTY(BlueprintReadOnly) int64 EquipmentRevision = 0;
	UPROPERTY(BlueprintReadOnly) FGuid SuggestedFishingSessionId;
};

USTRUCT(BlueprintType)
struct FCatBeginCastResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FCatFishingCommandResult Command;
	UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
	UPROPERTY(BlueprintReadOnly) FVector ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
};

ECatFishingCommandError MapDomainCommandError(ECatDomainCommandError Error);
FCatFishingCommandResult MakeFishingCommandResult(const FCatFishingStartResult& LegacyResult);
FCatFishingSessionCommandContext MakeFishingSessionCommandContext(FGuid FishingSessionId, const FCatScoopCommand& LegacyCommand);
