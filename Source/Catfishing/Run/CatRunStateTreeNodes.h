#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "CatRunStateTreeNodes.generated.h"

/** EnterPhase Task 的资产参数；Phase 由 ST_RunFlow 节点配置，C++ 只执行该次状态进入副作用。 */
USTRUCT()
struct FCatRunEnterPhaseTaskInstanceData
{
	GENERATED_BODY()

	/** 当前 StateTree State 对应的公开 Run Phase；资产拓扑决定何时进入它。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatRunPhase Phase = ECatRunPhase::NotStarted;

	/** 进入该 Phase 时记录的原因；只进入结构化 Result，不决定下一条边。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatRunTransitionReason Reason = ECatRunTransitionReason::None;
};

/** ST_RunFlow 的阶段入口 Task；调用 GameMode 唯一写口并把结构化成功/失败返回给资产。 */
USTRUCT(meta = (DisplayName = "Cat Run Enter Phase", Category = "Catfishing|Run"))
struct CATFISHING_API FCatRunEnterPhaseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatRunEnterPhaseTaskInstanceData;

	/** 关闭 Tick，使阶段副作用只在 State 进入时提交一次。 */
	FCatRunEnterPhaseTask();

	/** 返回本节点参数结构，供 StateTree 资产持有 Phase/Reason 而非写死在 C++ 拓扑。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** State 进入时调用 GameMode EnterPhase；成功返回 Succeeded，gate/宿主失败返回 Failed。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** 等待事件 Task 的空实例数据；节点不保存计时器、Phase 或并行 FSM 状态。 */
USTRUCT()
struct FCatRunWaitForEventTaskInstanceData
{
	GENERATED_BODY()
};

/** ST_RunFlow 的事件等待 Task；保持 Running，具体事件 Tag 与转移目标完全留在资产 Transition。 */
USTRUCT(meta = (DisplayName = "Cat Run Wait For Event", Category = "Catfishing|Run"))
struct CATFISHING_API FCatRunWaitForEventTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatRunWaitForEventTaskInstanceData;

	/** 关闭常规 Tick，让 StateTree 自身的事件调度唤醒 Transition，不创建每帧轮询。 */
	FCatRunWaitForEventTask();

	/** 返回空实例结构；等待生命周期完全由当前 State 与资产事件边控制。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** State 进入后保持 Running，直到资产中的事件 Transition 退出该 State。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Result 条件的资产参数；只比较 GameMode 最近一次结构化原因，不缓存第二份 Run 状态。 */
USTRUCT()
struct FCatRunResultReasonConditionInstanceData
{
	GENERATED_BODY()

	/** 当前 Transition 期望消费的结构化原因。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatRunTransitionReason ExpectedReason = ECatRunTransitionReason::None;
};

/** ST_RunFlow 的 Result 条件；资产可在同一事件 Tag 下按结构化原因分边，C++ 不决定目标 State。 */
USTRUCT(meta = (DisplayName = "Cat Run Result Reason", Category = "Catfishing|Run"))
struct CATFISHING_API FCatRunResultReasonCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatRunResultReasonConditionInstanceData;

	/** 向 StateTree 暴露预期结果原因，使资产选择转换条件；Condition 只比较公开结果，不在 C++ 内藏转移拓扑。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 从 Context Owner 的 GameMode 读取最新 Result 并比较原因；宿主类型不符时返回 false。 */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

/** 最终天选边条件的空实例数据；条件没有可配参数，但 StateTree 编译器要求每个节点都带实例值，所以给一个空结构。 */
USTRUCT()
struct FCatRunSuccessSettlementEligibleConditionInstanceData
{
	GENERATED_BODY()
};

/**
 * ST_RunFlow 的最终天选边条件：普通夜晚收到 AllEligibleReady 后，资产用它决定走 SuccessSettlementNight 还是再翻一天。
 * 判定本身留在服务器（UCatRunSettings::CanEnterSuccessSettlementNight 比较当前 DayIndex 与 FinalDayIndex），资产读不到 DayIndex，只负责按真假选边；
 * 没有参数，因为它只有一个问题要问 GameMode。对应 Docs/Development/工程自补决策记录.md 的 D-01。
 */
USTRUCT(meta = (DisplayName = "Cat Run Success Settlement Eligible", Category = "Catfishing|Run"))
struct CATFISHING_API FCatRunSuccessSettlementEligibleCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatRunSuccessSettlementEligibleConditionInstanceData;

	/** 返回空实例结构；本条件没有资产可配的参数，最终天序号只从 UCatRunSettings 读。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 向 Context Owner 的 GameMode 询问当前 DayIndex 是否已到可进成功结算夜的最终天；宿主类型不符或配置未裁时返回 false，让资产走翻天边。 */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
