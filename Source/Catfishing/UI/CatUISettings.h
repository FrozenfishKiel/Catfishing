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

	/** 返回开发期白盒命令面板是否显式启用；关闭时 UI 子系统只装配只读状态 View，不创建任何能发 RPC 的按钮。 */
	bool IsCommandPanelEnabled() const;

	/** Lake 只读状态 View 总开关；默认关闭，正式项目配置或资产接线后再启用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnableLakeStatusView = false;

	/** 开发期白盒命令面板开关：开着时 Lake 里会多一块按钮面板，把玩法 RPC 一条条发出去供人调试；它不是正式 UI，默认关闭，正式 UI 到位后整项删除。 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	bool bEnableCommandPanel = false;
};
