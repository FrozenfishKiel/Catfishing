#pragma once

#include "CoreMinimal.h"

/** 同一固定步的主动意图与实际物理位移；距离为 cm，费率为每米未完成意图的体力点数。 */
struct CATFISHING_API FCatFightFishIntentInput
{
	/** 当前出力对应的期望速度乘步长，不使用单步自由加速预测位置差。 */
	FVector IntendedDisplacementCentimeters = FVector::ZeroVector;
	/** 最终实际位移减去历史位置纠偏；保留真实受阻和反向拖行。 */
	FVector ActualDisplacementCentimeters = FVector::ZeroVector;
	double StaminaPerUnfulfilledMeter = 0.0;
};

/** 沿主动意图衡量进度；反向进度为负，未完成距离可以大于意图长度。 */
struct CATFISHING_API FCatFightFishIntentResult
{
	double IntendedDistanceCentimeters = 0.0;
	double ActualProgressCentimeters = 0.0;
	/** 不超过 UE_DOUBLE_SMALL_NUMBER cm 的正缺失按舍入误差归零；不对费用金额设最小门槛。 */
	double UnfulfilledDistanceCentimeters = 0.0;
	double StaminaDrain = 0.0;
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

/** 猫结算实际做功，时间支撑由模拟器统一去重；鱼按未完成意图距离独立计价。 */
class CATFISHING_API FCatFishingFightWorkModel
{
public:
	static bool ComputeCatWorkDrain(const FCatFightCatWorkInput& Input, double& OutDrain);
	/** 零意图返回零结果；输入或计算结果非法时返回 false 并清空结果。 */
	static bool ComputeFishIntentDrain(const FCatFightFishIntentInput& Input, FCatFightFishIntentResult& OutResult);
};
