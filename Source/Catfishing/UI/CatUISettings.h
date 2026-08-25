#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatUISettings.generated.h"

/** 正式 LocalPlayer UI 的显式运行设置；只控制 View 是否装配，不携带任何领域数值或权限。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing UI"))
class CATFISHING_API UCatUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 返回 Lake 原生状态 View 是否显式启用；默认玩家路径关闭它，UI 子系统仍维护 Online 生命周期。 */
	bool IsLakeStatusViewEnabled() const;

	/** Lake 原生状态 View 的开发/验证开关；默认关闭，只有显式配置或测试开启时才创建 LakeReach 根，避免玩家进 Lake 自动看到白盒状态文字。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnableLakeStatusView = false;

	/** Lake 菜单 Enhanced Input 使用的键名；UI 子系统把它解析为 FKey，同一值也投影给 UIOnly 焦点下的根 View。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input")
	FName LakeMenuToggleKeyName = TEXT("Tab");

	/** Lake 菜单 Mapping Context 的本地优先级；只影响同一 LocalPlayer 的输入解析，不改变玩法权限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake|Input", meta = (ClampMin = "0"))
	int32 LakeMenuInputPriority = 100;
};
