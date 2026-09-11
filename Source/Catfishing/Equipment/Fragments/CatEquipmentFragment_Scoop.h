#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentFragment_Scoop.generated.h"

/** 抄网的触达范围，供目标扫描和服务器捕获裁决读取；资产持有静态值，不复制运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Scoop : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 校验触达距离为有限正数；无效距离会阻止抄网进入捕获流程。 */
	virtual bool IsRuntimeReady() const override;

	/** 抄网可触达鱼的有效距离，单位厘米；抢抄命令读取它做范围裁决，不通过中心种类表判断抄网。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scoop", meta = (ClampMin = "0.0"))
	double ScoopReachCentimeters = 0.0;
};
