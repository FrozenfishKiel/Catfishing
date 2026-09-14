#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatConditionSettings.generated.h"

/** 猫状态的水深阈值配置；Condition 根据这些数值判定湿身、浅水与危险水域。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Conditions"))
class CATFISHING_API UCatConditionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 水深阈值必须有限、危险退出线低于进入线且确认时长非负。 */
	bool HasWaterExposureThresholds() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/** 脚点低于水面达到该深度后进入湿润表现。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double WetWaterDepthCentimeters = 1.0;
	/** 脚点达到该浸没深度并持续确认后，Condition 发布 Dangerous。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterDepthCentimeters = 35.0;
	/** 已危险后退回该深度以下才退出，避免水面抖动反复切换。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterExitDepthCentimeters = 25.0;
	/** 脚点持续超过危险水深多久后才发布 Dangerous，单位秒；0 表示命中即确认。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "s"))
	double DangerousWaterConfirmationSeconds = 0.2;
};
