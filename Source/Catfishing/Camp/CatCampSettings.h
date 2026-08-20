#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatCampSettings.generated.h"

/** 固定营地交互范围配置；没有建造、装饰、成长或搬迁字段。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fixed Camp"))
class CATFISHING_API UCatCampSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 统一裁决休息、救援等固定营地入口能否开放；缺 gate 或合法范围时返回 false，使各入口以同一边界 fail-closed。 */
	bool IsRuntimeReady() const;

	/** 固定营地运行 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableCampRuntime = false;

	/**
	 * 休息、鱼缸转移和篝火交互的世界距离上限，单位厘米；默认 0 代表这个数值还没被产品裁定，此时 IsRuntimeReady 返回
	 * false，整套营地命令连同 bEnableCampRuntime 一起 fail-closed，因此不存在“已裁为 0 半径”的合法配置。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (ClampMin = "0.0"))
	double InteractionRadiusCentimeters = 0.0;

	/** 结算夜提交的固定篝火封面事件 ID；封面参与者取当晚真正围坐的玩家，不要求全员在场。None 时每晚篝火回看照常广播，只是不产生相册封面成像计划。 */
	UPROPERTY(Config, EditAnywhere, Category = "Campfire")
	FName CampfireCoverEventId = NAME_None;
};
