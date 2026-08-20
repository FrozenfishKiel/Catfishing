#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "CatUISettings.generated.h"

class UCatSurvivalWidget;
class UCatTravelWidget;

/** 正式 LocalPlayer UI 的显式运行设置；只控制 View 是否装配、由哪个控件类承载，不携带任何领域数值或权限。 */
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

	/**
	 * Lake 状态 View 由哪一份控件蓝图（WBP）来画。它表达的是"这块 HUD 的外观归谁"，不表达它显示什么内容——
	 * 内容始终由 UCatLocalPlayerUISubsystem 投影成 FCatSurvivalViewState 后调 Render 送进去，换皮不改数据来源。
	 * 只有 UI 子系统装配状态 View 时读一次；留空（当前项目就是留空）表示美术资产还没做出来，此时退回 C++ 本体
	 * UCatSurvivalWidget，而不是让整块 HUD 消失。
	 * 用软类引用而不是硬引用：这份设置在引擎启动很早就被加载，硬引用会把控件蓝图连同它引用的字体、贴图一起
	 * 拽进内存，而实际用到它的时机要晚得多。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lake")
	TSoftClassPtr<UCatSurvivalWidget> SurvivalWidgetClass;

	/**
	 * 联机/旅行界面由哪一份控件蓝图来画。与 SurvivalWidgetClass 同一套口径：只决定外观载体，Host/Find/Join/Invite/Leave
	 * 的意图广播和 Online 快照消费仍在 C++ 本体 UCatTravelWidget 里。
	 * 留空时退回 UCatTravelWidget；软引用理由同上，避免早加载的设置对象牵出整条美术资产链。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Online")
	TSoftClassPtr<UCatTravelWidget> TravelWidgetClass;
};
