#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/CatFishingUseResults.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "CatFishingStateTreeNodes.generated.h"

/** Fishing 阶段入口 Task 参数；资产节点选择公开 Phase，C++ 不保存转移表。 */
USTRUCT()
struct FCatFishingEnterPhaseTaskInstanceData
{
	GENERATED_BODY()

	/** 当前 State 对应的公开 Fishing Phase。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatFishingPhase Phase = ECatFishingPhase::Created;

};

/** ST_FishingSession 的阶段入口 Task；只调用 Session 唯一写口。 */
USTRUCT(meta = (DisplayName = "Cat Fishing Enter Phase", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingEnterPhaseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingEnterPhaseTaskInstanceData;

	/** 关闭 Tick，使阶段进入副作用每次 State 只提交一次。 */
	FCatFishingEnterPhaseTask();

	/** 向 StateTree 暴露阶段入口参数布局，使资产可配置 Phase/近岸目标而不让 Task 保存第二份阶段状态。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入 State 时定位 Session Owner 并提交 Phase；拒绝时返回 Failed 且不选择备用边。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Fishing 事件等待 Task 的空实例数据。 */
USTRUCT()
struct FCatFishingWaitTaskInstanceData
{
	GENERATED_BODY()
};

/** ST_FishingSession 的无 Tick 等待节点；窗口/事件由资产和后续 Task 驱动。 */
USTRUCT(meta = (DisplayName = "Cat Fishing Wait", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingWaitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingWaitTaskInstanceData;

	/** 关闭 Tick 与无意义属性复制；节点不创建第二个计时器。 */
	FCatFishingWaitTask();

	/** 向 StateTree 声明本等待节点没有可写实例状态，避免资产误以为 C++ 维护窗口计时器。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入后保持 Running，直到资产事件边退出。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** 失败预算 Task 参数；每个节点只能选择一个正式惩罚类别。 */
USTRUCT()
struct FCatFishingFailureBudgetTaskInstanceData
{
	GENERATED_BODY()

	/** 本次失败要提交的唯一惩罚；同会话第二次提交会被 Session 拒绝。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatFishingFailurePenalty Penalty = ECatFishingFailurePenalty::None;
};

/** ST_FishingSession 的失败预算节点；把互斥惩罚提交给 Equipment，不同时扣饵和耐久。 */
USTRUCT(meta = (DisplayName = "Cat Fishing Commit Failure Budget", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingFailureBudgetTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingFailureBudgetTaskInstanceData;

	/** 关闭 Tick；失败预算只有单次提交点。 */
	FCatFishingFailureBudgetTask();

	/** 向 StateTree 暴露唯一失败惩罚参数，使资产只能选择一次互斥预算而不能组合双罚。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入 State 时提交互斥预算；成功返回 Succeeded，依赖/策略失败返回 Failed。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** ST_FishingSession 的重试耗尽终态节点；生成一次剪影 Grant 并终止会话，不创建实物鱼。 */
USTRUCT(meta = (DisplayName = "Cat Fishing Resolve Retry Exhausted Escape", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingResolveRetryExhaustedTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingWaitTaskInstanceData;

	/** 关闭 Tick；资产进入该终态时只提交一次已裁的剪影资格。 */
	FCatFishingResolveRetryExhaustedTask();

	/** 复用无参数实例数据；重试耗尽资格由资产所选节点本身表达。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入 State 时调用 Session 唯一剪影终态写口；Collection 拒绝时返回 Failed 且会话保持可诊断。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Waiting 阶段的咬钩探测调度节点；只安排下一次 Probe 计时器，真正选鱼要等合法提竿输入。 */
USTRUCT(meta=(DisplayName="Cat Fishing Schedule Waiting Probe", Category="Catfishing|Fishing"))
struct CATFISHING_API FCatFishingScheduleWaitingProbeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishingWaitTaskInstanceData;
	/** 关闭 Tick；调度只在 State 进入时发生一次，计时器由 Session 持有。 */
	FCatFishingScheduleWaitingProbeTask();
	/** 向 StateTree 声明本节点不需要可写实例数据，避免资产保存第二份等待状态。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	/** 进入 State 时请求 Session 安排 Probe；调度失败返回 Failed，让资产走失败边。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Probe 进入后只把浮漂切到猛沉并打开响应计时器；不会在玩家左键前创建鱼。 */
USTRUCT(meta=(DisplayName="Cat Fishing Open True Bite Window", Category="Catfishing|Fishing"))
struct CATFISHING_API FCatFishingOpenTrueBiteWindowTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishingWaitTaskInstanceData;
	/** 关闭 Tick；真咬响应窗口由 Session 计时器控制。 */
	FCatFishingOpenTrueBiteWindowTask();
	/** 向 StateTree 暴露空实例数据，避免节点自身持有鱼或库存状态。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	/** 进入 State 时打开真咬窗口；失败表示 Session 阶段或依赖已经失效。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

/** Fishing 阶段条件参数；资产写入期望阶段，C++ 每次执行时只读公开 Snapshot。 */
USTRUCT()
struct FCatFishingPhaseConditionInstanceData
{
	GENERATED_BODY()
	/** 本条件允许通过的唯一公开阶段；StateTree 资产写入它，Session 不会反向修改节点参数。 */
	UPROPERTY(EditAnywhere, Category="Parameter") ECatFishingPhase ExpectedPhase = ECatFishingPhase::Created;
};

/** Fishing 阶段判断条件；用于资产分支复核当前公开阶段，不承担阶段转移。 */
USTRUCT(meta=(DisplayName="Cat Fishing Phase Is", Category="Catfishing|Fishing"))
struct CATFISHING_API FCatFishingPhaseCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishingPhaseConditionInstanceData;
	/** 向 StateTree 暴露期望阶段参数；运行时不缓存第二份阶段。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	/** 执行条件时读取 Session Snapshot 并比较 ExpectedPhase；缺少 Session 时 fail-closed。 */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

/** 启动服务器搏斗 Runner 的节点；StateTree 只发起阶段副作用，所有数值模拟留在 Runner。 */
USTRUCT(meta=(DisplayName="Cat Fishing Start Fight Runner", Category="Catfishing|Fishing"))
struct CATFISHING_API FCatFishingStartFightRunnerTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishingWaitTaskInstanceData;
	/** 关闭 Tick；Runner 启动只允许在进入 HookedFight 时提交一次。 */
	FCatFishingStartFightRunnerTask();
	/** 复用空等待数据；搏斗参数来自 Session 和定义资产，不保存在节点实例里。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	/** 进入 State 时启动权威 Runner；依赖缺失或阶段不符时返回 Failed。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

/** 等待服务器搏斗 Runner 结束的节点；它只轮询终态，不直接执行搏斗固定步。 */
USTRUCT(meta=(DisplayName="Cat Fishing Wait For Fight Runner", Category="Catfishing|Fishing"))
struct CATFISHING_API FCatFishingWaitForFightRunnerTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishingWaitTaskInstanceData;
	/** 保持 Tick；节点需要定期观察 Runner 是否结束。 */
	FCatFishingWaitForFightRunnerTask();
	/** 复用空等待数据；Runner 的状态仍由 Session/FightRunner 持有。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	/** 进入 State 时确认 Runner 可观察；不可观察时失败，避免 StateTree 假装搏斗仍在进行。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	/** 每帧只检查 Runner 运行状态和 Session 阶段；不会在 StateTree 节点中计算耐久或体力。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
