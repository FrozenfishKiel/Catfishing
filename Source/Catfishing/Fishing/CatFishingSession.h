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
	/** 上钩后的主动止损写口；只接受当前钓手和精确 Revision，提交后鱼/饵丢失，不追加或退还鱼竿磨损。 */
	FCatFishingCommandResult CutLineFromAuthority(AController* RequestingController,
		const FCatFishingSessionCommandContext& Context);
	bool TryEnterHookedFightFromAuthority();
	bool SetReelingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bReeling);
	/** 主位右键写口；HookedFight / ExhaustedReel 共用 Runner，未满线时右键优先于收线并回体。 */
	bool SetSlackingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bSlacking,
		const struct FCatFishingRodAimSample* AimRebaseSample = nullptr, FGuid RequestId = FGuid());
	/** 主操作手离开竿位：搏斗期进入无人值守松线，等口期清空当前钓手；都不结束会话。 */
	void SuspendOperatorFromAuthority();
	/** 主控取得/释放的离散发布；旁人抓握不触发。 */
	void RefreshPrimaryControlFromAuthority();
	/** Runner只读发布本人力量与余额，旧反射摘要至多一人。 */
	void PublishPrimarySummaryFromAuthority(double Strength, double Stamina,
		double StaminaMaximum, bool bOperatorPresent);
	void BeginFixedStepMutationBoundary() { bFixedStepMutationBoundary = true; }
	void EndFixedStepMutationBoundary();
	bool IsFixedStepMutationBoundaryActive() const { return bFixedStepMutationBoundary; }
	bool IsFightRunnerRunning() const;
	void HandleFightRunnerStepFromAuthority(const FCatFightStepResult& Step, double FishStaminaRemaining,
		ECatFishMotionIntent MotionIntent);
	/** FightRunner/表现写入遇到不可恢复错误时终止会话；FailureStage 会进入日志，便于区分几何、装备、ASC 等故障。 */
	void HandleFightRunnerFailureFromAuthority(FName FailureStage = NAME_None);
	/** Condition确认主控危险落水，播放表现并释放其控制；物理旁人不进入本入口。 */
	void HandleCatEnteredDangerousWaterFromAuthority(double ImmersionDepthCentimeters,
		ACatCharacter* AffectedCharacter = nullptr);

	/** StateTree EnterPhase Task 的唯一阶段写入口；NearShore 必须提供水域内服务器目标，所有阶段只读取当前主控，终态启动有界销毁。 */
	FCatFishingPhaseResult EnterPhaseFromStateTree(ECatFishingPhase NewPhase);

	/** 旧协作命令兼容拒绝口；不能创建成员或取得主控。 */
	FCatDomainCommandResult SubmitFightAssist(AController* AssistingController, FGuid RequestId, int64 ExpectedRevision);

	/** 旧反射StateTree节点兼容拒绝口；费用只由Runner固定步提交。 */
	FCatDomainCommandResult ResolveFightExchangeFromStateTree(double FishStaminaCost, double ParticipantStaminaCost);

	/** 鱼上钩后可无视鱼的剩余体力抄取；服务器范围校验成功即生成世界鱼并直接进入抄手嘴叼状态。 */
	FCatScoopResult RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command);

	/** Service明确取回原拥有者控制后的会话恢复；禁止换成旁人，不迁移资源归属。 */
	bool ResumeOwnerControlFromAuthority(AController* NewFisherController);

	/** 当前主控私有身份；无人值守为空，服务用于索引。 */
	const FString& GetFisherStableNetIdForAuthority() const { return FisherStableNetId; }

	/** 局末或整场依赖失效时幂等写 Terminated；个人掉线/倒地走 Service 成员移除，不调用此入口。 */
	void TerminateSession(ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason);

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
	friend class FCatFishingPhysicalGripGraphTest;
	friend class FCatFishingPhysicalCouplingTest;
	friend class FCatFishingFormalPhysicalRunnerTest;
	friend class UCatFishingService;
	bool bFixedStepMutationBoundary = false;
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatRodSessionDurabilityTest;
	friend class FCatFishingBiteTimingWorldTest;
	friend class FCatFishingSessionReplicationContractTest;
	friend class FCatFishingSessionSnapshotVersionMutationRulesTest;
	friend class FCatFishingSessionTerminationOutcomeTest;
	friend class FCatFishingSessionScoopMouthCarryTest;
	friend class FCatFishingSessionRejectedFightSummaryPublicationTest;
	friend class FCatFishingOwnedRodLifecycleTest;
	friend class FCatFishingSessionLandedTerminalVisibilityTest;
	friend class FCatFishingExhaustedPickupHandoffTest;
	friend class FCatFishingSurfaceTraversalTest;
	friend class FCatFishingSessionLegacyLineBreakCompatibilityTest;
	friend class FCatFishingSessionOutcomePresentationTagTest;
	friend class FCatFishingSessionCutLineCommandTest;
	friend class FCatFishingSessionGroundedCutLineCommandTest;
	friend class FCatFishingServiceRodBoundSessionRoutingTest;
	friend class UCatFishingFightRunner;

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

	/** 非搏斗阶段重读主控属性；运行中的Runner拥有唯一费用和力量观察。 */
	bool RefreshFightSummary();

	/** 仅在失败路径重读摘要实际改变时发出高频复制更新。 */
	void PublishRefreshedFightSummaryIfChanged(bool bSummaryChanged);

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();
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
	/** 客户端体力到达诊断限频，不参与会话裁决。 */
	double NextStaminaReceivedDiagnosticSeconds = 0.0;
	/** 客户端耐久到达诊断按档位/终态过滤，不参与耐久裁决。 */
	int32 LastReceivedRodDurabilityBand = INDEX_NONE;
	bool bReceivedRodTerminal = false;

	/** 当前鱼种数据资产；只在服务器验证/捕获时读取，不复制为运行真相。 */
	UPROPERTY()
	TObjectPtr<UCatFishDefinition> FishDefinition;

	/** 当前主位身体弱引用；失效时Service解除控制，继续无人值守，不自动接任。 */
	TWeakObjectPtr<ACatCharacter> FisherCharacter;

	/** 当前钓手服务器私有 StableNetId（无人值守时为空）。 */
	FString FisherStableNetId;
	/** 抛钩时冻结的唯一钓手捕获归属；无人值守时仍保留，不授予当前控制权。 */
	FString CatchFisherStableNetId;

	/**
	 * 抛钩时冻结的会话协调组件：鱼饵预留属于原抛钩者，竿宿主/实例由其 FishingUseRecord 冻结。
	 * 接力不改物资归属；原身体销毁时 Service 将精确未结记录移入服务器托管组件并重绑定此入口。
	 */
	TWeakObjectPtr<UCatEquipmentComponent> CastEquipment;
	/** 仅用于装备磨损事务的去重顺序，不保存第二份鱼竿剩余耐久。 */
	int64 RodWearSequence = 0;

	/** 鱼运行态在会话创建时冻结的真实重量，单位千克。 */
	double FishWeightKilograms = 0.0;

	/** 由冻结重量计算的一次性表现缩放；水中 Encounter 与岸上 Pickup 共用，避免交接时尺寸跳变。 */
	double FishVisualScale = 1.0;


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

	/** HookedFight 首次进入时的幂等 stamina 初始化事实；重复阶段事件不能补满已消耗体力。 */
	bool bFightStaminaInitialized = false;

	/** 本场唯一负责的主控体力池；主控放下后解除，终局不能恢复旁人。 */
	TWeakObjectPtr<ACatCharacter> StaminaOwner;
	/** 最后一次主动放下鱼竿的钓手；只用于允许其在地面姿态就近切线，不复制、不接管当前输入。 */
	TWeakObjectPtr<APlayerState> LastSuspendedFisherPlayerState;

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
	TMap<FGuid, FCatFishingCommandResult> HookTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CancelTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CutLineTerminalByRequest;
};
