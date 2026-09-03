#pragma once

#include "CoreMinimal.h"

/** 鱼线对手持鱼竿旋转自由度施加的转矩输入；杆长来自玩法参数，不读取 Mesh 尺寸。 */
struct CATFISHING_API FCatFishingRodResistanceInput
{
	double CatStrength = 0.0;
	double FishStrength = 0.0;
	double RodPhysicsLengthCentimeters = 0.0;
	double NormalizedTension = 0.0;
	double NormalizedFishLineLoad = 0.0;
	/** 竿身与鱼线方向夹角的余弦，[0,1]；垂直鱼线时转矩最大。 */
	double RodLineAlignment = 1.0;
};

struct CATFISHING_API FCatFishingRodResistanceResult
{
	bool bSucceeded = false;
	double FishResistingTorqueStrengthMeters = 0.0;
	double CatTorqueCapacityStrengthMeters = 0.0;
	double TorqueLoadRatio = 0.0;
	double RotationSpeedMultiplier = 1.0;
	bool bRotationStalled = false;
};

/** 无状态旋转阻力模型：鱼转矩逐步压低实际杆速，达到猫转矩能力时自然停转。 */
class CATFISHING_API FCatFishingRodResistanceModel
{
public:
	static FCatFishingRodResistanceResult Evaluate(const FCatFishingRodResistanceInput& Input);
};
