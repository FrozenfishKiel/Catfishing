#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingCommandTypes.generated.h"

UENUM(BlueprintType)
enum class ECatFishingCommandType : uint8
{
	None, PlaceRod, OperateRod, LeaveRod, PackRod, ChangeRodSkin, BeginCast, RequestHook, SetReeling,
	PrimaryReleased, CancelFishing, RequestScoop, AssistFight, PlaceChum, TailRescue,
	/** 右键松开线杯的按下 / 松开边沿；只在 HookedFight 有效。 */
	SlackPressed, SlackReleased,
	/** Q 打窝蓄力按下 / 松开；服务器按按住时长换算蓄力并投放。 */
	ChumPressed, ChumReleased,
	/** 主动切断当前上钩会话的鱼线；与自然断线及普通取消分别结算。 */
	CutLine
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

	/** 本次放竿意图的幂等键；服务端用它串联库存借出、鱼竿生成和失败回滚。 */
	UPROPERTY(BlueprintReadWrite) FGuid RequestId;
	/** 放竿命令发起时观察到的钓具选择投影版本；它和正式库存版本不是替代关系，只保护当前选中的竿、皮肤等装备视图。 */
	UPROPERTY(BlueprintReadWrite) int64 ExpectedEquipmentRevision = 0;
	/** 新输入分派在提交放竿时写入的正式随身库存版本；PlaceRod 用它判断鱼竿实例离开背包前，背包是否仍是同一份事实。0 表示旧调用方未提供该版本，只跳过库存版本校验，不代表当前库存版本为 0。 */
	UPROPERTY(BlueprintReadWrite) int64 ExpectedInventoryRevision = 0;
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
struct FCatCutFishingLineCommand { GENERATED_BODY() UPROPERTY(BlueprintReadWrite) FCatFishingSessionCommandContext Context; };

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
	/** 涉及正式库存事务的命令完成后，服务器看到的背包内容版本；旧命令可保持 0，不参与钓鱼会话或鱼竿 Actor 并发。 */
	UPROPERTY(BlueprintReadOnly) int64 InventoryRevision = 0;
	/** 钓具选择或旧库存投影的版本；迁移期仍供旧监听者读取，但不能当作正式库存内容版本。 */
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
FCatFishingSessionCommandContext MakeFishingSessionCommandContext(FGuid FishingSessionId, const FCatScoopCommand& LegacyCommand);
