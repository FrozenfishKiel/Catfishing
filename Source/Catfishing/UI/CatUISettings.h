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
	/** 返回 Lake 状态 View 是否显式启用；关闭时 UI 子系统仍维护 Online 生命周期但不订阅玩法宿主。 */
	bool IsLakeStatusViewEnabled() const;

	/** Lake 只读状态 View 总开关；默认关闭，正式项目配置或资产接线后再启用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnableLakeStatusView = false;
};
