#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatConditionSettings.generated.h"

/**
 * 猫状态的显式数值配置；水深阈值在总 gate 开启时驱动 Wet 与危险水域，倒地自愈时长与爬行速度在这里。
 *
 * 墓碑（2026-09-12）：原有 PoisonDownedThreshold、FieldRestPoisonRelief、CampRestPoisonRelief、
 * HerbPoisonRelief、HerbUseRangeCentimeters 五项全部删除。前三项服务的是「跨鱼累加 Poison、
 * 到阈值 100 倒地、按点数清毒」的渐进模型，09-12 裁决改成按单条鱼的结论直接裁决倒地、恢复即解除；
 * 后两项属于 2026-08-13 已删除的草药机制。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Conditions"))
class CATFISHING_API UCatConditionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Character 状态运行总 gate；关闭时 Condition 不裁决倒地、疲惫档与恢复。 */
	bool IsRuntimeReady() const;
	/** 水深阈值必须有限、危险退出线低于进入线且确认时长非负。 */
	bool HasWaterExposureThresholds() const;
	/** 倒地自愈时长是否已配；未配时倒地不会自己结束，只能靠救援或休息。 */
	bool HasDownedSelfRecovery() const;

	/** Character 状态运行总 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableConditionRuntime = false;

	/**
	 * 倒地满这么久后自己站起来（猫册 §3.1.5，2026-09-07 李前臻拍，占位 60 秒，参数页「祭坛与倒地」行为准）。
	 * 0 表示未配：爬回营地与队友搬运就重新变成唯一出路，全队倒地会卡住一天，所以正式配置必须给正值。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0", Units = "s"))
	double DownedSelfRecoverySeconds = 0.0;

	/**
	 * 倒地时的移动速度倍率（猫册 §3.1.5「倒地者可缓慢爬行」）。
	 * **这个数设计没拍**：参数页没有「爬行速度」这一行，下面的值是工程占位，等策划给数。
	 * 0 表示不许动——那等于把「可缓慢爬行」关掉，所以不要用 0 当未裁标记。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double DownedCrawlSpeedScale = 0.25;

	/** 脚点低于水面达到该深度后进入湿润表现。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double WetWaterDepthCentimeters = 1.0;
	/** 脚点达到该浸没深度并持续确认后，Condition 发布 Dangerous。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterDepthCentimeters = 35.0;
	/** 已危险后退回该深度以下才退出，避免水面抖动反复切换。 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "cm"))
	double DangerousWaterExitDepthCentimeters = 25.0;
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "0.0", Units = "s"))
	double DangerousWaterConfirmationSeconds = 0.2;
};
