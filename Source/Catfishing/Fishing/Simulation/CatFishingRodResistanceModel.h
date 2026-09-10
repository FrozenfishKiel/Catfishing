#pragma once

#include "CoreMinimal.h"

/** 鱼线对手持鱼竿旋转自由度施加的转矩输入；杆长来自玩法参数，不读取 Mesh 尺寸。 */
struct CATFISHING_API FCatFishingRodResistanceInput
{
	double CatStrength = 0.0;
	double LineTensionNewtons = 0.0;
	double ForcePerStrengthNewtons = 1.0;
	double RodPhysicsLengthCentimeters = 0.0;
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

/** 权威旋转积分的累计观察量；同一 Epoch 求差，换持有人或搏斗生命周期后重新计数。 */
struct CATFISHING_API FCatFishingRodRotationEffortSnapshot
{
	uint64 Epoch = 0;
	double ExertionSquaredSeconds = 0.0;
	double PositiveWorkRadians = 0.0;
	double IntegratedSeconds = 0.0;
};

/** 将帧积分累计量按时间分配给固定步；低帧率追赶时不能在第一步吃完后让后续步重复收费。 */
class CATFISHING_API FCatFishingRodEffortSampler
{
public:
	/** 接入既有累计快照时建立基线，清除之前尚未消费的努力。 */
	void Reset(const FCatFishingRodRotationEffortSnapshot& Snapshot);
	/** 返回本固定步分配量；同一快照只消费剩余积压，Epoch 变化先丢弃旧持有人的积压。 */
	FCatFishingRodRotationEffortSnapshot Consume(
		const FCatFishingRodRotationEffortSnapshot& Snapshot, double StepSeconds);

private:
	FCatFishingRodRotationEffortSnapshot PreviousSnapshot;
	FCatFishingRodRotationEffortSnapshot PendingEffort;
};

/** Pure lever-arm observation for effort pricing and diagnostics; never applies torque or moves a shaft. */
class CATFISHING_API FCatFishingRodResistanceModel
{
public:
	static FCatFishingRodResistanceResult Evaluate(const FCatFishingRodResistanceInput& Input);
};
