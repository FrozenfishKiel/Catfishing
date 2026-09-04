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
	/** 上一帧已应用的有向鱼线负载，跨固定步保持；不是额外的鱼端驱动力。 */
	FVector PreviousSmoothedFishPullStrengthMeters = FVector::ZeroVector;
	double CatTorqueCapacity = 0.0;
	double MaximumFishTorque = 0.0;
	double MaximumAngularSpeedDegreesPerSecond = 360.0;
	double ResponseSeconds = 0.08;
	double FishPullSmoothingSeconds = 0.15;
	double DeltaSeconds = 0.0;
};

struct CATFISHING_API FCatFishingRodRotationResult
{
	bool bSucceeded = false;
	FRotator ActualAim = FRotator::ZeroRotator;
	FVector NetTorque = FVector::ZeroVector;
	FVector SmoothedFishPullStrengthMeters = FVector::ZeroVector;
	double AngularSpeedDegreesPerSecond = 0.0;
	/** 猫主动转矩按自身容量归一化后的意图弧长，使用一米参考力臂；受阻时仍累积。 */
	double CatIntentArcCentimeters = 0.0;
	/** 沿猫主动转矩方向完成的弧长，上限为对应意图；鱼的被动拖动或额外助力不记入。 */
	double CatActualArcCentimeters = 0.0;
	double IntegratedSeconds = 0.0;
};

/** 权威旋转积分的累计观察量；同一 Epoch 求差，换持有人或搏斗生命周期后重新计数。 */
struct CATFISHING_API FCatFishingRodRotationEffortSnapshot
{
	uint64 Epoch = 0;
	double IntentArcCentimeters = 0.0;
	double ActualArcCentimeters = 0.0;
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

/** 鱼线负载先连续插值，再做有阻尼的转矩对抗；不保存锁定状态，也不裁剪允许角度。 */
class CATFISHING_API FCatFishingRodResistanceModel
{
public:
	static FCatFishingRodResistanceResult Evaluate(const FCatFishingRodResistanceInput& Input);
	static FCatFishingRodRotationResult StepRotation(const FCatFishingRodRotationInput& Input);
};
