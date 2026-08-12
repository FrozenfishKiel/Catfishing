#pragma once

#include "NativeGameplayTags.h"

namespace CatRunStateTreeEvents
{
	/** 当日额度第一次达到目标；ST_RunFlow 资产负责把 DayActive 转向 NormalNight。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(QuotaReached);

	/** 白天截止且额度不足；ST_RunFlow 资产负责转向 FailureSettlementNight。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(QuotaFailed);

	/** 普通夜晚当前合资格集合全部 ready；ST_RunFlow 资产负责进入下一次 DayActive。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllEligibleReady);

	/** 成功或失败结算依赖已收口；ST_RunFlow 资产负责进入 Ending。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SettlementComplete);
}
