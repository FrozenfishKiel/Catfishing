#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponentSchema.h"
#include "Fishing/CatFishingTypes.h"
#include "StateTreeTaskBase.h"
#include "CatFishBehaviorStateTree.generated.h"

/** 每个鱼行为状态自己的运行数据；Duration 由服务器性格随机流在 EnterState 时冻结。 */
USTRUCT()
struct FCatFishBehaviorStateTaskInstanceData
{
	GENERATED_BODY()

	/** 资产状态要发布给固定步 Runner 的高层意图。 */
	UPROPERTY(EditAnywhere, Category="Parameter")
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;

	/** 本次状态剩余秒数；不在资产中配置、不复制。 */
	UPROPERTY(Transient)
	double RemainingSeconds = 0.0;
};

/**
 * ST_FishFight 的薄 Task：进入时让 Runner 从性格 DA 抽一次持续时间，Tick 只等待该时长结束。
 * 它不计算位置、鱼线、力量或体力，状态完成后由资产转移到下一状态。
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
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};

/** 鱼 Actor 专用 Component Schema；让 StateTree 编辑器上下文明确显示 ACatFishEncounterActor。 */
UCLASS()
class CATFISHING_API UCatFishBehaviorStateTreeSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	UCatFishBehaviorStateTreeSchema();
};
