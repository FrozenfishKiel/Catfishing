#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatWorldInfoAuthoringLibrary.generated.h"

/** 正式世界信息牌的编辑器资产创作入口；只为缺失 WBP 写入首份布局，后续视觉编辑归 Designer 所有。 */
UCLASS()
class CATFISHINGEDITOR_API UCatWorldInfoAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 创建缺失的 WorldInfo 行和面板 WBP，或只读核验既有资产；失败返回 false 并记录原因，不覆盖人工布局，不保存地图。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|WorldInfo")
	static bool CreateMissingWorldInfoWidgetBlueprints();
};
