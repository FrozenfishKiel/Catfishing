#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatCollectionAuthoringLibrary.generated.h"

/** 图鉴旧文本布局的定向资产迁移；仅编辑器使用，保留正式 WBP 路径及所有外部引用。 */
UCLASS()
class UCatCollectionAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** 检查已知旧控件名单、绑定与事件连线后断开旧根树，重编译并保存原 WBP；预检不通过拒绝修改，编译失败不保存但可能留有内存改动。 */
    UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring")
    static bool MigrateCollectionWidget();
};
