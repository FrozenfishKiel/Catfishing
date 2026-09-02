#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Fishing/CatFishingTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Data/CatFishSelectionTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingSession.generated.h"

class ACatCharacter;
class ACatFishingHookActor;
class ACatFishPickupActor;
class UCatEquipmentComponent;
class UCatFishDefinition;
class UCatItemsService;
class UCatFishingFightRunner;
class UStateTreeComponent;
struct FCatFightStepResult;
class FCatFishingSessionReplicationContractTest;
class FCatFishingSessionSnapshotVersionMutationRulesTest;
class FCatFishingSessionTerminationOutcomeTest;
class FCatFishingSessionScoopMouthCarryTest;
class FCatFishingSessionRejectedFightSummaryPublicationTest;
class FCatFishingSessionExhaustedReelContinuityTest;
class FCatFishingSessionLandedTerminalVisibilityTest;
class FCatFishingSessionOutcomePresentationTagTest;
class FCatFishingServiceRodBoundSessionRoutingTest;

DECLARE_MULTICAST_DELEGATE(FCatFishingSessionSnapshotChanged);

/** 一次服务器钓鱼长流程宿主；StateTree 拥有阶段拓扑，Actor 只执行阶段副作用与短事务。 */
UCLASS(BlueprintType)
class CATFISHING_API ACatFishingSession : public AActor
{
	GENERATED_BODY()

public:
	/** 创建唯一 StateTree 组件、开启只读 Snapshot 复制并关闭 Tick。 */
	ACatFishingSession();

	/** 注册公开 Snapshot 复制；私有身份、鱼资产和容器服务引用不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 两阶段抛竿准备入口；捕获结果先生成世界鱼，不在抛竿阶段冻结任何鱼护容器。 */
	bool PrepareSessionFromAuthority(const FCatFishingAttemptSnapshot& Attempt, AController* FisherController,
		ACatCharacter* FisherCharacter, ACatFishingHookActor* HookActor);
	bool StartPreparedSessionLogicFromAuthority();
	bool PublishPreparedSessionFromAuthority();
	void AbortPreparedSessionFromAuthority();
	bool ScheduleWaitingProbeFromStateTree();
	/** Probe 状态只打开响应窗口，不选鱼、不生成鱼、不扣饵；鱼只在合法 RequestHook 到达后创建。 */
	bool OpenTrueBiteWindowFromStateTree();
	FCatFishingCommandResult RequestHookFromAuthority(FGuid RequestId);
	FCatFishingCommandResult CancelFromAuthority(FGuid RequestId);
	/** 上钩后的主动止损写口；只接受当前钓手和精确 Revision，提交后鱼/饵丢失但不追加鱼线磨损。 */
	FCatFishingCommandResult CutLineFromAuthority(AController* RequestingController,
		const FCatFishingSessionCommandContext& Context);
	bool TryEnterHookedFightFromAuthority();
	bool SetReelingFromAuthority(int64 InputSequence, bool bReeling);
	/** 右键松开线杯写口；仅 HookedFight 且 Runner 运行中生效，收线优先。 */
	bool SetSlackingFromAuthority(int64 InputSequence, bool bSlacking);
	/** 主操作手离开竿位：搏斗期进入无人值守松线，等口期清空当前钓手；都不结束会话。 */
	void SuspendOperatorFromAuthority();
	bool IsFightRunnerRunning() const;
	void HandleFightRunnerStepFromAuthority(const FCatFightStepResult& Step, double FishStaminaRemaining,
		ECatFishMotionIntent MotionIntent, double RodDurabilityRemaining);
	/** FightRunner/表现写入遇到不可恢复错误时终止会话；FailureStage 会进入日志，便于区分几何、装备、ASC 等故障。 */
	void HandleFightRunnerFailureFromAuthority(FName FailureStage = NAME_None);

	/** StateTree EnterPhase Task 的唯一阶段写入口；NearShore 必须提供水域内服务器目标，HookedFight/NearShore 保留合法参与者，其他阶段重置为钓手，终态启动有界销毁。 */
	FCatFishingPhaseResult EnterPhaseFromStateTree(ECatFishingPhase NewPhase);

	/** 巨鱼 HookedFight 中按统一参战能力谓词登记一次协作者；非 Active、倒地、力量/体力非正、阶段不符或重复 RequestId 都不增加参与集合。 */
	FCatDomainCommandResult SubmitFightAssist(AController* AssistingController, FGuid RequestId, int64 ExpectedRevision);

	/** StateTree 搏斗节点的唯一资源交换写口；读取 Character ASC 与 FishDefinition 后原子消耗双方短周期体力。 */
	FCatDomainCommandResult ResolveFightExchangeFromStateTree(double FishStaminaCost, double ParticipantStaminaCost);

	/** StateTree 失败节点提交本会话唯一物资惩罚；丢特殊饵与伤竿互斥且同会话只允许一次。 */
	FCatFishingFailureResult CommitFailureBudgetFromStateTree(ECatFishingFailurePenalty Penalty);

	/** StateTree 在唯一已裁的“重试耗尽”逃鱼终态调用；Collection 生成剪影 Grant 后终止会话且不创建实物鱼。 */
	FCatDomainCommandResult ResolveRetryExhaustedEscapeFromStateTree();

	/** 鱼上钩后可无视鱼的剩余体力抄取；服务器范围校验成功即生成世界鱼并直接进入抄手嘴叼状态。 */
	FCatScoopResult RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command);

	/**
	 * 多人接力（规格：用别人的竿继续钓）：把会话的"钓手"身份转移给新操作者。
	 * 允许在等待/试探/真咬及 HookedFight 转移；搏斗接力会迁移 Runner 的 ASC、力量、体力和输入序号域。
	 * 饵料预留与竿磨损仍结算到抛竿时冻结的 CastEquipment（原始抛竿者的装备），体力/力量随新钓手。
	 * 接力只转移当前操作猫；鱼最终落地为世界 Actor，接力时不绑定任何鱼护。
	 * 仅供 UCatFishingService 在主操作位占用提交后调用；失败时服务回滚刚增加的竿位。
	 */
	bool TransferFisherFromAuthority(AController* NewFisherController);

	/** 会话当前钓手的服务器私有身份（转移后为新钓手）；仅服务读取用于索引维护。 */
	const FString& GetFisherStableNetIdForAuthority() const { return FisherStableNetId; }

	/** 掉线、倒地、局末或依赖失效时幂等写 Terminated、停树并释放参与者；Resolved 保持捕获终态，两种终态都只保留配置的复制窗口后销毁 Actor。 */
	void TerminateSession(ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason);

	/** 判断 Character 是否为钓手或已登记协作者；FishingService 用于生命周期中断。 */
	bool InvolvesCharacter(const ACatCharacter* Character) const;

	/** 提供当前复制阶段和协作摘要供服务/表现读取；私有参与身份、鱼资产和事务缓存不会随返回值泄露。 */
	const FCatFishingSessionSnapshot& GetSnapshot() const;

	/** 蓝图只读副本（HUD/调试用）；与 GetSnapshot 相同内容，按值返回。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing")
	FCatFishingSessionSnapshot GetReplicatedSnapshot() const { return GetSnapshot(); }

	/** 蓝图只读：会话是否已进入终态。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing")
	bool IsSessionTerminal() const { return IsTerminal(); }

	/** 本机完整 Snapshot 变化通知；订阅者只需重新读取 GetSnapshot。 */
	FCatFishingSessionSnapshotChanged OnSnapshotChanged;

	/** 判断会话是否已进入 Resolved/Terminated 终态；FishingService 据此清理会话弱索引。 */
	bool IsTerminal() const;

protected:
	/** World 销毁时停止仍运行的 StateTree并清弱引用，随后交给父类。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FCatFishingSessionReplicationContractTest;
	friend class FCatFishingSessionSnapshotVersionMutationRulesTest;
	friend class FCatFishingSessionTerminationOutcomeTest;
	friend class FCatFishingSessionScoopMouthCarryTest;
	friend class FCatFishingSessionRejectedFightSummaryPublicationTest;
	friend class FCatFishingSessionExhaustedReelContinuityTest;
	friend class FCatFishingSessionLandedTerminalVisibilityTest;
	friend class FCatFishingSessionLineBreakKeepsRodOperableTest;
	friend class FCatFishingSessionOutcomePresentationTagTest;
	friend class FCatFishingSessionCutLineCommandTest;
	friend class FCatFishingSessionGroundedCutLineCommandTest;
	friend class FCatFishingServiceRodBoundSessionRoutingTest;

	/** 客户端收到完整 Snapshot 后只广播重读信号，不推进任何玩法。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** authority 发布和客户端 RepNotify 共用的本地重读信号；调用本身不修改 Snapshot。 */
	void NotifySnapshotChanged();

	/** 从已激活 Controller 的 PlayerState::UniqueId 读取服务器私有身份；失败返回空。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 发布 Snapshot 并请求立即网络更新；只由 authority 调用。 */
	void PublishSnapshot(ECatFishingSnapshotMutation Mutation);

	/** 终态的单一直接写口；StateTree 不可进入终态。 */
	void FinalizeSession(ECatFishingPhase FinalPhase, ECatFishingOutcome FinalOutcome, const TCHAR* DiagnosticReason);

	/** 只有断线/猫落水拥有当前猫 Montage；其余终局返回空 Tag，不借用错误表现。 */
	static FGameplayTag ResolveTerminalFisherPresentationTag(ECatFishingOutcome Outcome);

	/** 用 FishingService 的统一权威谓词重读参与者，更新公开人数、合计 FishingStrength 与 FightStamina。 */
	bool RefreshFightSummary();

	/** 仅在失败路径重读摘要实际改变时发出高频复制更新。 */
	void PublishRefreshedFightSummaryIfChanged(bool bSummaryChanged);

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();
	bool BeginExhaustedReelFromAuthority();
	void HandleExhaustedReelStep();
	/** 力竭回收阶段统一同步鱼嘴 Hook 与绷紧鱼线表现；避免位置移动而客户端仍沿用搏斗末帧的旧 L_paid/Slack。 */
	bool PublishExhaustedReelLineFromAuthority(const FVector& FishWorldLocation);
	bool TryResolveExhaustedReelTarget(FVector& OutTarget) const;
	bool SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation);
	/** 抄网成功时生成世界鱼并立即附到抄手嘴部；不读取鱼体力，也不写入鱼护。 */
	bool SpawnScoopedFishPickupFromAuthority(ACatCharacter* ScoopingCharacter, APlayerState* ScoopingPlayerState,
		const FString& ScooperStableNetId);
	bool CommitCatchEquipmentFromAuthority();
	void HandleBiteWarningTimer();
	void HandleProbeTimer();
	void HandleTrueBiteWindowExpired();
	/** 真咬窗口内收到合法左键后，冻结选择上下文、选鱼、生成 Encounter 并提交饵料。 */
	FCatFishSelectionCommitResult ResolveHookSelectionFromAuthority();
	bool TryReadNearShoreFishSpatial(FCatWaterSpatialResult& OutSpatial) const;

	/** 当前会话唯一 StateTree 组件；自动启动关闭，由 Initialize 显式设置资产。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	UPROPERTY()
	TObjectPtr<UCatFishingFightRunner> FightRunner;

	/** 客户端可观察的会话阶段、鱼种和参与人数；服务器是唯一写者。 */
	UPROPERTY(ReplicatedUsing=OnRep_Snapshot)
	FCatFishingSessionSnapshot Snapshot;

	/** 当前鱼种数据资产；只在服务器验证/捕获时读取，不复制为运行真相。 */
	UPROPERTY()
	TObjectPtr<UCatFishDefinition> FishDefinition;

	/** 当前钓手 Character 弱引用（接力转移后指向新钓手）；失效时服务终止本会话。 */
	TWeakObjectPtr<ACatCharacter> FisherCharacter;

	/** 当前钓手服务器私有 StableNetId（接力转移后为新钓手）。 */
	FString FisherStableNetId;

	/**
	 * 抛竿时冻结的装备组件（原始抛竿者的装备）：饵料预留、竿磨损、失败惩罚全部结算到它。
	 * 钓手接力转移不改变它——用谁的竿就磨谁的竿、扣抛竿时上的饵。
	 */
	TWeakObjectPtr<UCatEquipmentComponent> CastEquipment;

	/** 鱼运行态在会话创建时冻结的真实重量，单位千克。 */
	double FishWeightKilograms = 0.0;

	/** 由冻结重量计算的一次性表现缩放；水中 Encounter 与岸上 Pickup 共用，避免交接时尺寸跳变。 */
	double FishVisualScale = 1.0;

	/** HookedFight 的服务器私有参与者身份集合；巨鱼进入 NearShore 后保留到候选生成，普通鱼始终只含初始钓手。 */
	TSet<FString> FightParticipantIds;

	/** 参与者身份到 Character 弱引用；只用于掉线/倒地中断检查，不复制。 */
	TMap<FString, TWeakObjectPtr<ACatCharacter>> FightParticipantCharacters;

	/** 协作命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> AssistTerminalCache;

	/** 抢抄 RequestId 首次终态缓存；失败请求可重放，但只有成功会关闭整个会话。 */
	TMap<FString, FCatScoopResult> ScoopTerminalCache;

	/** Items 唯一写服务弱引用；World teardown 不被本 Actor 强持。 */
	TWeakObjectPtr<UCatItemsService> ItemsService;

	/** StateTree StartLogic 同步进入首状态时允许 EnterPhase 写入的短生命周期标记。 */
	bool bStartupInProgress = false;
	bool bPrepared = false;
	bool bPublished = false;

	/** 鱼是否已从水中 Encounter 交接为世界鱼；true 后所有新抢抄返回 AlreadyResolved。 */
	bool bCaptureResolved = false;

	/** 本会话失败预算是否已经提交；true 后任何第二种惩罚都返回 AlreadyResolved。 */
	bool bFailureBudgetCommitted = false;

	/** HookedFight 首次进入时的幂等 stamina 初始化事实；重复阶段事件不能补满已消耗体力。 */
	bool bFightStaminaInitialized = false;

	/** 本会话实际初始化或消耗过 stamina 的 Character；终态只恢复这些池。 */
	TSet<TWeakObjectPtr<ACatCharacter>> StaminaParticipantsTouched;
	/** 最后一次主动放下鱼竿的钓手；只用于允许其在地面姿态就近切线，不复制、不接管当前输入。 */
	TWeakObjectPtr<APlayerState> LastSuspendedFisherPlayerState;

	/** 本会话唯一失败预算终态；重放不再次扣特殊饵或鱼竿耐久。 */
	FCatFishingFailureResult FailureBudgetResult;
	FCatFishingAttemptSnapshot AttemptSnapshot;
	FCatFishSelectionContext FrozenSelectionContext;
	FCatFishSelectionResult FrozenSelectionResult;
	ECatFishSelectionResolution SelectionResolution = ECatFishSelectionResolution::None;
	/** 当前是本次抛竿的第几个咬钩机会；漏按后递增，使下一轮等待与选鱼拥有新的确定性随机流。 */
	uint32 BiteOpportunitySequence = 0;
	/** 从抛竿种子和 BiteOpportunitySequence 派生；等待采样、选鱼与后续搏斗共用。 */
	uint64 CurrentBiteRandomSeed = 0;
	/** 服务器是否仍接受当前真咬窗口的首次左键；计时器先关闸，再把 WindowExpired 交给 StateTree。 */
	bool bTrueBiteWindowAcceptingHook = false;
	FTimerHandle BiteWarningTimerHandle;
	FTimerHandle ProbeTimerHandle;
	FTimerHandle TrueBiteTimerHandle;
	FTimerHandle ExhaustedReelTimerHandle;
	int64 LastExhaustedReelInputSequence = 0;
	/** 当前竿尖表面投影；手持竿移动时按固定步更新，地面姿态自然保持不变。 */
	FVector ExhaustedReelTarget = FVector::ZeroVector;
	bool bHasExhaustedReelTarget = false;
	TMap<FGuid, FCatFishingCommandResult> HookTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CancelTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CutLineTerminalByRequest;
};
