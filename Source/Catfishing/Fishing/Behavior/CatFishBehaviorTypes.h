#pragma once

#include "CoreMinimal.h"
#include "CatFishBehaviorTypes.generated.h"

/** 服务器鱼策略；独立于兼容动画用的 ECatFishMotionIntent，不直接决定费用或终局。 */
UENUM(BlueprintType)
enum class ECatFishBehavior : uint8
{
	None,
	OutwardRush,
	LateralArc,
	EaseOff
};

/** StateTree 资产组合这些只读条件选择真实边；模型不内置下一行为。 */
UENUM()
enum class ECatFishBehaviorCondition : uint8
{
	None,
	MinimumDurationElapsed,
	DurationExpired,
	SustainedBlocked,
	LowStamina
};
