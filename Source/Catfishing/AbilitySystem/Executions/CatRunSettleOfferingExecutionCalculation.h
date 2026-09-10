#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "CatRunSettleOfferingExecutionCalculation.generated.h"

/** 夜晚供品结算的纯数值结果；GameMode 用它预演，ExecCalc 用同一公式写入 Run Attribute。 */
struct CATFISHING_API FCatRunOfferingSettlementResult
{
	/** 本次祭坛上供品按重量档折算后的总点数；它来自供品计数，不接受客户端直接提交。 */
	int32 OfferedPoints = 0;

	/** 本次结算对世界进度产生的变化；达标为非负，未达标为负，最终由 WorldProgress 夹到 0 到 100。 */
	int32 WorldProgressDelta = 0;

	/** 结算后的世界进度；GameMode 用它决定是否发送失败或毕业相关的 StateTree 事件。 */
	int32 NewWorldProgress = 0;

	/** 本次供品点数是否达到当天目标；臭鱼只折扣达标增益，不会把达标反转为失败。 */
	bool bMetDailyTarget = false;
};

/** 夜晚供品结算的唯一数值计算器；读取供品计数、每日目标和世界进度倍率，覆盖上一晚结果与世界进度。 */
UCLASS()
class CATFISHING_API UCatRunSettleOfferingExecutionCalculation : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	/** 注册每日目标、世界进度和来源倍率捕获，确保结算在 GE 应用瞬间读取 Run ASC 的当前权威值。 */
	UCatRunSettleOfferingExecutionCalculation();

	/** 将供品数量按策划案重量档换算成点数；调用方和 ExecCalc 共用它，避免在鱼容器服务、Run 或 UI 各写一套鱼价值公式。 */
	static bool TryCalculateOfferingPointsFromCounts(int32 SmallFishCount, int32 MediumFishCount,
		int32 LargeFishCount, int32 GiantFishCount, int32& OutOfferedPoints);

	/** 按供品点、臭鱼数量、每日奖惩和世界进度倍率计算最终结算；GameMode 预检和 GE 执行共用它保证前后一致。 */
	static bool TryCalculateSettlement(int32 SmallFishCount, int32 MediumFishCount, int32 LargeFishCount,
		int32 GiantFishCount, int32 StinkyFishCount, int32 BaseProgressGain, int32 BaseProgressLoss,
		int32 DailyOfferingTarget, int32 CurrentWorldProgress, float WorldProgressGainMultiplier,
		float WorldProgressLossMultiplier, FCatRunOfferingSettlementResult& OutResult);

	/** 读取 SetByCaller 供品计数与日程奖惩，捕获目标和倍率后输出上一晚供品点、进度变化和世界进度。 */
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
