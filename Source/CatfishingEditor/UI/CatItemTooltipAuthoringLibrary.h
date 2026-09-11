#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatItemTooltipAuthoringLibrary.generated.h"

/**
 * Aegis 物品悬停框迁移期间使用的编辑器资产辅助入口；它只为一次性导入提供旧父类解析与 WidgetTree 合同补齐，不进入运行时模块或配置重定向。
 * Python 迁移脚本先调用重定向入口加载旧 WBP，再调用收尾入口将副本固定为 Catfishing 的父类和控件合同。
 */
UCLASS()
class CATFISHINGEDITOR_API UCatItemTooltipAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 注册仅在本编辑器进程有效的 Aegis 父类重定向，使旧 WBP 第一次反序列化时指向新的 CatItemTooltipWidget。
	 * 迁移脚本必须在加载源包前调用；返回 true 表示本次进程已具备该映射，项目配置和打包运行时不会保存此映射。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|ItemTooltip")
	static bool InstallLegacyParentRedirect();

	/**
	 * 将已复制的 WBP 收尾为正式物品悬停框：固定新的原生父类、补充实例详情文本并编译保存。
	 * 只接受迁移脚本刚创建的目标资产；如果原始布局不包含所需控件或说明文本不在纵向容器内，会失败而不会猜测性重建美术布局。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|ItemTooltip")
	static bool FinalizeMigratedTooltipWidget(UObject* TooltipAsset);
};
