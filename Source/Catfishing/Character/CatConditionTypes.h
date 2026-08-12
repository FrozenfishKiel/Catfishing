#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConditionTypes.generated.h"

/** 倒地后的恢复方式；它描述服务器已接受的路径，不包含动画或数值公式。 */
UENUM(BlueprintType)
enum class ECatRecoveryMode : uint8
{
	/** 当前没有恢复动作。 */
	None,
	/** 单人可用的野外缓慢休息/爬行自救路径。 */
	FieldSelfRecovery,
	/** 固定营地的快速休息路径。 */
	CampRest,
	/** 自己或伙伴消耗草药的恢复路径。 */
	Herb,
	/** 伙伴搬运到固定营地救援点。 */
	CarriedToCamp
};

/** Character 局内身体离散状态的复制读模型；Hunger/Fatigue/Poison 数值仍只在 ASC AttributeSet。 */
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

	/** 猫是否因正式 Poison/Fatigue 阈值进入可恢复倒地；项目不存在死亡终态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;

	/** 最近一次服务器接受的恢复方式；无动作时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRecoveryMode RecoveryMode = ECatRecoveryMode::None;
};
