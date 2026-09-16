#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryQuickbarAuthoringLibrary.generated.h"

/** 快捷栏正式资产作者器；仅在编辑器内创建独立快捷栏格、恢复共享背包格并迁移既有 IMC 与输入配置，不参与运行时库存。 */
UCLASS()
class CATFISHINGEDITOR_API UCatInventoryQuickbarAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 创建或校验 WBP_CatInventoryQuickbar 及其独立格资产；共享背包格只在完整旧结构匹配时移除历史快捷栏控件。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool CreateOrValidateInventoryQuickbarWidgets();

	/** 迁移正式 IMC 和 Native Input Config 到统一选择/使用/丢弃动作；选杆即装备，左键统一使用，R 架竿、X 收竿；移除 G 及旧 Q/R/F/X 映射。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateBackpackQuickbarInputAssets();
};
