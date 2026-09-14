#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConditionTypes.generated.h"

UENUM(BlueprintType)
enum class ECatWaterExposureState : uint8
{
	Dry,
	Shallow,
	Dangerous
};

enum class ECatWaterExposureUpdate : uint8
{
	Unavailable,
	Unchanged,
	Changed,
	DangerousEntered
};

/** Character 局内身体离散状态的复制读模型；保存表现和交互资格需要的客观状态。 */
USTRUCT(BlueprintType)
struct FCatConditionSnapshot
{
	GENERATED_BODY()

	/** 身体离散状态快照的版本，0 表示尚未提交变化；Condition 在 Wet、Downed 或水域暴露状态改变后递增，复制读模型的消费者读取它识别状态版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 毛发当前是否淋湿；它只驱动表现，不带移动、数值或钓鱼惩罚。 */
	UPROPERTY(BlueprintReadOnly)
	bool bWet = false;

	/** 水深阈值的唯一离散结果；危险只在服务器持续确认后进入。 */
	UPROPERTY(BlueprintReadOnly)
	ECatWaterExposureState WaterExposure = ECatWaterExposureState::Dry;

	/** 猫当前是否处于倒地状态；Condition 写入，交互和身体表现读取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;
};
