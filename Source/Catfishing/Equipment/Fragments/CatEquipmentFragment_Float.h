#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentFragment_Float.generated.h"

/** 浮漂的抛投范围、误差和信号配置，供服务器抛投裁决读取；资产持有静态值，不复制运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Float : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 校验本片段数值与空间约束；装备目录和具体玩法仅接收完整配置，不在这里补写缺省值。 */
	virtual bool IsRuntimeReady() const override;

	/** 浮漂允许的最大抛投距离，单位厘米；瞄准和服务器抛投裁决读取它限制落点。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double MaximumCastDistanceCentimeters = 0.0;

	/** 浮漂抛投误差的标准差，单位厘米；落点随机偏移读取它，不能大于最大误差半径。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double CastErrorStandardDeviationCentimeters = 0.0;

	/** 浮漂抛投误差的最大半径，单位厘米；服务器裁决用它截断随机偏移，避免远超配置落点。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0"))
	double MaximumCastErrorRadiusCentimeters = 0.0;

	/** 浮漂咬钩信号稳定度，范围 0 到 1；等待和表现读取它表达信号质量，不承担物品分类。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Float", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double BiteSignalStability = 0.0;
};
