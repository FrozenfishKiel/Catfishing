#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatFrontendWidgetAuthoringLibrary.generated.h"

/**
 * 正式 Frontend WBP 的编辑器构造入口；只在编辑器中创建或修正目标资产并保存，不向运行时模块泄漏 UMGEditor API。
 * 主线程或资产脚本调用它建立首份可编辑控件树；普通子页面保留人工布局，Root 与 Loading 会在合同变化时重建来移除旧加载子页。
 */
UCLASS()
class CATFISHINGEDITOR_API UCatFrontendWidgetAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 创建当前缺失的 Frontend 子页面，并重建 Root 与全局 Loading WBP 来落实最新 C++ / BindWidget 合同。
	 * 该入口由编辑器内的资产构造脚本调用；成功时相关包已经编译、登记并保存，普通业务子页面不会被重复执行覆盖。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Frontend")
	static bool CreateMissingFrontendWidgetBlueprints();

	/**
	 * 创建或重建局内 ESC 菜单 WBP，并核验它继承局内菜单 View 基类且提供返回、设置、保存、退出和设置页控件。
	 * 该入口沿用项目正式 WBP 作者链路；当前菜单资产由本工具生成，升级时重建同一路径以清除旧三按钮残留。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Lake")
	static bool CreateMissingLakeMainMenuWidgetBlueprint();

	/**
	 * 修复已存在正式 Frontend WBP 的中文字体引用；它只替换文本类控件的字体对象，不改控件树、绑定名、布局和蓝图逻辑。
	 * 该入口由资产构造脚本在缺失资产创建后调用；成功时所有前端中文文案都引用项目中文 Font 资产，避免预览和打包运行时显示缺字占位。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Frontend")
	static bool RepairFrontendWidgetBlueprintFonts();

	/**
	 * 核验已存在正式 Frontend WBP 的文本字体引用；它只读取文本类控件并输出错误日志，不保存资产。
	 * 该入口给资产构造脚本提供自动防线，避免中文字体修复失败后仍把缺字占位界面当作成功生成。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Frontend")
	static bool ValidateFrontendWidgetBlueprintFonts();

	/**
	 * 创建 Frontend 设置使用的 SoundMix 与五个 SoundClass；仅接受全新或完整已存在的资源集，避免对美术已调校的部分音频树做猜测性修补。
	 * Settings Model 通过固定软引用加载这些正式资源，并在当前 World 的 AudioDevice 上施加音量覆盖；本方法不写入运行时设置或 Cook 配置。
	 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Authoring|Frontend")
	static bool CreateMissingFrontendAudioSettingsAssets();
};
