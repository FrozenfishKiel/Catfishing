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
};

/** 统一做功模型：实际位移计费；没有实现的意图位移也按等长系数计费，因此僵持自然消耗体力。 */
class CATFISHING_API FCatFishingFightWorkModel
{
public:
	static bool ComputeDrain(const FCatFightWorkInput& Input, double& OutDrain,
		double& OutEffectiveEffortDistanceCentimeters);
};
