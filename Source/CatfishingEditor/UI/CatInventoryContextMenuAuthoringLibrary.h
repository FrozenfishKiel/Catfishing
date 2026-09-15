#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryContextMenuAuthoringLibrary.generated.h"

/** 库存菜单资产作者器；只在编辑器内创建正式菜单 WBP 并清理既有库存页的废弃固定操作控件。 */
UCLASS()
class CATFISHINGEDITOR_API UCatInventoryContextMenuAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 创建并编译唯一的 WBP_CatInventoryContextMenu；目标已存在时只校验父类和 BindWidget 合同，不覆盖人工布局。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool CreateOrValidateInventoryContextMenuWidget();

	/** 从三个正式库存 WBP 删除旧右键 Use、底部固定操作和数量面板控件，再逐个编译保存。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Authoring|Inventory")
	static bool MigrateInventoryActionWidgets();
};
