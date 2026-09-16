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
	/** 旧等待倍率已退役；仅沿用库存片段通用准入，特殊饵身份仍由失败预算读取。 */
	virtual bool IsRuntimeReady() const override;

	/** Bait 的特殊身份标记；它只区分偏好和失败惩罚语义，该饵是否是一局数量物由 bRunConsumable 表达。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumption")
	bool bSpecialBait = false;

	/** 旧等待频率字段，仅留资产与未知 Blueprint 绑定兼容；选鱼权重读取鱼表。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bait", meta = (DeprecatedProperty, DeprecationMessage="Waiting now uses chum concentration; fish preference weights live on fish definitions"))
	double BiteRateMultiplier = 0.0;

	/** 旧慢浮下限字段，仅留序列化与未知 Blueprint 绑定兼容；运行不读取。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bait", meta = (DeprecatedProperty, DeprecationMessage="Waiting now uses chum concentration; fish preference weights live on fish definitions"))
	double MinimumBiteDelayMultiplier = 0.0;
};
