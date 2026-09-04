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
	/** 真咬响应窗口；此时只存在浮漂信号，鱼种与鱼 Actor 要等合法左键到达服务器后才创建。 */
	TrueBiteWindow = 2,
	/** Hooked 后唯一允许多人协作的搏斗阶段。 */
	HookedFight = 3,
	/** 兼容近岸阶段；与 HookedFight/ExhaustedReel 一样按抄网几何开放，不绑定容器事务。 */
	NearShore = 4,
	/** 会话已解决；鱼可能已转成嘴叼世界鱼，或作为力竭落地世界鱼等待拾取。 */
	Resolved = 5,
	/** 掉线、倒地、局末或依赖失效后终止；旧半场不会重连恢复。 */
	Terminated = 6,
	CastFlight = 7,
	Waiting = 8,
	AutoHauling = 9,
	/** 鱼体力已归零且不再挣扎；玩家必须继续收线把鱼真实拖过岸线。 */
	ExhaustedReel = 10
};

UENUM(BlueprintType)
enum class ECatFishingOutcome : uint8
{
	None,
	/** 抄网已把水中鱼交接为抄手嘴叼的世界鱼；尚未写入容器。 */
	Caught,
	EmptyHook, HookWindowExpired, Escaped,
	/** 同一鱼竿实例的累计耐久耗尽；必须维修或换一根可用鱼竿才能继续钓鱼。 */
	RodBroken,
	CatInWater, Cancelled, Invalidated,
	/** 会话已把鱼安全释放为独立岸上拾取物；鱼尚未归属任何玩家。 */
	Landed,
	/** 旧蓝图/表现资产的枚举值兼容；现行搏斗不再生成强度过载断线。保持枚举序号。 */
	LineBroken UMETA(Hidden),
	/** 玩家主动切断本场鱼线止损；鱼与已消耗鱼饵丢失，不追加或退还鱼竿磨损。 */
	LineCut
};

UENUM(BlueprintType)
enum class ECatFishMotionIntent : uint8
{
	None, CalmOrInward, StrugglingOutward, AutoHauling
};

UENUM(BlueprintType)
enum class ECatFishSelectionResolution : uint8
{
	None, InProgress, Selected, NoEligibleFish, Failed
};

USTRUCT(BlueprintType)
struct FCatFishSelectionCommitResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) ECatFishSelectionResolution Resolution = ECatFishSelectionResolution::None;
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;
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
	/** 本次抛竿绑定的鱼竿物品实例；它来自部署 Actor，用于诊断和后续实例状态回写。 */
	UPROPERTY(BlueprintReadOnly) FGuid RodItemInstanceId;
	UPROPERTY(BlueprintReadOnly) FName RodDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName FloatDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName BaitDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) int64 EquipmentReservationRevision = 0;
	UPROPERTY(BlueprintReadOnly) int64 RodActorRevision = 0;
	UPROPERTY(BlueprintReadOnly) FVector ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
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

	/** 本次服务器实际抽到的鱼重量；力量、视觉和捕获结果都使用这一份个体事实。 */
	UPROPERTY(BlueprintReadOnly)
	double FishWeightKilograms = 0.0;

	/** 本次个体由重量换算、并叠加完美中鱼倍率后的搏斗力量。 */
	UPROPERTY(BlueprintReadOnly)
	double FishStrength = 0.0;

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

	/** 兼容旧 HUD 的二值输入指示：1=正在收线，0=未收线；不再存在蓄力积分。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Use bReeling; charging was removed"))
	float PrimaryPowerAlpha = 0.0f;

	/** 当前固定步参与意图求解的多人力量合计，尚未乘竿向修正。 */
	UPROPERTY(BlueprintReadOnly)
	double ActiveCombinedFishingStrength = 0.0;

	/** 当前实际贡献/消耗体力模型中的辅助位数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ActiveHelperCount = 0;

	/** 当前鱼短周期体力剩余；常规搏斗由固定步 Runner 消耗，兼容巨鱼交换由 StateTree Task 消耗。 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStaminaRemaining = 0.0;

	/** 冻结定义体力的归一化剩余值；鱼的位置继续由 FishEncounter Transform 复制。 */
	UPROPERTY(BlueprintReadOnly)
	double NormalizedFishStamina = 0.0;

	/** 本场绑定鱼竿实例剩余耐久的只读镜像；来自 Equipment，跨场累计，终态不补满。 */
	UPROPERTY(BlueprintReadOnly, meta=(DisplayName="Rod Durability Remaining"))
	double RodDurabilityRemaining = 0.0;

	/** 主位当前是否按住收线；松开边沿立即回到锁线。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReeling = false;

	/** 当前是否主动按住右键自由出线；两键均松开时为锁线。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSlacking = false;

	/** 本次是否在完美响应窗内提竿；鱼力量/体力已按性格模板折减。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPerfectHook = false;

	/** FishEncounter 的只读运动意图；不含位置或 Transform。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishMotionIntent FishMotionIntent = ECatFishMotionIntent::None;

	/** 鱼当前游向与鱼线向外方向夹角余弦，范围 [-1,1]。 */
	UPROPERTY(BlueprintReadOnly)
	float FishLineAlignment = 0.0f;

	/** 性格曲线处理后的归一化鱼线受力，范围 [0,1]。 */
	UPROPERTY(BlueprintReadOnly)
	float NormalizedLineLoad = 0.0f;

	/** 服务器确认的强对抗状态；客户端只消费，不自行按 Transform 猜测。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStrongConfrontation = false;

	/** 当前竿身朝向产生的有效杠杆倍率；1 为完全对齐，配置下限防止瞬时归零。 */
	UPROPERTY(BlueprintReadOnly)
	float RodLeverageMultiplier = 1.0f;

	/** 兼容旧 HUD，移动不再折算为力量百分比。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Carrier movement is an endpoint intent"))
	float CarrierMovementAlpha = 0.0f;

	/** 鱼占优部分传到猫端的加速度；实际运动由 Rod 平滑追赶同一步的目标牵引速度。 */
	UPROPERTY(BlueprintReadOnly)
	float CarrierPullAccelerationCentimetersPerSecondSquared = 0.0f;

	/** 持竿者沿远离鱼方向的权威速度倍率；1 表示当前没有运动约束。 */
	UPROPERTY(BlueprintReadOnly)
	float CarrierAwaySpeedMultiplier = 1.0f;

	/** 统一约束求解前的线长误差，用于 Development 包诊断和调试表现。 */
	UPROPERTY(BlueprintReadOnly)
	float ConstraintErrorCentimeters = 0.0f;

	/** 同一份约束误差本步实际分配给鱼端的水平修正距离。 */
	UPROPERTY(BlueprintReadOnly)
	float FishConstraintCorrectionCentimeters = 0.0f;

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

/** 抄网命令；身份由服务器 Controller 重建，ExpectedRevision 只绑定 FishingSession。 */
USTRUCT(BlueprintType)
struct FCatScoopCommand
{
	GENERATED_BODY()

	/** 抢抄 RequestId、Session ExpectedRevision 与服务器身份。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;
};

/** 抄网事务终态；成功表示鱼已成为抄手嘴上的世界鱼，尚未写入任何容器。 */
USTRUCT(BlueprintType)
struct FCatScoopResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 对应 FishingSession。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;
};
