#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatConditionSettings.generated.h"

/** 猫状态的显式数值配置；中毒/恢复未裁值继续关闭，水深阈值在总 gate 开启时驱动 Wet 与危险水域。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Conditions"))
class CATFISHING_API UCatConditionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决服务器是否拥有可比较的中毒倒地阈值；未配置时返回 false，使 Condition 不猜测产品边界。 */
	bool HasDownedThresholds() const;
	/** 水深阈值必须有限、危险退出线低于进入线且确认时长非负。 */
	bool HasWaterExposureThresholds() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/** Poison 达到该值时倒地；0 表示未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	double PoisonDownedThreshold = 0.0;

	/** 脚点低于水面达到该深度后进入湿润表现。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double WetWaterDepthCentimeters = 1.0;
	/** 脚点达到该浸没深度并持续确认后，Condition 发布 Dangerous。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterDepthCentimeters = 45.0;
	/** 已危险后退回该深度以下才退出，避免水面抖动反复切换。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterExitDepthCentimeters = 35.0;
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "s"))
	double DangerousWaterConfirmationSeconds = 0.2;

	/** 野外自救一次减少的 Poison；0 表示路径未调好并拒绝命令。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double FieldRestPoisonRelief = 0.0;

	/** 营地休息一次减少的 Poison；应由正式调参保证快于野外，本类不偷偷修正。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double CampRestPoisonRelief = 0.0;

	/** 草药一次减少的 Poison；0 表示未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double HerbPoisonRelief = 0.0;

	/** 服务器允许施药者与目标之间的最大世界距离，单位厘米；0 表示未裁并拒绝草药命令。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double HerbUseRangeCentimeters = 0.0;
};
