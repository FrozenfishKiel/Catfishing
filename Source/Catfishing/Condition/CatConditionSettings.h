#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatConditionSettings.generated.h"

/**
 * 猫状态的显式数值配置；默认 Unset，结构存在但不会制造死亡或 Wet 惩罚。疲惫数值制已按飞书猫咪状态册（2026-08-15）删
 * 除，恢复路径不再带任何减量配置。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Conditions"))
class CATFISHING_API UCatConditionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决服务器是否拥有可比较的倒地阈值（仅 Poison）；未配置时返回 false，使 Condition 不猜测产品边界。 */
	bool HasDownedThresholds() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/** Poison 达到该值时倒地；0 表示未裁。倒地来源仅中毒（飞书猫咪状态册 v1.4）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	double PoisonDownedThreshold = 0.0;
};
