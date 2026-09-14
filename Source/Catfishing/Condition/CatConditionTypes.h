#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConditionTypes.generated.h"

/** 倒地后的恢复方式；它描述服务器已接受的客观救援路径，不包含动画或数值公式。 */
UENUM(BlueprintType)
enum class ECatRecoveryMode : uint8
{
	/** 当前没有恢复动作。 */
	None,
	/** 伙伴搬运到固定营地救援点。 */
	CarriedToCamp
};

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

/** Character 局内身体离散状态的复制读模型；保存表现、交互资格和救援需要的客观状态。 */
USTRUCT(BlueprintType)
struct FCatConditionSnapshot
{
	GENERATED_BODY()

	/** 每次 Wet、Downed 或恢复方式提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 毛发当前是否淋湿；它只驱动表现，不带移动、数值或钓鱼惩罚。 */
	UPROPERTY(BlueprintReadOnly)
	bool bWet = false;

	/** 水深阈值的唯一离散结果；危险只在服务器持续确认后进入。 */
	UPROPERTY(BlueprintReadOnly)
	ECatWaterExposureState WaterExposure = ECatWaterExposureState::Dry;

	/** 猫当前是否处于倒地状态；Condition 写入，交互、身体表现和救援入口读取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;

	/** 最近一次服务器接受的恢复方式；无动作时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRecoveryMode RecoveryMode = ECatRecoveryMode::None;
};
