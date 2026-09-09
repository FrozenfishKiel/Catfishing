#pragma once

#include "NativeGameplayTags.h"

// RunFlow 事件 Tag 是 GameMode 与 ST_RunFlow 之间的公开事件契约；符号导出给 CatfishingEditor，资产生成工具用同一批原生 Tag 写 Transition，避免字符串和 C++ 定义分叉。
namespace CatRunStateTreeEvents
{
	/** 当日额度首次达到目标时由 GameMode 发布的事件；ST_RunFlow 消费它，把 DayActive 转向 NormalNight。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(QuotaReached);

	/** DayActive 截止且额度不足时由 GameMode 发布的事件；ST_RunFlow 消费它，把流程转向 FailureSettlementNight。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(QuotaFailed);

	/** 普通夜晚当前合资格玩家全部 ready 后由 GameMode 发布的事件；ST_RunFlow 消费它，进入下一次 DayActive。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllEligibleReady);

	/** 结算依赖完成收口后由 GameMode 协调入口发布的事件；ST_RunFlow 消费它，把结算夜推进到 Ending。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SettlementComplete);
}
