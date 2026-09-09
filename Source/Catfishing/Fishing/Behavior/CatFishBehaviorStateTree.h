#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponentSchema.h"
#include "Fishing/Behavior/CatFishBehaviorTypes.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "CatFishBehaviorStateTree.generated.h"

/** 叶子只配置要执行的策略。执行时间和连续出力由 Runner 固定步唯一持有。 */
USTRUCT()
struct FCatFishBehaviorStateTaskInstanceData
{
	GENERATED_BODY()

	/** StateTree 资产叶子提交的策略，不能由动画意图反推。 */
	UPROPERTY(EditAnywhere, Category="Parameter")
	ECatFishBehavior Behavior = ECatFishBehavior::None;
};

/**
 * ST_FishFight 的薄 Task：进入时提交一次策略，之后保持 Running。
 * 无 Task Tick 或第二份计时。资产 OnTick 条件读取 Runner 固定步事实选择真实转移边。
 */
USTRUCT(meta=(DisplayName="Cat Fish Run Behavior State", Category="Catfishing|Fishing|Fish Behavior"))
struct CATFISHING_API FCatFishBehaviorStateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCatFishBehaviorStateTaskInstanceData;
	FCatFishBehaviorStateTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FCatFishBehaviorConditionInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Parameter")
	ECatFishBehaviorCondition Condition = ECatFishBehaviorCondition::None;
	UPROPERTY(EditAnywhere, Category="Parameter")
	bool bInvert = false;
};

/** 只读反馈谓词；组合、优先级和目标状态均属于正式 StateTree 资产。 */
USTRUCT(meta=(DisplayName="Cat Fish Behavior Feedback", Category="Catfishing|Fishing|Fish Behavior"))
struct CATFISHING_API FCatFishBehaviorFeedbackCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FCatFishBehaviorConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

/** 鱼 Actor 专用 Component Schema；让 StateTree 编辑器上下文明确显示 ACatFishEncounterActor。 */
UCLASS()
class CATFISHING_API UCatFishBehaviorStateTreeSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	UCatFishBehaviorStateTreeSchema();
};
