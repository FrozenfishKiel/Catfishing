#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatConditionSettings.generated.h"

/** 猫状态的显式数值配置；默认全为 Unset，结构存在但不会制造死亡、恢复或 Wet 惩罚。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Conditions"))
class CATFISHING_API UCatConditionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决服务器是否拥有可比较的中毒倒地阈值；未配置时返回 false，使 Condition 不猜测产品边界。 */
	bool HasDownedThresholds() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/** Poison 达到该值时倒地；0 表示未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	double PoisonDownedThreshold = 0.0;

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
