#pragma once

#include "CoreMinimal.h"
#include "Growth/CatGrowthTypes.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/CatFishingUseResults.h"
#include "Data/CatFishSelectionTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingSession.generated.h"

class ACatCharacter;
class ACatFishingHookActor;
class ACatFishPickupActor;
class UCatEquipmentComponent;
class UCatFishDefinition;
class UCatFishContainerService;
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
	friend class FCatRunTransientCleanupTest;
	friend class FCatFishingR3BaitDistanceTest;
	friend class FCatFishingR3HoldTest;

public:
	/** 成长选择后更新已存在的个人等待/完美窗，不重抽鱼或重置机会。 */
	void RefreshGrowthFromAuthority(const ACatCharacter* Character, ECatGrowthOptionId OptionId, double AppliedDelta);
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
	/** 仅刷新尚未真咬的计时；次日重新采样，已有真咬和搏斗不受影响。 */
	void RefreshBiteAvailabilityFromAuthority();
	/**
	 * 进入试探期（钓鱼规则 §3.4:141 演出时序）：抽中瞬间就选鱼并生成按真鱼体型的鱼影，浮漂轻点，
	 * 停留解析后的试探时长（逐鱼可选覆盖，否则参数页区间）之后浮漂猛沉、才打开真咬响应窗。
	 * 2026-09-12 前是「Probe 只打开响应窗、鱼在合法左键之后才创建」，那样提竿前水里根本没有影子；
	 * 而 09-12 裁「竿强瞬断报废鱼竿」的前提正是玩家看得见那团黑影才谈得上知情的赌博（钓鱼规则 §4.2:176）。
	 */
	bool BeginProbeFromStateTree();
	FCatFishingCommandResult RequestHookFromAuthority(FGuid RequestId);
	FCatFishingCommandResult CancelFromAuthority(FGuid RequestId);
	FCatFishingCommandResult SetCancelHeldFromAuthority(AController* Controller, bool bHeld, FGuid RequestId);
	void ClearCancelHoldFromAuthority();
	/** 上钩后的主动止损写口；只接受当前钓手和精确 Revision，提交后鱼/饵丢失，不追加或退还鱼竿磨损。 */
	FCatFishingCommandResult CutLineFromAuthority(AController* RequestingController,
		const FCatFishingSessionCommandContext& Context);
	bool TryEnterHookedFightFromAuthority();
	bool SetReelingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bReeling);
	/** 主位右键写口；HookedFight / ExhaustedReel 共用 Runner，未满线时右键优先于收线并回体。 */
	bool SetSlackingFromAuthority(APlayerState* InputPlayerState, int64 InputSequence, bool bSlacking,
		const struct FCatFishingRodAimSample* AimRebaseSample = nullptr, FGuid RequestId = FGuid());
	/**
	 * 主位主动离竿：搏斗期进入无人值守松线，等口期清空当前钓手；都不结束会话。
	 * 钓鱼规则 §4.6（:230）"落水与猫体力归零不走这条出口"——落水路径先写终局，本入口再被调用时已是终态、直接返回。
	 */
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
	/**
	 * Condition确认主控危险落水：按钓鱼规则 §4.6（:222,228）写 CatInWater 终局——鱼逃、饵已扣、
	 * 嘴里原有的鱼保留，随后才释放其竿位控制。落水不再转入无人值守放线，那条出口只留给主位主动离竿。
	 * 物理旁人不进入本入口。
	 */
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

	/** Service显式授予主控后的会话接管；保留原扣饵记录和同一竿实例。 */
	bool ResumePrimaryControlFromAuthority(AController* NewFisherController);

	/**
	 * 主钓手按键发起或取消换人请求（多人钓鱼附篇 §2.4：按 E 发起、再按 E 取消、无时限挂起）。
	 * 只有当前主控能发起；返回 false 表示身份或阶段不对，状态一个字没动。
	 */
	bool ToggleHandoffRequestFromAuthority(AController* PrimaryController);

	/** 当前是否挂着换人请求；空 PlayerState＝没有。 */
	bool IsHandoffRequested() const { return Snapshot.HandoffRequestedByPlayerState != nullptr; }

	/** 谁发起的这次换人请求；替补接手时用它复核「发起者仍是当前主控」。 */
	APlayerState* GetHandoffRequesterPlayerState() const { return Snapshot.HandoffRequestedByPlayerState; }

	/** 清掉挂着的换人请求；主控换人、本竿结束与会话终止都会调用，重复调用无副作用。 */
	void ClearHandoffRequestFromAuthority(const TCHAR* Reason);

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
	friend class FCatGrowthRuntimeConsumersTest;
	friend class FCatGrowthWearDeliveryTest;
	friend class FCatRunFishCollectionHandoffTest;
	friend class FCatFishingPhysicalGripGraphTest;
	friend class FCatFishingOperatorRunnerIntegrationTest;
	friend class FCatFishingPhysicalCouplingTest;
	friend class FCatFishingCMCStabilityTest;
	friend class FCatFishingFormalPhysicalRunnerTest;
	friend class UCatFishingService;
	bool bFixedStepMutationBoundary = false;
	bool bResolvingWater = false;
	bool bWaterResolutionPending = false;
	bool bResolvingRevival = false;
	FTimerHandle CancelHoldTimer;
	TWeakObjectPtr<AController> CancelHoldController;
	uint32 CancelHoldControlEpoch = 0;
	FGuid CancelHoldRequestId;
	void CompleteCancelHoldFromAuthority();
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatRodSessionDurabilityTest;
	friend class FCatFishingBiteTimingWorldTest;
	friend class FCatFishingBaitTerminalConsumptionTest;
	friend class FCatFishingProbeDurationOverrideTest;
	friend class FCatFishingCatalogTimingDefaultsTest;
	friend class FCatFishingCatalogTimingOverridesTest;
	friend class FCatFishingCatalogTimingTimersTest;
	friend class FCatFishingPerfectLineProductionTest;
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

	/**
	 * 钓鱼规则 §4.2（:176,178）的强度检查序：①竿强瞬断 → ②碾压 → ③常规搏斗。
	 * 瞬时判定，只在搏斗开始、合力变动（换人/参与者进出）时调用一次，不是搏斗中的持续状态。
	 * 力量比较统一用 F_total（持竿猫当前力量，不随体力衰减）与已含完美削减的本场鱼力。
	 * 返回 true 表示①或②已经写下终局，调用方必须立刻停止推进常规搏斗。
	 */
	bool EvaluateStrengthCheckOrderFromAuthority(const TCHAR* Trigger);

	/** 读当前持竿猫的 F_total；体力归零不降力量，因此取 ASC 的 FishingStrength 而不是 Runner 的出力值。 */
	bool TryResolvePrimaryCombinedStrength(double& OutCombinedStrength) const;

	/** 读本场绑定鱼竿定义上的竿强度（静态配置，三档 25/60/210）；0 表示未裁，调用方不得据此瞬断。 */
	bool TryResolveRodStrength(double& OutRodStrength) const;

	/**
	 * 声明：真咬成立那一刻把本竿鱼漂的咬钩信号发出去——写进公开快照，够门槛的再走一次全场广播。
	 * 依据：鱼漂表「铃铛漂：咬钩铃响、全场可闻」；稳定度是三款漂唯一的既有差异字段，不为这条新增资产字段。
	 * 边界：读不到鱼漂定义时只记一行诊断、不广播；它不改变任何咬钩判定，纯表现事实。
	 */
	void PublishBiteSignalFromAuthority();

	/** 碾压达标：沿钓线向猫身后固定距离找可达干地，找不到才脚下兜底，交付待拾取鱼。 */
	bool FlingFishAshoreFromAuthority();

	/** 岸上世界鱼的唯一生成口；力竭拖岸与碾压甩岸共用，负责收口装备事务、隐藏水中 Encounter 并写 Landed 终态。 */
	bool SpawnLandedFishPickupFromAuthority(const FVector& SurfaceLocation, const FVector& GroundNormal,
		const TCHAR* DiagnosticReason);
	/** 两个成功收鱼出口共用；公共板子归上钩者，实物交接失败时绝不调用。 */
	void RecordRunCollectionCaptureFromAuthority(const class ACatFishPickupActor& Pickup) const;

	/** 冻结本竿的图鉴首次条件（水域＋时段＋天气）；三轴都来自咬钩成立那一刻，交给实物鱼随捕获一起归档。 */
	FCatCaptureConditionSnapshot BuildFrozenCaptureCondition() const;

	/**
	 * 把本次搏斗摸过这根竿的猫并入演出贡献名单（图鉴 §4:113,118-119，09-08 已裁）。
	 * 只喂贡献名单这一路：合力拉竿与抄网命中都不登记收集、不刷新个人最佳重量，
	 * 收集层归属仍是 CatchFisherStableNetId 一人，本函数不碰它。
	 * 抓握组只认对竿 Actor 的直接抓握；搏斗起始时刻由本会话提供，未开打（负值）时不收集。
	 */
	void AppendGripContributorsToParticipants(TArray<FString>& Participants) const;

	/** 进入 ExhaustedReel 时起算翻肚鱼苏醒时限；拖动中照走，上岸后不再苏醒。 */
	void ScheduleExhaustedRevivalTimerFromAuthority();

	/** 苏醒时限到点：鱼仍未上岸即苏醒逃跑写 Escaped 终局。 */
	void HandleExhaustedRevivalTimer();

	/** 非搏斗阶段重读主控属性；运行中的Runner拥有唯一费用和力量观察。 */
	bool RefreshFightSummary();

	/** 仅在失败路径重读摘要实际改变时发出高频复制更新。 */
	void PublishRefreshedFightSummaryIfChanged(bool bSummaryChanged);

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();
	/** 力竭鱼被真实拖过岸线并收到竿尖可达范围内后的交接口；落点是 Encounter 当前干地位置。 */
	bool SpawnExhaustedFishPickupFromAuthority(const FVector& SurfaceLocation);
	/** 抄网成功时生成世界鱼并立即附到抄手嘴部；不读取鱼体力，也不写入鱼护。 */
	bool SpawnScoopedFishPickupFromAuthority(ACatCharacter* ScoopingCharacter, APlayerState* ScoopingPlayerState,
		const FString& ScooperStableNetId);
	/** 渔获收口：确认消耗本场鱼饵，并按钓鱼规则 §4.4（:203）给鱼竿另扣 1 点基础磨损。 */
	bool CommitCatchEquipmentFromAuthority();
	void HandleBiteWarningTimer();
	/** 咬钩等待计时到点：只把「试探触发」送进 StateTree，选鱼与鱼影在 BeginProbeFromStateTree 里发生。 */
	void HandleProbeTimer();
	/** 试探期停留到点：浮漂由轻点转猛沉，打开真咬响应窗。 */
	void HandleProbeStayTimer();
	/** 真咬窗口的唯一打开口；试探期停留结束后由计时器调用，写 TrueBiteWindow 阶段并起响应计时。 */
	bool OpenTrueBiteWindowFromAuthority();
	/** 按秒解析试探：逐鱼正值优先，其次档位默认，两层缺配才取冻结种子的旧区间；非法值拒绝。 */
	bool TryResolveProbeDurationSeconds(double& OutProbeSeconds, const TCHAR** OutSource = nullptr) const;
	bool TryResolveTrueBiteWindowSeconds(double& OutSeconds, const TCHAR** OutSource = nullptr) const;
	double GetFisherGrowthMagnitude(ECatGrowthOptionId OptionId) const;
	void HandleTrueBiteWindowExpired();
	/** 咬钩计时到点那一刻冻结选择上下文、选鱼、生成鱼影 Encounter；饵的数量在真咬成立时才扣。 */
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
	/** 真咬成立时的鱼猫距离（cm）；负值表示尚未冻结，不能从试探表现回填。 */
	double TrueBiteDistanceCentimeters = -1.0;

	/**
	 * 本场冻结的鱼体力初值：鱼表「体力系数」× 实际重量（钓鱼规则 §4.1:160），入场时再乘完美削减。
	 * 归一化展示与苏醒判定共用这一个分母，避免和鱼种定额两套口径。
	 */
	double FishFightStaminaInitial = 0.0;

	/** 由冻结重量计算的一次性表现缩放；水中 Encounter 与岸上 Pickup 共用，避免交接时尺寸跳变。 */
	double FishVisualScale = 1.0;


	/** 抢抄 RequestId 首次终态缓存；失败请求可重放，但只有成功会关闭整个会话。 */
	TMap<FString, FCatScoopResult> ScoopTerminalCache;

	/** Items 唯一写服务弱引用；World teardown 不被本 Actor 强持。 */
	TWeakObjectPtr<UCatFishContainerService> ItemsService;

	/** StateTree StartLogic 同步进入首状态时允许 EnterPhase 写入的短生命周期标记。 */
	bool bStartupInProgress = false;
	bool bPrepared = false;
	bool bPublished = false;

	/** 鱼是否已从水中 Encounter 交接为世界鱼；true 后所有新抢抄返回 AlreadyResolved。 */
	bool bCaptureResolved = false;

	/** HookedFight 首次进入时已登记体力域归属的幂等事实；09-11 裁决④之后它不再触发任何补满。 */
	bool bFightStaminaInitialized = false;

	/**
	 * 本场负责的主控体力池归属；主控放下后解除。
	 * 09-11 裁决④删掉「进搏斗补满」与各终局的一次性回满之后，它只用于终局诊断：
	 * 搏斗体力是跨竿资源，会话结束不再把它写回上限。
	 */
	TWeakObjectPtr<ACatCharacter> StaminaOwner;

	/**
	 * 上一次跑强度检查序时观察到的 Snapshot.ActiveCombinedFishingStrength。
	 * 它只是「合力是否变动」的判据，不是检查里用的 F_total（后者不随体力衰减，见 §4.1:170）。
	 * 负值表示本场尚未查过。
	 */
	double LastStrengthCheckCombinedStrength = -1.0;

	/**
	 * 本次搏斗的起始服务器世界时间，秒；演出贡献名单向抓握组要「这一竿摸过竿的人」时的筛选窗口起点。
	 * 与 Snapshot.PhaseStartedServerTime 不是一回事：后者每次换阶段都重置，
	 * HookedFight→ExhaustedReel 一跨就会把前半场摸过竿的猫漏掉。
	 * 负值表示本场还没开打，此时一律不收集（世界时间恒为非负）。
	 */
	double FightStartedServerTimeSeconds = -1.0;

	FCatFishingAttemptSnapshot AttemptSnapshot;
	FCatFishSelectionContext FrozenSelectionContext;
	FCatFishSelectionResult FrozenSelectionResult;
	ECatFishSelectionResolution SelectionResolution = ECatFishSelectionResolution::None;
	/** 当前是本次抛竿的第几个咬钩机会；入夜收回后重回 Waiting 时递增，使下一轮等待与选鱼拥有新的确定性随机流。 */
	uint32 BiteOpportunitySequence = 0;
	/** 从抛竿种子和 BiteOpportunitySequence 派生；等待采样、选鱼与后续搏斗共用。 */
	uint64 CurrentBiteRandomSeed = 0;
	/** 服务器是否仍接受当前真咬窗口的首次左键；计时器先关闸，再按 §3.4 写鱼吐钩逃跑终局。 */
	bool bTrueBiteWindowAcceptingHook = false;
	FTimerHandle BiteWarningTimerHandle;
	/** 当前真咬成立时的鱼情；仅用于这次窗口跨夜后的选鱼，不是另一份世界昼夜状态。 */
	ECatEnvironmentTimeOfDay BiteTimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	ECatEnvironmentWeather BiteWeather = ECatEnvironmentWeather::Unknown;
	/** 咬钩等待计时；到点即抽鱼并进入试探期，不是试探期本身的长度。 */
	FTimerHandle ProbeTimerHandle;
	/** 试探期停留计时；到点浮漂猛沉、打开真咬响应窗。 */
	FTimerHandle ProbeStayTimerHandle;
	FTimerHandle TrueBiteTimerHandle;
	/** 本次咬钩机会的稳定键；剪影 Grant 用它去重，入夜收回后重新抛竿会换一个新的。 */
	FGuid CurrentBiteEncounterId;
	/** 翻肚鱼苏醒时限计时；进入 ExhaustedReel 起算，拖动中照走，鱼真正上岸或会话收口后清除。 */
	FTimerHandle ExhaustedRevivalTimerHandle;
	TMap<FGuid, FCatFishingCommandResult> HookTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CancelTerminalByRequest;
	TMap<FGuid, FCatFishingCommandResult> CutLineTerminalByRequest;
};
