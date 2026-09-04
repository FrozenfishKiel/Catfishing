#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponentSchema.h"
#include "CatRunFlowStateTreeSchema.generated.h"

/** ST_RunFlow 专用 Schema；它把这棵服务器流程树的上下文固定为 GameMode，避免资产误接到客户端或普通 Actor 上。 */
UCLASS()
class CATFISHING_API UCatRunFlowStateTreeSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	/** 构造默认 Schema 配置；完成后编辑器和运行时都会把 Context Owner 视为 ACatfishingGameModeBase。 */
	UCatRunFlowStateTreeSchema();
};
