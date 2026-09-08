#pragma once

#include "CoreMinimal.h"

/** 鱼的连续出力费用；实际出力与有效对抗均无量纲，费率为体力点/s。 */
struct CATFISHING_API FCatFightFishEffortInput
{
	double EffortRatio = 0.0;
	double OppositionRatio = 0.0;
	double StaminaPerSecond = 0.0;
	double DeltaSeconds = 0.0;
};

/** 猫端实际做功单位为标准力量·cm 或标准转矩·rad，必须传入对应单价。 */
struct CATFISHING_API FCatFightCatWorkInput
{
	double PositiveWorkUnits = 0.0;
	double CostPerWorkUnit = 0.0;
	double NormalizedLoad = 0.0;
	double UnloadedWorkMultiplier = 0.15;
	double LoadStaminaMultiplier = 1.0;
	double ActionMultiplier = 1.0;
};

/** 猫结算实际做功，时间支撑由模拟器统一去重；鱼保留独立对抗努力计价。 */
class CATFISHING_API FCatFishingFightWorkModel
{
public:
	static bool ComputeCatWorkDrain(const FCatFightCatWorkInput& Input, double& OutDrain);
	static bool ComputeFishEffortDrain(const FCatFightFishEffortInput& Input, double& OutDrain);
};
