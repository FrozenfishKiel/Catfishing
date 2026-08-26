#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatUISettings.generated.h"

class UCatLakeReachWidget;

/** 正式 LocalPlayer UI 的显式运行设置；只控制 View 是否装配，不携带任何领域数值或权限。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing UI"))
class CATFISHING_API UCatUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 建立 LakeReach 默认前端契约：没有项目配置覆盖时，玩家 UI 必须指向正式 WBP，而不是退回 C++ 白盒类。 */
	UCatUISettings();

	/** 返回正式 LakeReach View 是否允许装配；关闭时 LocalPlayer 仍维护 Online 生命周期但不创建玩家可见 UIReach。 */
	bool IsLakeReachViewEnabled() const;

	/** 返回配置的正式 LakeReach WBP 类；缺失或未加载时调用方必须 fail-closed，不能回退原生白盒类。 */
	TSubclassOf<UCatLakeReachWidget> LoadLakeReachWidgetClass() const;

	/** 返回准星与交互提示是否显式开启。 */
	bool IsInteractionViewEnabled() const { return bEnableInteractionView; }

	/** LakeReach 正式 View 的装配开关；默认开启后仍要求 LakeReachWidgetClass 有效，关闭只用于测试或临时禁用整条玩家 UIReach。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnableLakeReachView = true;

	/** 正式 LakeReach 玩家前端 WBP 类；默认指向 /Game/UI/WBP_CatLakeReach，缺类时 PageController 不创建原生白盒替身。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	TSoftClassPtr<UCatLakeReachWidget> LakeReachWidgetClass;

	/** Lake 菜单 Enhanced Input 使用的键名；UI 子系统把它解析为 FKey，同一值也投影给 UIOnly 焦点下的根 View。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	FName LakeMenuToggleKeyName = TEXT("Tab");

	/** Lake 菜单 Mapping Context 的本地优先级；只影响同一 LocalPlayer 的输入解析，不改变玩法权限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input", meta = (ClampMin = "0"))
	int32 LakeMenuInputPriority = 100;

	/** 中央准星与交互提示；只在当前 LocalPlayer 已占有项目 Character 时创建。 */
	UPROPERTY(Config, EditAnywhere, Category="Interaction")
	bool bEnableInteractionView = true;
};
