#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "StateTreeTaskBase.h"
#include "CatFishingStateTreeNodes.generated.h"

/** Fishing 阶段入口 Task 参数；资产节点选择公开 Phase，C++ 不保存转移表。 */
USTRUCT()
struct FCatFishingEnterPhaseTaskInstanceData
{
	GENERATED_BODY()

	/** 当前 State 对应的公开 Fishing Phase。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECatFishingPhase Phase = ECatFishingPhase::Created;

	/** 进入 NearShore 时由服务器鱼运行态绑定的世界目标；其他 Phase 不读取。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector AuthoritativeNearShoreTarget = FVector::ZeroVector;

	/** 资产确认上述目标来自服务器运行态；默认 false，禁止仅靠阶段名绕过几何校验。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bHasAuthoritativeNearShoreTarget = false;
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

/** 搏斗资源交换 Task 参数；具体消耗由 StateTree 资产显式提供，0 表示未裁并失败。 */
USTRUCT()
struct FCatFishingFightExchangeTaskInstanceData
{
	GENERATED_BODY()

	/** 本次成功交换消耗的鱼体力。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	double FishStaminaCost = 0.0;

	/** 本次成功交换对每名参与猫消耗的搏斗体力。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	double ParticipantStaminaCost = 0.0;
};

/** ST_FishingSession 的权威搏斗交换节点；只调用 Session 资源写口，不内置阶段转移。 */
USTRUCT(meta = (DisplayName = "Cat Fishing Fight Exchange", Category = "Catfishing|Fishing"))
struct CATFISHING_API FCatFishingFightExchangeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishingFightExchangeTaskInstanceData;

	/** 关闭 Tick；每次 State 进入最多提交一次资源交换。 */
	FCatFishingFightExchangeTask();

	/** 向 StateTree 暴露单次搏斗交换参数，使具体消耗留在资产配置且 0 值继续 fail-closed。 */
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** 进入 State 时向 Session 提交力量/体力交换；任何不足返回 Failed 供资产选择失败边。 */
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
