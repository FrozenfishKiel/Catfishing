#pragma once

#include "CoreMinimal.h"

/** 同一固定步的主动意图与实际物理位移；距离为 cm，费率为每米未完成意图的体力点数。 */
struct CATFISHING_API FCatIntentMotionInput
{
	/** 当前出力对应的期望速度乘步长，不使用单步自由加速预测位置差。 */
	FVector IntendedDisplacementCentimeters = FVector::ZeroVector;
	/** 最终实际位移减去历史位置纠偏；保留真实受阻和反向拖行。 */
	FVector ActualDisplacementCentimeters = FVector::ZeroVector;
	double StaminaPerUnfulfilledMeter = 0.0;
};

/** 沿主动意图衡量进度；反向进度为负，未完成距离可以大于意图长度。 */
struct CATFISHING_API FCatIntentMotionResult
{
	double IntendedDistanceCentimeters = 0.0;
	double ActualProgressCentimeters = 0.0;
	/** 不超过 UE_DOUBLE_SMALL_NUMBER cm 的正缺失按舍入误差归零；不对费用金额设最小门槛。 */
	double UnfulfilledDistanceCentimeters = 0.0;
	double StaminaDrain = 0.0;
};

/** Shared by fish swimming and cat locomotion/support; no actors, resources or side effects. */
class CATFISHING_API FCatIntentMotionModel
{
public:
	static bool ComputeDrain(const FCatIntentMotionInput& Input, FCatIntentMotionResult& OutResult);
};
