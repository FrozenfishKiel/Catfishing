#pragma once

#include "NativeGameplayTags.h"

// 四个 Tag 对象带 CATFISHING_API 导出：CatfishingEditor 模块的资产生成 Commandlet 要拿同一批对象给 ST_RunFlow 写事件
// 边，不允许在那边重抄字符串造出第二份事件集合。
namespace CatRunStateTreeEvents
{
	/** 白天固定时长走完；ST_RunFlow 资产负责把 DayActive 转向 NormalNight。到点入夜与额度成败无关，本事件不携带任何结算含义。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DayElapsed);

	/** 翻天确认时当日额度仍不足；ST_RunFlow 资产负责转向 FailureSettlementNight。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(QuotaFailed);

	/**
	 * 普通夜晚当前合资格集合全部 ready 且额度已交齐；ST_RunFlow 资产负责进入下一次 DayActive，或在最终天进入
	 * SuccessSettlementNight（由 FCatRunSuccessSettlementEligibleCondition 选边）。
	 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllEligibleReady);

	/** 成功或失败结算依赖已收口；ST_RunFlow 资产负责进入 Ending。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SettlementComplete);
}
