#pragma once

#include "CoreMinimal.h"

struct CATFISHING_API FCatFightWorkInput
{
	double Strength = 0.0;
	double IntendedLineDistanceCentimeters = 0.0;
	double ActualLineDistanceCentimeters = 0.0;
	double IsometricEffortMultiplier = 1.0;
	double CostPerStrengthCentimeter = 0.0;
	double PhaseMultiplier = 1.0;
	/** 无对抗负载时的基础努力系数；猫保留 1，鱼固定为 0，自由游动不扣体。 */
	double BaseEffortMultiplier = 1.0;
	/** 自身主动努力承受的相对负载；被动位移不生成努力。 */
	double NormalizedLoad = 0.0;
	double LoadStaminaMultiplier = 0.0;
};

/** 主动努力统一结算：完成与受阻努力分别折算，再按自身负载与独立成本扣体。 */
class CATFISHING_API FCatFishingFightWorkModel
{
public:
	static bool ComputeDrain(const FCatFightWorkInput& Input, double& OutDrain,
		double& OutEffectiveEffortDistanceCentimeters);
};
