#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatFishStateTreeAuthoringLibrary.generated.h"

/** 可重复生成项目默认鱼行为 StateTree 的编辑器工具；运行时模块不依赖任何 Editor API。 */
UCLASS()
class CATFISHINGEDITOR_API UCatFishStateTreeAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Fishing")
	static bool CreateOrUpdateDefaultFishBehaviorStateTree();

	/** 重建并编译默认钓鱼会话树；包含 WindowExpired -> Waiting 的可重复咬钩循环。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Fishing")
	static bool CreateOrUpdateDefaultFishingSessionStateTree();
};
