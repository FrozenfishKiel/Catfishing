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
	LowStamina,
	NeedsRecovery,
	/**
	 * 段末抽到的「下一段向外」。概率来自鱼表「食性」列换算出的 P_base，
	 * 在 BeginBehavior 冻结一次，整段内不变（段内不换向）。
	 * 食性未填或档位概率未裁时该条件恒为 false，资产上挂它的转移边永不触发，树的现有拓扑不受影响。
	 */
	OutwardSegmentRoll
};
