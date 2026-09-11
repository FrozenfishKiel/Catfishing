#include "Run/CatRunStateTreeEvents.h"

namespace CatRunStateTreeEvents
{
	// ST_RunFlow 资产事件名仍包含 QuotaReached/QuotaFailed 字符串；C++ 符号表达“白天结束/世界进度归零”语义，避免二进制资产未重建时收不到事件。
	UE_DEFINE_GAMEPLAY_TAG(DayEnded, "Cat.Run.QuotaReached");
	UE_DEFINE_GAMEPLAY_TAG(WorldProgressDepleted, "Cat.Run.QuotaFailed");
	UE_DEFINE_GAMEPLAY_TAG(AllEligibleReady, "Cat.Run.AllEligibleReady");
	UE_DEFINE_GAMEPLAY_TAG(SettlementComplete, "Cat.Run.SettlementComplete");
}
