#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatDayTransitionAuthoringLibrary.generated.h"

/** 正式翻天 WBP 的编辑器资产构造入口；只在目标资产缺失时创建首份 WidgetTree，已存在资产只读核验以保护人工调整的布局。 */
UCLASS()
class CATFISHINGEDITOR_API UCatDayTransitionAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 编辑器创建缺失的 /Game/UI/Run/WBP_CatDayTransition，或只读核验既有编译状态、生成类和可达绑定；失败返回 false，既有人工布局不被覆盖。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Run")
	static bool CreateMissingDayTransitionWidgetBlueprint();
};
