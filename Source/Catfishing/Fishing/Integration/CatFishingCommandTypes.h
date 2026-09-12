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
	/** 主动切断当前上钩会话的鱼线；与鱼竿断裂及普通取消分别结算。 */
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
	GuardCapacityExceeded, CaptureAlreadyCommitted,
	RodDeploymentLimitReached,
	/**
	 * 抢抄拒绝的六个细分原因（钓鱼规则 §5.5:273）。ScoopGeometryFailed 保留为「原因不明的几何失败」兜底，
	 * 新代码应给出下面这六个之一，玩家提示由 CatFishingCommandFeedback::GetPlayerFacingText 统一映射。
	 */
	ScoopOutOfReach, ScoopLineOfSightBlocked, ScoopGroundTooSteep, ScoopVerticalDeltaTooLarge,
	ScoopMouthOccupied, ScoopNotOnShore
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
	/** 放竿命令发起时观察到的钓具选择投影版本；它只保护当前选中的竿、皮肤等装备视图。 */
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
	/** 钓具选择投影的版本；它只保护当前选中的鱼竿、鱼漂等装备视图，不能当作随身库存内容版本。 */
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

/** 把抢抄拒绝原因映射成对应的命令错误码；None 表示这次拒绝不是可抄几何，调用方保留原错误。 */
ECatFishingCommandError MapScoopRejectReason(ECatScoopRejectReason Reason);

namespace CatFishingCommandFeedback
{
	/**
	 * 钓鱼命令错误的玩家可见提示（ui 表「提示文案」）。
	 * 抢抄的四种几何原因（够不着／有遮挡／脚下太陡／高差太大）统一回「没够着」，
	 * 嘴里有鱼与不在岸上各自有话说；没有玩家话术的错误返回空文本，调用方据此不弹提示。
	 */
	CATFISHING_API FText GetPlayerFacingText(ECatFishingCommandError Error);
}
