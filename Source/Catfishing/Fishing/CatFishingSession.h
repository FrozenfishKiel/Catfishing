#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/CatFishingTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Math/RandomStream.h"
#include "CatFishingSession.generated.h"

class ACatCharacter;
class UCatFishDefinition;
class UCatItemsService;
class UStateTreeComponent;

/** StateTree 搏斗节点每帧推进一次后拿到的结果；节点只看它决定 Running/Succeeded/Failed，不自己读会话内部状态。 */
USTRUCT()
struct FCatFishingFightStepResult
{
	GENERATED_BODY()

	/** 推进是否被接受；不在 HookedFight、会话未初始化或钓手身体/ASC/装备丢失时为拒绝原因，接受时为 None。 */
	UPROPERTY()
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;

	/** 推进后的搏斗终局；None 表示还在打。 */
	UPROPERTY()
	ECatFishingFightOutcome Outcome = ECatFishingFightOutcome::None;
};

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

	/** authority 注入当前钓手、鱼定义、目标鱼护与 Boundary 冻结的 EncounterSpec 并启动 ST_FishingSession；任一 gate 失败返回 false。 */
	bool InitializeSession(AController* FisherController, ACatCharacter* FisherCharacter, UCatFishDefinition* FishDefinition,
		FGuid FisherGuardContainerId, const FCatFishingEncounterSpec& EncounterSpec);

	/**
	 * StateTree EnterPhase Task 的唯一阶段写入口；TrueBiteWindow 记下服务器进入时刻供完美中鱼判定，HookedFight 冻结搏
	 * 斗参数并开局（参数缺失拒绝进入），NearShore 由会话自己用钓手位置与冻结水域算出服务器近岸目标（v0 近似），算不出
	 * 就拒绝进入，HookedFight/NearShore 保留合法参与者，其他阶段重置为钓手，终态启动有界销毁。
	 */
	FCatFishingPhaseResult EnterPhaseFromStateTree(ECatFishingPhase NewPhase);

	/**
	 * v0 近岸目标近似：取冻结水域 AABB 内离钓手最近的点作为"鱼被遛到岸边"的替身；钓手位置或水域半尺寸非法时返回
	 * false 且不写输出。正式鱼运行态（D/L 距离模型）接入后应改为读鱼的真实位置，见 Docs/Development/工程自补决策记
	 * 录.md D-12。
	 */
	static bool TryComputeNearShoreTargetV0(const FVector& FisherWorldLocation,
		const FCatWaterRegionSnapshot& WaterRegion, FVector& OutTargetWorldLocation);

	/** 巨鱼 HookedFight 中按统一参战能力谓词登记一次协作者；非 Active、倒地、力量/体力非正、阶段不符或重复 RequestId 都不增加参与集合。 */
	FCatDomainCommandResult SubmitFightAssist(AController* AssistingController, FGuid RequestId, int64 ExpectedRevision);

	/**
	 * StateTree HookedFight 节点每帧调用的唯一搏斗推进口：按飞书 4.3 判定表 / 4.4 消耗战 / D-L 模型推进 DeltaSeconds，
	 * 把猫体力写回钓手 ASC、把竿磨损按秒批量提交给钓手 EquipmentComponent，并把公开字段拷进 Snapshot。
	 * 返回的 Outcome 一旦不是 None 就是终局：断竿/拖下水让节点 Failed（树进 Terminated，鱼逃），碾压/翻肚/遛到岸边让
	 * 节点 Succeeded（树进 NearShore）。
	 */
	FCatFishingFightStepResult ApplyFightExchangeFromStateTree(double DeltaSeconds);

	/**
	 * 钓手（只认初始钓手身份）上报当前按住的遛鱼操作：拖/松/无。HookedFight 内它决定下一帧走判定表哪一行；
	 * TrueBiteWindow 内第一次"拖"就是提竿：按服务器时间戳判断是否在 1 秒完美窗内，并向 StateTree 发 HookSet 事件让资产转进 HookedFight。
	 * 其他阶段只记录意图不产生效果（试探期提竿=空竿，飞书说不损饵，这里暂不结束会话）。
	 */
	FCatDomainCommandResult SubmitFightIntent(AController* Controller, ECatFishingFightIntent Intent);

	/** StateTree 失败节点提交本会话唯一物资惩罚；丢特殊饵与伤竿互斥且同会话只允许一次。 */
	FCatFishingFailureResult ApplyFailureBudgetFromStateTree(ECatFishingFailurePenalty Penalty);

	/**
	 * NearShore 的首个合法 Compare-and-Commit；实物只归抄手，配置成像事件时为全部合法搏斗参与者先建齐 Planned 事实再
	 * 投递，Resolved 后启动有界销毁。
	 */
	FCatScoopResult RequestScoop(AController* ScoopingController, const FCatScoopCommand& Command);

	/** 掉线、倒地、局末或依赖失效时幂等写 Terminated、停树并释放参与者；Resolved 保持捕获终态，两种终态都只保留配置的复制窗口后销毁 Actor。 */
	void TerminateSession(const TCHAR* Reason);

	/** 判断 Character 是否为钓手或已登记协作者；FishingService 用于生命周期中断。 */
	bool InvolvesCharacter(const ACatCharacter* Character) const;

	/** 提供当前复制阶段和协作摘要供服务/表现读取；私有参与身份、鱼资产和事务缓存不会随返回值泄露。 */
	const FCatFishingSessionSnapshot& GetSnapshot() const;

	/** 本会话在 Cast 时冻结的咬钩间隔（秒）；ST_FishingSession 的 Probe 等待节点读它决定何时进入 TrueBiteWindow，0 表示没有合法间隔。 */
	double GetBiteIntervalSeconds() const { return BiteIntervalSeconds; }

	/** 判断会话是否已进入 Resolved/Terminated 终态；FishingService 据此释放该钓手的单活跃槽位。 */
	bool IsTerminal() const;

protected:
	/** World 销毁时停止仍运行的 StateTree并清弱引用，随后交给父类。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 发布 Snapshot 并请求立即网络更新；只由 authority 调用。 */
	void PublishSnapshot();

	/** 用 FishingService 的统一权威谓词重读参与者，更新公开人数、合计 FishingStrength 与 FightStamina。 */
	void RefreshFightSummary();

	/** 在终态快照强制网络更新后设置有界 Actor lifespan；配置缺失时立即销毁以免无界泄漏。 */
	void ScheduleTerminalDestroy();

	/** 从钓手当前装备读出鱼竿定义并冻结强度与 L_max；没有可用（未断、运行目录合法）的竿返回 false，会话初始化据此 fail-closed。 */
	bool TryFreezeFisherRod();

	/** 进入 HookedFight 时冻结搏斗参数（猫力、鱼力=重量×K、游速系数、P_base、体力上限、近岸距离）并按完美中鱼削减后开局；任一输入缺失返回 false。 */
	bool BeginFightOnPhaseEntry();

	/** 把累计但尚未提交的竿磨损一次性提交给钓手 EquipmentComponent；bBreakRod 为 true 时把剩余耐久整段磨光，让 Equipment 把 RodBroken 写成 true。 */
	void FlushPendingRodWear(bool bBreakRod);

	/** 把搏斗运行态里允许客户端看到的字段拷进 Snapshot（D/L/鱼状态/鱼体力/挣扎/意图/终局）；不递增 Revision，也不强制网络更新。 */
	void CopyFightStateToSnapshot();

	/** 当前会话唯一 StateTree 组件；自动启动关闭，由 Initialize 显式设置资产。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	/** 客户端可观察的会话阶段、鱼种和参与人数；服务器是唯一写者。 */
	UPROPERTY(Replicated)
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

	/** 会话创建时唯一命中的水域快照；不作为近岸几何的替代真相。 */
	FCatWaterRegionSnapshot WaterRegionSnapshot;

	/** Cast 时按落点窝料冻结的咬钩间隔（秒）；来自 EncounterSpec，之后窝料变化不改它（计时器挂浮漂级）。 */
	double BiteIntervalSeconds = 0.0;

	/** Cast 时冻结的猫到浮漂落点距离（米），也就是搏斗开局的 D₀；它由当前鱼漂的射程与精准度决定，换漂就会变。 */
	double InitialFishDistanceMeters = 0.0;

	/** 进入 NearShore 时服务器算出的近岸目标世界位置（v0：钓手面前水域 AABB 上最近的点）；客户端 ScoopWorldLocation 与资产字面量都不能覆盖它。 */
	FVector NearShoreTargetWorldLocation = FVector::ZeroVector;

	/** 当前 NearShore 是否持有通过水域 AABB 校验的服务器目标；默认 false 使算不出目标时捕获 fail-closed。 */
	bool bHasNearShoreTarget = false;

	/** HookedFight 的服务器私有参与者身份集合；巨鱼进入 NearShore 后保留到候选生成，普通鱼始终只含初始钓手。 */
	TSet<FString> FightParticipantIds;

	/** 参与者身份到 Character 弱引用；只用于掉线/倒地中断检查，不复制。 */
	TMap<FString, TWeakObjectPtr<ACatCharacter>> FightParticipantCharacters;

	/** 协作命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> AssistTerminalCache;

	/** 协作命令首次终态对应的业务载荷签名；同 RequestId 换 Session Revision 会被拒绝。 */
	TMap<FString, FString> AssistPayloadByKey;

	/** 抢抄 RequestId 首次终态缓存；失败请求可重放，但只有成功会关闭整个会话。 */
	TMap<FString, FCatScoopResult> ScoopTerminalCache;

	/** 抢抄首次终态对应的业务载荷签名；同 RequestId 换鱼护、Session Revision 或命中位置会被拒绝。 */
	TMap<FString, FString> ScoopPayloadByKey;

	/** Items 唯一写服务弱引用；World teardown 不被本 Actor 强持。 */
	TWeakObjectPtr<UCatItemsService> ItemsService;

	/** StateTree StartLogic 同步进入首状态时允许 EnterPhase 写入的短生命周期标记。 */
	bool bStartupInProgress = false;

	/** 捕获是否已经由某个合法请求不可逆提交；true 后所有新抢抄返回 AlreadyResolved。 */
	bool bCaptureResolved = false;

	/**
	 * 进入 HookedFight 时冻结的搏斗不变量（猫力、鱼基础力量、竿强度、L_max、游速系数、体力上限、近岸距离、巨影）；搏
	 * 斗外无意义。猫力在每帧推进前按当前合法参与者合计刷新，其余不再改。
	 */
	FCatFishingFightParams FightParams;

	/** 搏斗逐帧运行态（D/L/鱼段/鱼体力/挣扎/终局）的服务器真相；Snapshot 里的同名字段只是它的只读拷贝。 */
	FCatFishingFightState FightState;

	/** 搏斗用的随机流；用 EncounterSpec.SelectionSeed 初始化，同一次 encounter 的段长与方向 roll 可复现，便于审计。 */
	FRandomStream FightRandom;

	/** 钓手当前按住的遛鱼操作；由 SubmitFightIntent 写，每帧推进时读。 */
	ECatFishingFightIntent FisherIntent = ECatFishingFightIntent::None;

	/** 服务器进入 TrueBiteWindow 的 World 时间（秒）；负数表示尚未进入。完美中鱼 = 提竿意图到达时刻减它 ≤ 1 秒。 */
	double TrueBiteEnteredServerSeconds = -1.0;

	/** 真咬期是否已经收到过提竿并发出 HookSet 事件；防止同一次真咬重复发事件或重复判完美。 */
	bool bHookSetRequested = false;

	/** 本局钓手鱼竿的强度（静态）；InitializeSession 时从竿定义冻结，进搏斗后不再重读装备。 */
	double RodStrength = 0.0;

	/** 本局钓手鱼竿的放线上限 L_max（米）；与 RodStrength 同时冻结。 */
	double LineMaxMeters = 0.0;

	/** 搏斗是否已经开局（BeginFightOnPhaseEntry 成功）；推进口用它拒绝未开局的帧。 */
	bool bFightStarted = false;

	/** 僵持中累计、尚未提交给 Equipment 的竿磨损；每满 1 秒或终局时整批提交，避免每帧各开一条幂等记录。 */
	double PendingRodWearCost = 0.0;

	/** 距离上一次提交竿磨损已累计的搏斗秒数；达到 1 秒就触发一次提交。 */
	double RodWearFlushAccumulatorSeconds = 0.0;

	/** 本会话失败预算是否已经提交；true 后任何第二种惩罚都返回 AlreadyResolved。 */
	bool bFailureBudgetCommitted = false;

	/** 本会话唯一失败预算终态；重放不再次扣特殊饵或鱼竿耐久。 */
	FCatFishingFailureResult FailureBudgetResult;
};
