#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentFragment_Bait.generated.h"

/** 鱼饵的吸引效果与特殊饵身份，供咬钩选择和失败预算读取；资产持有静态值，不复制运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Bait : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 校验咬钩和等待倍率为有限正数；特殊饵标记由失败预算读取，不改变数值有效性。 */
	virtual bool IsRuntimeReady() const override;

	/** Bait 的特殊身份标记；它只区分偏好和失败惩罚语义，该饵是否是一局数量物由 bRunConsumable 表达。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bSpecialBait = false;

	/** 鱼饵对咬钩概率或权重的倍率；Fishing 选鱼和等待预算读取它，库存只负责数量冻结与消耗。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bait", meta = (ClampMin = "0.0"))
	double BiteRateMultiplier = 0.0;

	/** 鱼饵对最短咬钩等待时间的倍率；Fishing 等待采样读取它，数值语义不进入库存系统。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bait", meta = (ClampMin = "0.0"))
	double MinimumBiteDelayMultiplier = 0.0;
};
