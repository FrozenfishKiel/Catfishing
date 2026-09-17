#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatCampInventoryAuthoringLibrary.generated.h"

/** 团队库存布局迁移的编辑器清理边界；只处理本次专属 WBP，不进入运行时。 */
UCLASS()
class CATFISHINGEDITOR_API UCatCampInventoryAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 收尾团队布局替换，避免孤立模板和旧变量 GUID 导致正式 WBP 重载失败；保留已知背景引用，其他变量节点或属性绑定仍依赖旧控件时拒绝删除。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Inventory")
	static bool FinalizeCampLayout(class UBlueprint* Blueprint);
};
