#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatAltarConfirmationAuthoringLibrary.generated.h"

/** 祭坛确认窗口的编辑器资产入口；负责首份正式布局，运行时只加载保存后的 WBP。 */
UCLASS()
class CATFISHINGEDITOR_API UCatAltarConfirmationAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 创建缺失的正式顶部窗口；既有资产只检查绑定，避免覆盖设计者调整过的布局。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Run")
	static bool CreateMissingAltarConfirmationWidgetBlueprint();
};
