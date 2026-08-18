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
	/** 裁决服务器是否拥有可比较的倒地阈值；任一值未配置时返回 false，使 Condition 不猜测 Hunger/Fatigue 产品边界。 */
	bool HasDownedThresholds() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/** Poison 达到该值时倒地；0 表示未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	double PoisonDownedThreshold = 0.0;

	/** Fatigue 达到该值时倒地；0 表示未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	double FatigueDownedThreshold = 0.0;

	/** 野外自救一次减少的 Fatigue；0 表示路径未调好并拒绝命令。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double FieldRestFatigueRelief = 0.0;

	/** 营地休息一次减少的 Fatigue；应由正式调参保证快于野外，本类不偷偷修正。 */
	UPROPERTY(Config, EditAnywhere, Category = "Recovery", meta = (ClampMin = "0.0"))
	double CampRestFatigueRelief = 0.0;

};
