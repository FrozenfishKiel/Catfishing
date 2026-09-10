#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatRunStateTreeAuthoringLibrary.generated.h"

/** RunFlow 资产生成工具，代表编辑器侧维护默认 ST_RunFlow 拓扑的唯一重建入口；它只在 CatfishingEditor 模块使用，避免运行时依赖 Editor API。 */
UCLASS()
class CATFISHINGEDITOR_API UCatRunStateTreeAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 重建并编译默认一局流程树；成功后保存白天、普通夜、毕业夜和归零直接结束的拓扑，创建、编译或保存失败时返回 false。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Run")
	static bool CreateOrUpdateDefaultRunFlowStateTree();
};
