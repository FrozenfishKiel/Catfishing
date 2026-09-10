#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/CatFishingUseResults.h"
#include "Data/CatFishSelectionTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingSession.generated.h"

class ACatCharacter;
class ACatFishingHookActor;
class ACatFishPickupActor;
class UCatEquipmentComponent;
class UCatFishDefinition;
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
	/** 两阶段抛竿提交的逻辑启动入口；只在 Prepare 成功且尚未发布时启动 StateTree，失败不会公开半成品会话。 */
	bool StartPreparedSessionLogicFromAuthority();
	/** 两阶段抛竿提交的公开发布入口；StateTree 已运行后才复制 Snapshot，让客户端只看到可推进会话。 */
	bool PublishPreparedSessionFromAuthority();
	/** 两阶段抛竿失败回滚入口；仅销毁尚未发布的预备会话，已发布会话必须走正式终止流程。 */
	void AbortPreparedSessionFromAuthority();
	/** Waiting 阶段请求安排下一次咬钩探测；只设置计时器和随机种子，不创建鱼或提交库存。 */
	bool ScheduleWaitingProbeFromStateTree();
	/** Probe 状态只打开响应窗口，不选鱼、不生成鱼、不扣饵；鱼只在合法 RequestHook 到达后创建。 */
	bool OpenTrueBiteWindowFromStateTree();
	/** 主位提竿命令写口；按 RequestId 幂等处理空钩、真咬选鱼和进入搏斗，不让客户端直接选鱼。 */
	FCatFishingCommandResult RequestHookFromAuthority(FGuid RequestId);
	/** 主位取消命令写口；只在允许取消的阶段终止会话并释放装备，不重算已提交的咬钩或搏斗结果。 */
	FCatFishingCommandResult CancelFromAuthority(FGuid RequestId);
	/** 上钩后的主动止损写口；只接受当前钓手和精确 Revision，提交后鱼/饵丢失，不追加或退还鱼竿磨损。 */
	FCatFishingCommandResult CutLineFromAuthority(AController* RequestingController,
		const FCatFishingSessionCommandContext& Context);
	/** 从真咬命中进入 HookedFight 的服务器入口；初始化 Runner 和参与者摘要，重复调用保持幂等。 */
	bool TryEnterHookedFightFromAuthority();
	/** 主位左键收线写口；按输入序号过滤失效边沿，并把收线状态交给当前阶段的 Runner 或力竭收近逻辑。 */
	bool SetReelingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bReeling);
	/** 主位右键写口；HookedFight / ExhaustedReel 共用 Runner，正常右键优先于收线并回体。 */
	bool SetSlackingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bSlacking);
	/** 主操作手离开竿位：搏斗期进入无人值守松线，等口期清空当前钓手；都不结束会话。 */
	void SuspendOperatorFromAuthority();
	/** 只读查询 FightRunner 是否仍在推进固定步；服务层用它区分可接力搏斗和已停机阶段。 */
	bool IsFightRunnerRunning() const;
	/** Runner 固定步回调；先把累计磨损写回本会话绑定的鱼竿实例，再发布公开搏斗快照并处理终局。 */
	void HandleFightRunnerStepFromAuthority(const FCatFightStepResult& Step, double FishStaminaRemaining,
		ECatFishMotionIntent MotionIntent, double RodDurabilityRemaining = -1.0);
	/** FightRunner/表现写入遇到不可恢复错误时终止会话；FailureStage 会进入日志，便于区分几何、装备、ASC 等故障。 */
	void HandleFightRunnerFailureFromAuthority(FName FailureStage = NAME_None);
	/** Condition 确认当前钓手脚点进入危险水深后的唯一落水终局入口。 */
	void HandleCatEnteredDangerousWaterFromAuthority(double ImmersionDepthCentimeters);

	/** StateTree EnterPhase Task 的唯一阶段写入口；NearShore 必须提供水域内服务器目标，HookedFight/NearShore 保留合法参与者，其他阶段重置为钓手，终态启动有界销毁。 */
	FCatFishingPhaseResult EnterPhaseFromStateTree(ECatFishingPhase NewPhase);

	/** 巨鱼 HookedFight 中按统一参战能力谓词登记一次协作者；非 Active、倒地、力量/体力非正、阶段不符或重复 RequestId 都不增加参与集合。 */
	FCatDomainCommandResult SubmitFightAssist(AController* AssistingController, FGuid RequestId, int64 ExpectedRevision);

	/** StateTree 失败节点提交本会话唯一物资惩罚；丢特殊饵与伤竿互斥且同会话只允许一次。 */
	FCatFishingFailureResult CommitFailureBudgetFromStateTree(ECatFishingFailurePenalty Penalty);

	/** StateTree 在唯一已裁的“重试耗尽”逃鱼终态调用；Collection 生成剪影 Grant 后终止会话且不创建实物鱼。 */
	FCatDomainCommandResult ResolveRetryExhaustedEscapeFromStateTree();

	/** 鱼上钩后可无视鱼的剩余体力抄取；服务器范围校验成功即生成世界鱼并直接进入抄手嘴叼状态。 */
	FCatScoopResult RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command);

	/**
	 * 多人接力（规格：用别人的竿继续钓）：把会话的"钓手"身份转移给新操作者。
	 * 允许在等待/试探/真咬及 HookedFight 转移；搏斗接力会转交 Runner 的 ASC、力量、体力和输入序号域。
	 * 饵料冻结仍结算到抛竿时冻结的 CastEquipment；竿磨损跟随 Begin 记录里的世界鱼竿归属库存，体力和力量随新钓手。
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
	friend class FCatRodSessionDurabilityTest;
	friend class FCatFishingBiteTimingWorldTest;
	friend class FCatFishingSessionReplicationContractTest;
	friend class FCatFishingSessionSnapshotVersionMutationRulesTest;
	friend class FCatFishingSessionTerminationOutcomeTest;
	friend class FCatFishingSessionScoopMouthCarryTest;
	friend class FCatFishingSessionRejectedFightSummaryPublicationTest;
	friend class FCatFishingSessionLandedTerminalVisibilityTest;
	friend class FCatFishingExhaustedPickupHandoffTest;
	friend class FCatFishingSurfaceTraversalTest;
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

	/** 只有切线/猫落水拥有当前猫表现事件；其余终局返回空 Tag，不借用错误表现。 */
	static FGameplayTag ResolveTerminalFisherPresentationTag(ECatFishingOutcome Outcome);

	/** 用 FishingService 的统一权威谓词重读参与者，更新公开人数、合计 FishingStrength 与 FightStamina。 */
	bool RefreshFightSummary();
	/** Runner 登记/释放实际被本会话扣过体力的角色，终态只恢复仍归本会话所有的池。 */
	void RegisterFightStaminaParticipantFromAuthority(ACatCharacter* Character);
	/** Runner 在参与者离开或失效时移除体力恢复候选，避免终态恢复落到已离场角色。 */
	void UnregisterFightStaminaParticipantFromAuthority(ACatCharacter* Character);

	/** 仅在失败路径重读摘要实际改变时发出高频复制更新。 */
	void PublishRefreshedFightSummaryIfChanged(bool bSummaryChanged);

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();
	/** 进入鱼已力竭后的收近阶段；冻结岸线目标、同步侧翻表现，并启动独立固定步。 */
	bool BeginExhaustedReelFromAuthority();
	/** 力竭收近固定步；只在玩家仍按住收线时推进鱼向冻结目标移动，到达后生成岸上拾取物。 */
	void HandleExhaustedReelStep();
	/** 力竭收近阶段同步鱼嘴 Hook 与绷紧鱼线表现；避免客户端继续显示搏斗末帧的松线。 */
	bool PublishExhaustedReelLineFromAuthority(const FVector& FishWorldLocation);
	/** 解析力竭鱼最终停靠的竿尖表面投影；Z 取水面与地面较高者，防止岸坡穿插。 */
	bool TryResolveExhaustedReelTarget(FVector& OutTarget) const;
	/** 力竭鱼到达冻结表面点后生成正式世界拾取物；成功后收口装备、隐藏 Encounter 并写入捕获终态。 */
	bool SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation);
	/** 抄网成功时生成世界鱼并立即附到抄手嘴部；不读取鱼体力，也不写入鱼护。 */
	bool SpawnScoopedFishPickupFromAuthority(ACatCharacter* ScoopingCharacter, APlayerState* ScoopingPlayerState,
		const FString& ScooperStableNetId);
	/** 捕获终态共用的装备收口入口；只提交本会话冻结饵料，鱼竿磨损已在固定步写回。 */
	bool CommitCatchEquipmentFromAuthority();
	/** 预警计时器回调；仍处于 Waiting 时只切换浮漂表现，不推进阶段或创建鱼。 */
	void HandleBiteWarningTimer();
	/** Probe 计时器回调；仍处于 Waiting 时向 StateTree 发送试探事件，失效计时器直接忽略。 */
	void HandleProbeTimer();
	/** 真咬窗口超时回调；关闭本次响应窗口并交回 StateTree，下一轮等待会重新派生咬钩机会。 */
	void HandleTrueBiteWindowExpired();
	/** 真咬窗口内收到合法左键后，冻结选择上下文、选鱼、生成 Encounter 并提交饵料。 */
	FCatFishSelectionCommitResult ResolveHookSelectionFromAuthority();
	/** 只读解析近岸阶段鱼的位置；失败时清空输出，避免 StateTree 拿到上一帧脏空间结果。 */
	bool TryReadNearShoreFishSpatial(FCatWaterSpatialResult& OutSpatial) const;

	/** 当前会话唯一 StateTree 组件；自动启动关闭，由 Initialize 显式设置资产。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	/** 本会话拥有的权威搏斗 Runner；HookedFight/力竭收近读取它推进数值，StateTree 节点只启动或等待它。 */
	UPROPERTY()
	TObjectPtr<UCatFishingFightRunner> FightRunner;

	/** 客户端可观察的会话阶段、鱼种和参与人数；服务器是唯一写者。 */
	UPROPERTY(ReplicatedUsing=OnRep_Snapshot)
	FCatFishingSessionSnapshot Snapshot;
	/** 客户端体力到达诊断限频，不参与会话裁决。 */
	double NextStaminaReceivedDiagnosticSeconds = 0.0;
	/** 客户端耐久到达诊断按档位/终态过滤，不参与耐久裁决。 */
	int32 LastReceivedRodDurabilityBand = INDEX_NONE;
	/** 客户端是否已经记录过鱼竿耐久终态；仅用于诊断去重，服务器耐久仍在库存实例上。 */
	bool bReceivedRodTerminal = false;

	/** 当前鱼种数据资产；只在服务器验证/捕获时读取，不复制为运行真相。 */
	UPROPERTY()
	TObjectPtr<UCatFishDefinition> FishDefinition;

	/** 当前钓手 Character 弱引用（接力转移后指向新钓手）；失效时服务终止本会话。 */
	TWeakObjectPtr<ACatCharacter> FisherCharacter;

	/** 当前钓手服务器私有 StableNetId（接力转移后为新钓手）。 */
	FString FisherStableNetId;

	/**
	 * 抛竿时冻结的操作者装备组件：饵料冻结和失败惩罚结算到它；鱼竿磨损由 Begin 记录中的鱼竿归属库存接收。
	 * 钓手接力转移不改变它，保证扣的是抛竿时上的饵而不是接手者之后切换的饵。
	 */
	TWeakObjectPtr<UCatEquipmentComponent> CastEquipment;
	/** 仅用于装备磨损事务的去重顺序，不保存第二份鱼竿剩余耐久。 */
	int64 RodWearSequence = 0;

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

	/** StateTree StartLogic 同步进入首状态时允许 EnterPhase 写入的短生命周期标记。 */
	bool bStartupInProgress = false;
	/** Prepare 阶段是否已经写入完整服务器上下文；未准备完成的会话不能启动或发布。 */
	bool bPrepared = false;
	/** 会话是否已经公开给客户端；发布后失败必须走终止流程，不能再按预备对象直接销毁。 */
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

	/** 本会话唯一失败预算终态；重放只返回首次预算结果。 */
	FCatFishingFailureResult FailureBudgetResult;
	/** 抛竿时冻结的尝试快照；后续提竿、选鱼和诊断都读取这份上下文，不重新读取客户端输入。 */
	FCatFishingAttemptSnapshot AttemptSnapshot;
	/** 真咬命中时冻结的选鱼上下文；Collection、窝料和水域事实都从这里进入一次性选鱼。 */
	FCatFishSelectionContext FrozenSelectionContext;
	/** 真咬命中后得到的选鱼结果；生成 Encounter、终态诊断和剪影资格都读取它。 */
	FCatFishSelectionResult FrozenSelectionResult;
	/** 当前选鱼流程的解析状态；用于区分未选、已选中和逃鱼路径，避免重复生成鱼。 */
	ECatFishSelectionResolution SelectionResolution = ECatFishSelectionResolution::None;
	/** 当前是本次抛竿的第几个咬钩机会；漏按后递增，使下一轮等待与选鱼拥有新的确定性随机流。 */
	uint32 BiteOpportunitySequence = 0;
	/** 从抛竿种子和 BiteOpportunitySequence 派生；等待采样、选鱼与后续搏斗共用。 */
	uint64 CurrentBiteRandomSeed = 0;
	/** 服务器是否仍接受当前真咬窗口的首次左键；计时器先关闸，再把 WindowExpired 交给 StateTree。 */
	bool bTrueBiteWindowAcceptingHook = false;
	/** 咬钩预警计时器；阶段变化、终态或销毁会让回调自行失效，不拥有玩法状态。 */
	FTimerHandle BiteWarningTimerHandle;
	/** Waiting 到 Probe 的计时器；只触发 StateTree 事件，不直接创建鱼。 */
	FTimerHandle ProbeTimerHandle;
	/** 真咬响应窗口计时器；超时只关闭本次机会，整场架杆会话继续等待下一轮。 */
	FTimerHandle TrueBiteTimerHandle;
	/** 力竭收近阶段的固定步计时器；终态、销毁和阶段失败都会清理它，避免失效 Step 继续移动鱼。 */
	FTimerHandle ExhaustedReelTimerHandle;
	/** 力竭收近阶段最近接受的左键输入序号；防止失效边沿在跨阶段后重新打开收线。 */
	int64 LastExhaustedReelInputSequence = 0;
	/** 鱼力竭瞬间冻结的竿尖表面投影；Z 取水面与地面较高者，后续目标固定，不能重新查询或改写。 */
	FVector ExhaustedReelTarget = FVector::ZeroVector;
	/** 是否已经冻结力竭收近目标；没有目标时固定步 fail-closed，避免把鱼拖向零点。 */
	bool bHasExhaustedReelTarget = false;
	/** 提竿命令按 RequestId 记录首次终态；输入重发时返回同一结果，不重复选鱼或重启搏斗。 */
	TMap<FGuid, FCatFishingCommandResult> HookTerminalByRequest;
	/** 取消命令按 RequestId 记录首次终态；重复取消不会再次向 StateTree 发送中断事件。 */
	TMap<FGuid, FCatFishingCommandResult> CancelTerminalByRequest;
	/** 主动切线命令按 RequestId 记录首次终态；重放不会再次抢占终态写口或改写磨损结果。 */
	TMap<FGuid, FCatFishingCommandResult> CutLineTerminalByRequest;
};
