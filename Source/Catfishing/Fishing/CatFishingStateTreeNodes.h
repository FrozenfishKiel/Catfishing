#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "StateTreeTaskBase.h"
#include "CatFishingStateTreeNodes.generated.h"

/**
 * Fishing 阶段入口 Task 参数；资产节点只选择公开 Phase，C++ 不保存转移表。NearShore 的近岸目标由 Session 在服务器按钓
 * 手位置与冻结水域计算，资产没有（也不该有）填写世界位置的入口——写死在资产里的常量不可能是运行时的近岸位置。
 */
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

	/** 向 StateTree 暴露阶段入口参数布局，使资产只配置 Phase 而不让 Task 保存第二份阶段状态。 */
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

/** 咬钩间隔等待 Task 的实例数据；只有一个累计秒数，用来和会话冻结的间隔比较。 */
USTRUCT()
struct FCatFishingBiteIntervalWaitTaskInstanceData
{
	GENERATED_BODY()

	/** 进入本状态以来累计流逝的秒数；每次 Tick 累加，达到会话的 BiteIntervalSeconds 时任务完成。不暴露给资产编辑。 */
	UPROPERTY()
	double ElapsedSeconds = 0.0;
};

/**
 * ST_FishingSession 的 Probe 等待节点：等满会话在 Cast 时冻结的咬钩间隔（按落点窝料算出的 T_actual）后完成，让资产的
 * 完成边把状态推进到 TrueBiteWindow。
 * 时长不在资产里配，也不在这里配——它是每次抛竿按窝料现算、冻结进会话的值；资产只负责"等完了去哪"。
 */
USTRUCT(meta = (DisplayName = "Cat Fishing Wait Bite Interval", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingBiteIntervalWaitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingBiteIntervalWaitTaskInstanceData;

	/** 打开 Tick（本节点就是靠 Tick 累计时间的），关闭无意义的属性复制。 */
	FCatFishingBiteIntervalWaitTask();

	/** 向 StateTree 暴露累计秒数实例数据；它只是计时器状态，不是可配参数。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入时清零累计并核对会话持有合法（正有限）间隔；间隔非法返回 Failed 让资产走失败边。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** 每帧累加 DeltaTime，累计达到会话间隔返回 Succeeded，否则保持 Running；会话失效返回 Failed。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

/** 搏斗推进 Task 的空实例数据：消耗公式全部来自飞书规则写死在会话的纯模型里，资产没有可调参数。 */
USTRUCT()
struct FCatFishingFightExchangeTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * ST_FishingSession 的 HookedFight 驱动节点：每帧把 DeltaTime 交给 Session 唯一的搏斗推进口（飞书 4.3 判定表 / 4.4 消耗战 / D-L 模型都在会话里），
 * 自己不保存任何搏斗状态，只按会话返回的终局决定 Running / Succeeded（碾压、翻肚、遛到岸边 → 资产进 NearShore）/
 * Failed（断竿、拖下水、依赖丢失 → 资产走失败边进 Terminated）。
 */
USTRUCT(meta = (DisplayName = "Cat Fishing Fight Exchange", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingFightExchangeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingFightExchangeTaskInstanceData;

	/** 打开 Tick（搏斗靠它逐帧推进），关闭无意义的属性复制。 */
	FCatFishingFightExchangeTask();

	/** 向 StateTree 声明本节点没有可配参数，避免资产误以为能在这里改公式。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入时只确认 Owner 是 Session 并保持 Running；开局副作用已由同状态的 EnterPhase(HookedFight) 完成。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** 每帧调用 Session 推进口：拒绝或失败终局返回 Failed，成功终局返回 Succeeded，否则 Running。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
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
