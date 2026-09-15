#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentFragment_Scoop.generated.h"

/** 抄网资格片段；有效射程统一读取 CatFishingSettings，不复制运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Scoop : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 片段的存在即提供抄网资格，不再以旧逐网距离拒绝装备。 */
	virtual bool IsRuntimeReady() const override;

	/** 墓碑（2026-09-14，T15；钓鱼规则 §5.1）：仅保留旧资产序列化；运行统一全局 2 米。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scoop", meta = (DeprecatedProperty, DeprecationMessage="逐网射程已退出裁决；使用全局 ScoopReach。"))
	double ScoopReachCentimeters = 0.0;
};
