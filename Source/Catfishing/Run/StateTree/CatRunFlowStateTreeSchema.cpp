#include "Run/StateTree/CatRunFlowStateTreeSchema.h"

#include "Framework/Game/CatGameplayTypes.h"

// RunFlow Schema 初始化流程：构造 CDO 时只写入 StateTree 上下文 Actor 类型；具体阶段、计时器和网络公开状态仍由 GameMode 的正式写口处理。
UCatRunFlowStateTreeSchema::UCatRunFlowStateTreeSchema()
{
	ContextActorClass = ACatfishingGameModeBase::StaticClass();
}
