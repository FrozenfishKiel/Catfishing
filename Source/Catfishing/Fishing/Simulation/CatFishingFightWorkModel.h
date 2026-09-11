#pragma once

#include "CoreMinimal.h"

/** 收线/转杆实际做功单位为标准力量·cm 或标准转矩·rad，必须传入对应单价。 */
struct CATFISHING_API FCatFightCatWorkInput
{
	double PositiveWorkUnits = 0.0;
	double CostPerWorkUnit = 0.0;
	double NormalizedLoad = 0.0;
	double UnloadedWorkMultiplier = 0.15;
	double LoadStaminaMultiplier = 1.0;
	double ActionMultiplier = 1.0;
};

/** 只结算收线/转杆实际做功；身体移动与鱼共用 CatIntentMotionModel。 */
class CATFISHING_API FCatFishingFightWorkModel
{
public:
	static bool ComputeCatWorkDrain(const FCatFightCatWorkInput& Input, double& OutDrain);
};
