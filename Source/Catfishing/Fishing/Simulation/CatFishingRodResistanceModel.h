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
	/** 竿身与鱼线方向夹角的余弦，[-1,1]；垂直鱼线时转矩最大。 */
	double RodLineAlignment = 1.0;
};

struct CATFISHING_API FCatFishingRodResistanceResult
{
	bool bSucceeded = false;
	double FishResistingTorqueStrengthMeters = 0.0;
	double CatTorqueCapacityStrengthMeters = 0.0;
	double MaximumFishTorqueStrengthMeters = 0.0;
};

struct CATFISHING_API FCatFishingRodRotationInput
{
	FRotator CurrentAim = FRotator::ZeroRotator;
	FRotator RequestedAim = FRotator::ZeroRotator;
	FVector PullAxis = FVector::ForwardVector;
	double CatTorqueCapacity = 0.0;
	double MaximumFishTorque = 0.0;
	double MaximumAngularSpeedDegreesPerSecond = 360.0;
	double ResponseSeconds = 0.08;
	double DeltaSeconds = 0.0;
};

struct CATFISHING_API FCatFishingRodRotationResult
{
	bool bSucceeded = false;
	FRotator ActualAim = FRotator::ZeroRotator;
	FVector NetTorque = FVector::ZeroVector;
	double AngularSpeedDegreesPerSecond = 0.0;
};

/** 有阻尼的连续转矩对抗：净转矩为零才停转，不保存锁定状态，也不裁剪允许角度。 */
class CATFISHING_API FCatFishingRodResistanceModel
{
public:
	static FCatFishingRodResistanceResult Evaluate(const FCatFishingRodResistanceInput& Input);
	static FCatFishingRodRotationResult StepRotation(const FCatFishingRodRotationInput& Input);
};
