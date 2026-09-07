#pragma once

#include "CoreMinimal.h"

/** 鱼的对抗努力输入；猫改用实际做功与时间支撑，不能再走受阻距离计费。 */
struct CATFISHING_API FCatFightWorkInput
{
	double Strength = 0.0;
	double IntendedLineDistanceCentimeters = 0.0;
	double ActualLineDistanceCentimeters = 0.0;
	double IsometricEffortMultiplier = 1.0;
	double CostPerStrengthCentimeter = 0.0;
	double PhaseMultiplier = 1.0;
	/** 正式鱼计费固定为 0，自由游动不扣体。 */
	double BaseEffortMultiplier = 1.0;
	/** 自身主动努力承受的相对负载；被动位移不生成努力。 */
	double NormalizedLoad = 0.0;
	double LoadStaminaMultiplier = 0.0;
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
	static bool ComputeDrain(const FCatFightWorkInput& Input, double& OutDrain,
		double& OutEffectiveEffortDistanceCentimeters);
};
