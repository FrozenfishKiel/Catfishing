#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "Items/CatItemTypes.h"
#include "CatFishingTypes.generated.h"

/** 单次钓鱼长流程的公开阶段；转移拓扑只由 ST_FishingSession 资产编排。 */
UENUM(BlueprintType)
enum class ECatFishingPhase : uint8
{
	/** 会话对象已建立但 StateTree 尚未进入试探期。 */
	Created = 0,
	/** 鱼只给试探信号；此阶段提竿不能直接形成捕获。 */
	Probe = 1,
	/** 真咬响应窗口；具体时长与输入规则由未裁配置和资产决定。 */
	TrueBiteWindow = 2,
	/** Hooked 后唯一允许多人协作的搏斗阶段。 */
	HookedFight = 3,
	/** 鱼已到近岸并等待首个合法抢抄 Compare-and-Commit。 */
	NearShore = 4,
	/** 捕获事务已提交且唯一鱼实例已经进入胜者鱼护。 */
	Resolved = 5,
	/** 掉线、倒地、局末或依赖失效后终止；旧半场不会重连恢复。 */
	Terminated = 6,
	CastFlight = 7,
	Waiting = 8,
	AutoHauling = 9
};

UENUM(BlueprintType)
enum class ECatFishingOutcome : uint8
{
	None, Caught, EmptyHook, HookWindowExpired, Escaped, RodBroken, CatInWater, Cancelled, Invalidated
};

UENUM(BlueprintType)
enum class ECatFishMotionIntent : uint8
{
	None, CalmOrInward, StrugglingOutward, AutoHauling
};

class APlayerState;
class ACatFishingRodActor;
class ACatFishingHookActor;
class ACatFishEncounterActor;

enum class ECatFishingSnapshotMutation : uint8
{
	HighFrequency,
	Discrete,
	PhaseChange
};

USTRUCT(BlueprintType)
struct FCatFishingAttemptSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> FisherPlayerState = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<ACatFishingRodActor> RodActor = nullptr;
	UPROPERTY(BlueprintReadOnly) FName RodDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName FloatDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName BaitDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) int64 EquipmentReservationRevision = 0;
	UPROPERTY(BlueprintReadOnly) int64 RodActorRevision = 0;
	UPROPERTY(BlueprintReadOnly) FVector ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FName WaterRegionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) int64 GeometryRevision = 0;
	UPROPERTY() uint64 ServerRandomSeed = 0;
};

/** FishingSession 对客户端公开的最小只读事实；不复制 FishDefinition 对象、StableNetId 或容器写模型。 */
USTRUCT(BlueprintType)
struct FCatFishingSessionSnapshot
{
	GENERATED_BODY()

	/** 本次钓鱼会话的一局稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 每次阶段进入、协作者集合变化或终态提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 每次公开快照写入都会递增；高频输入不使离散命令的 Revision 过期。 */
	UPROPERTY(BlueprintReadOnly)
	int64 SnapshotSequence = 0;

	/** 每次阶段变化递增，用于拒绝前一阶段的延迟事件。 */
	UPROPERTY(BlueprintReadOnly)
	int64 PhaseEpoch = 0;

	/** 此次投竿的服务器分配身份；不同于会话身份且不可由 Session 自行生成。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CastAttemptId;

	/** 会话完成原因；Terminated 阶段不从阶段名猜测结果。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingOutcome Outcome = ECatFishingOutcome::None;

	/** 当前阶段在服务器开始的世界时间。 */
	UPROPERTY(BlueprintReadOnly)
	double PhaseStartedServerTime = 0.0;

	/** 真咬窗口的服务器截止时间；未配置窗口时保持零。 */
	UPROPERTY(BlueprintReadOnly)
	double WindowEndsServerTime = 0.0;

	/** 当前钓手的公开 PlayerState 身份；StableNetId 不复制。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> FisherPlayerState = nullptr;

	/** 表现 Actor 的类型化引用；Task 6 不负责生成或初始化它们。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACatFishingRodActor> RodActor = nullptr;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACatFishingHookActor> HookActor = nullptr;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACatFishEncounterActor> FishEncounterActor = nullptr;

	/** 当前由服务器 StateTree 进入的公开阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase Phase = ECatFishingPhase::Created;

	/** 当前鱼种稳定 ID；表现和图鉴候选据此查询 Data，不持资产指针。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 当前鱼是否属于 Giant 档；只影响 HookedFight 是否接收协作者，近岸仍抢抄。 */
	UPROPERTY(BlueprintReadOnly)
	bool bGiant = false;

	/** 当前搏斗参与者数量；不公开平台身份数组。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FightParticipantCount = 0;

	/** 当前服务器计算的参与者 FishingStrength 合计；只在 HookedFight 更新，不能由客户端写入。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFishingStrength = 0.0;

	/** 当前服务器认定的合法参与者 FightStamina 合计；与人数和力量一起描述当下协作可达性。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFightStamina = 0.0;

	/** 当前鱼短周期体力剩余；只由 StateTree FightExchange Task 消耗，进入会话时从 FishDefinition 冻结。 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStaminaRemaining = 0.0;

	/** 冻结定义体力的归一化剩余值；鱼的位置继续由 FishEncounter Transform 复制。 */
	UPROPERTY(BlueprintReadOnly)
	double NormalizedFishStamina = 0.0;

	/** 当前连续收线输入；高频更新不推进离散命令 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReeling = false;

	/** FishEncounter 的只读运动意图；不含位置或 Transform。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishMotionIntent FishMotionIntent = ECatFishMotionIntent::None;

	void AdvanceVersion(const ECatFishingSnapshotMutation Mutation)
	{
		++SnapshotSequence;
		if (Mutation == ECatFishingSnapshotMutation::Discrete || Mutation == ECatFishingSnapshotMutation::PhaseChange)
		{
			++Revision;
		}
		if (Mutation == ECatFishingSnapshotMutation::PhaseChange)
		{
			++PhaseEpoch;
		}
	}
};

/** StateTree 阶段入口结果；失败不会由 C++ 选择备用阶段。 */
USTRUCT(BlueprintType)
struct FCatFishingPhaseResult
{
	GENERATED_BODY()

	/** 本次资产选择的阶段是否已应用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	/** 处理前阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase PreviousPhase = ECatFishingPhase::Created;

	/** 处理后阶段。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingPhase CurrentPhase = ECatFishingPhase::Created;

	/** 失败原因；成功为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;

	/** 结果对应的 Session Revision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 近岸抢抄命令；身份由服务器 Controller 重建，Guard Revision 绑定实际目标容量事实。 */
USTRUCT(BlueprintType)
struct FCatScoopCommand
{
	GENERATED_BODY()

	/** 抢抄 RequestId、Session ExpectedRevision 与服务器身份。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 抢抄者个人鱼护 ID；RPC 到达服务器后由当前 Pawn 的 authority 注册事实覆盖，客户端值不参与授权。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid TargetGuardContainerId;

	/** 客户端表现命中位置，仅用于载荷诊断；授权只比较服务器 Character 与 StateTree 绑定的权威近岸目标。 */
	UPROPERTY(BlueprintReadWrite)
	FVector ScoopWorldLocation = FVector::ZeroVector;
};

/** 抢抄事务终态；只在首个合法请求成功时包含 CaptureCommittedResult。 */
USTRUCT(BlueprintType)
struct FCatScoopResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 对应 FishingSession 而非容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** Items 唯一捕获提交结果；拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatCaptureCommittedResult Capture;
};

/** FishingService 建立会话的同步结果；成功只表示 StateTree 已启动，不表示已经咬钩或捕获。 */
USTRUCT(BlueprintType)
struct FCatFishingStartResult
{
	GENERATED_BODY()

	/** 与开始意图一致的 RequestId；相同服务器身份重试会重放包含同一会话 ID 的首次结果。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 会话是否成功创建并启动唯一 StateTree。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStarted = false;

	/** 新会话 ID；失败保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 启动失败原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;
};
