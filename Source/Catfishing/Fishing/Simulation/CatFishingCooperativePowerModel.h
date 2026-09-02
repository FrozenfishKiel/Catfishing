#pragma once

#include "CoreMinimal.h"

/** 多人搏斗蓄力与猫体力的纯数值配置；不读取 Actor、输入组件或 World 时间。 */
struct CATFISHING_API FCatFightPowerTuning
{
	double ChargeSeconds = 2.0;
	double DecaySeconds = 1.0;
	double HelperMaximumPowerAlpha = 0.75;
	double PrimaryStaminaDrainPerSecondAtFullPower = 10.0;
	double HelperStaminaDrainMultiplier = 1.5;
	double DisruptionPrimaryDrainShare = 0.5;

	bool IsValid() const;
};

/** 一名参与者完成一个固定步后的蓄力结果。PowerAlpha 使用真实 0..1 比例，辅助位上限默认 0.75。 */
struct CATFISHING_API FCatFightPowerStepResult
{
	bool bSucceeded = false;
	double PowerAlpha = 0.0;
	double StrengthContribution = 0.0;
	double StaminaDrainPerSecond = 0.0;
};

/**
 * 多人蓄力纯模型：按住在 ChargeSeconds 内线性上升，松开在 DecaySeconds 内线性下降。
 * 主位和辅助位只在上限及体力倍率上不同；捣乱附加消耗由独立函数按所有辅助者消耗合计计算。
 */
class CATFISHING_API FCatFishingCooperativePowerModel
{
public:
	static FCatFightPowerStepResult StepParticipant(const FCatFightPowerTuning& Tuning,
		double FixedStepSeconds, double CurrentPowerAlpha, bool bPullHeld, bool bPrimary,
		double BaseFishingStrength);

	static double ComputePrimaryDisruptionDrainPerSecond(const FCatFightPowerTuning& Tuning,
		double PrimaryPowerAlpha, double CombinedHelperDrainPerSecond);
};
