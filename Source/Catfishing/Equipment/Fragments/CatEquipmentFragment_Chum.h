#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Environment/CatChumFieldTypes.h"
#include "CatEquipmentFragment_Chum.generated.h"

/** 窝料的水域影响规格；定义持有配置，投放事务读取它创建水域影响，不保存运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Chum : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 校验窝料影响范围、数量和效果配置；缺失或无效配置不能进入投放事务。 */
	virtual bool IsRuntimeReady() const override;

	/** Chum placement 的空间影响定义；不具备窝料投放规则的定义应保持未配置，客户端不能覆盖该数据。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chum")
	FCatChumInfluenceSpec ChumInfluence;
};
