#pragma once

#include "NativeGameplayTags.h"

// RunFlow 事件 Tag 是 GameMode 与 ST_RunFlow 之间的公开事件契约；符号导出给 CatfishingEditor，资产生成工具用同一批原生 Tag 写 Transition，避免字符串和 C++ 定义分叉。
namespace CatRunStateTreeEvents
{
	/** 白天截止时由 GameMode 发布的事件；ST_RunFlow 消费它，把 DayActive 转向 NormalNight，Tag 字符串暂沿用当前资产里的 QuotaReached。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DayEnded);

	/** 夜晚供品结算后世界进度归零时由 GameMode 发布的事件；ST_RunFlow 消费它，把流程转向 FailureSettlementNight，Tag 字符串暂沿用当前资产里的 QuotaFailed。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WorldProgressDepleted);

	/** 普通夜晚供品已经结算且世界进度未归零时由 GameMode 发布的继续事件；AllEligibleReady 是当前资产匹配名，对应供品继续事件。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllEligibleReady);

	/** 结算依赖完成收口后由 GameMode 协调入口发布的事件；ST_RunFlow 消费它，把结算夜推进到 Ending。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SettlementComplete);
}
