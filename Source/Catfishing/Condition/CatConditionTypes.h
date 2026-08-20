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
	/**
	 * 旧草药恢复路径的保留枚举；WORK-04 起不再由生产入口提交。之所以保留空位而不是删掉，是因为这个枚举随 Snapshot 复
	 * 制到客户端：删值会让 CarriedToCamp 的整数值前移，版本不一致的客户端会把搬运恢复读成另一种恢复方式。
	 */
	Herb,
	/** 伙伴搬运到固定营地救援点。 */
	CarriedToCamp
};

/** Character 局内身体离散状态的复制读模型；Poison 数值仍只在 ASC AttributeSet。 */
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

	/** 猫是否因 Poison 达到显式阈值进入可恢复倒地；倒地来源仅中毒（飞书猫咪状态册 v1.4）。项目不存在死亡终态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;

	/** 最近一次服务器接受的恢复方式；无动作时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRecoveryMode RecoveryMode = ECatRecoveryMode::None;
};
