#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/CatFishingTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Data/CatFishSelectionTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingSession.generated.h"

class ACatCharacter;
class ACatFishingHookActor;
class UCatFishDefinition;
class UCatItemsService;
class UCatFishingFightRunner;
class UStateTreeComponent;
struct FCatFightStepResult;
class FCatFishingSessionReplicationContractTest;
class FCatFishingSessionSnapshotVersionMutationRulesTest;
class FCatFishingSessionTerminationOutcomeTest;
class FCatFishingSessionExistingCaptureReconciliationTest;
class FCatFishingSessionRejectedFightSummaryPublicationTest;

DECLARE_MULTICAST_DELEGATE(FCatFishingSessionSnapshotChanged);

/** 一次服务器钓鱼长流程宿主；StateTree 拥有阶段拓扑，Actor 只执行阶段副作用与短事务。 */
UCLASS()
class CATFISHING_API ACatFishingSession : public AActor
{
	GENERATED_BODY()

public:
	/** 创建唯一 StateTree 组件、开启只读 Snapshot 复制并关闭 Tick。 */
	ACatFishingSession();

	/** 注册公开 Snapshot 复制；私有身份、鱼资产和容器服务引用不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** authority 注入当前钓手、鱼定义、目标鱼护与水域快照并启动 ST_FishingSession；任一 gate 失败返回 false。 */
	bool InitializeSession(FGuid InFishingSessionId, FGuid InCastAttemptId, AController* FisherController,
		ACatCharacter* FisherCharacter, UCatFishDefinition* FishDefinition, FGuid FisherGuardContainerId,
		double FishWeightKilograms, const FCatWaterRegionHandle& WaterRegion);
	bool PrepareSessionFromAuthority(const FCatFishingAttemptSnapshot& Attempt, AController* FisherController,
		ACatCharacter* FisherCharacter, ACatFishingHookActor* HookActor);
	bool StartPreparedSessionLogicFromAuthority();
	bool PublishPreparedSessionFromAuthority();
	void AbortPreparedSessionFromAuthority();
	bool ScheduleWaitingProbeFromStateTree();
	FCatFishSelectionCommitResult ResolveProbeSelectionFromStateTree();
	FCatFishingCommandResult RequestHookFromAuthority(FGuid RequestId);
	FCatFishingCommandResult CancelFromAuthority(FGuid RequestId);
	bool TryEnterHookedFightFromAuthority();
	bool SetReelingFromAuthority(int64 InputSequence, bool bReeling);
	bool IsFightRunnerRunning() const;
	void HandleFightRunnerStepFromAuthority(const FCatFightStepResult& Step, double FishStaminaRemaining,
		ECatFishMotionIntent MotionIntent);
	void HandleFightRunnerFailureFromAuthority();

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

	/** NearShore 的首个合法 Compare-and-Commit；实物只归抄手，配置成像事件时为全部合法搏斗参与者先建齐 Planned 事实再投递，Resolved 后启动有界销毁。 */
	FCatScoopResult RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command);

	/** 掉线、倒地、局末或依赖失效时幂等写 Terminated、停树并释放参与者；Resolved 保持捕获终态，两种终态都只保留配置的复制窗口后销毁 Actor。 */
	void TerminateSession(ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason);

	/** 判断 Character 是否为钓手或已登记协作者；FishingService 用于生命周期中断。 */
	bool InvolvesCharacter(const ACatCharacter* Character) const;

	/** 提供当前复制阶段和协作摘要供服务/表现读取；私有参与身份、鱼资产和事务缓存不会随返回值泄露。 */
	const FCatFishingSessionSnapshot& GetSnapshot() const;

	/** 本机完整 Snapshot 变化通知；订阅者只需重新读取 GetSnapshot。 */
	FCatFishingSessionSnapshotChanged OnSnapshotChanged;

	/** 判断会话是否已进入 Resolved/Terminated 终态；FishingService 据此释放该钓手的单活跃槽位。 */
	bool IsTerminal() const;

protected:
	/** World 销毁时停止仍运行的 StateTree并清弱引用，随后交给父类。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FCatFishingSessionReplicationContractTest;
	friend class FCatFishingSessionSnapshotVersionMutationRulesTest;
	friend class FCatFishingSessionTerminationOutcomeTest;
	friend class FCatFishingSessionExistingCaptureReconciliationTest;
	friend class FCatFishingSessionRejectedFightSummaryPublicationTest;

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

	/** 只接受当前会话完整且与快照一致的 Items committed 事实，随后同步写捕获终态。 */
	bool ReconcileCommittedCapture(const FCatCaptureCommittedResult& Committed);

	/** 验证 Items 已提交 DTO 可安全作为当前会话唯一捕获事实。 */
	bool IsCommittedCaptureForCurrentSession(const FCatCaptureCommittedResult& Committed) const;

	/** 用 FishingService 的统一权威谓词重读参与者，更新公开人数、合计 FishingStrength 与 FightStamina。 */
	bool RefreshFightSummary();

	/** 仅在失败路径重读摘要实际改变时发出高频复制更新。 */
	void PublishRefreshedFightSummaryIfChanged(bool bSummaryChanged);

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();
	void HandleProbeTimer();
	void HandleTrueBiteWindowExpired();
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

	/** 初始钓手 Character 弱引用；失效时服务终止本会话。 */
	TWeakObjectPtr<ACatCharacter> FisherCharacter;

	/** 初始钓手服务器私有 StableNetId。 */
	FString FisherStableNetId;

	/** 初始钓手个人鱼护 ID；钓手之外的抢抄者必须提交自己的 Guard ID。 */
	FGuid FisherGuardContainerId;

	/** 鱼运行态在会话创建时冻结的真实重量，单位千克。 */
	double FishWeightKilograms = 0.0;

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

	/** 捕获是否已经由某个合法请求不可逆提交；true 后所有新抢抄返回 AlreadyResolved。 */
	bool bCaptureResolved = false;

	/** 本会话失败预算是否已经提交；true 后任何第二种惩罚都返回 AlreadyResolved。 */
	bool bFailureBudgetCommitted = false;

	/** HookedFight 首次进入时的幂等 stamina 初始化事实；重复阶段事件不能补满已消耗体力。 */
	bool bFightStaminaInitialized = false;

	/** 本会话实际初始化或消耗过 stamina 的 Character；终态只恢复这些池。 */
	TSet<TWeakObjectPtr<ACatCharacter>> StaminaParticipantsTouched;

	/** 本会话唯一失败预算终态；重放不再次扣特殊饵或鱼竿耐久。 */
	FCatFishingFailureResult FailureBudgetResult;
	FCatFishingAttemptSnapshot AttemptSnapshot;
	FCatFishSelectionContext FrozenSelectionContext;
	FCatFishSelectionResult FrozenSelectionResult;
	ECatFishSelectionResolution SelectionResolution = ECatFishSelectionResolution::None;
	FTimerHandle ProbeTimerHandle;
	FTimerHandle TrueBiteTimerHandle;
	TMap<FGuid, FCatFishingCommandResult> HookTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CancelTerminalByRequest;
};
