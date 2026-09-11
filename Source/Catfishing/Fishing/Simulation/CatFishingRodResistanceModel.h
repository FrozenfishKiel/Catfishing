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

struct CATFISHING_API FCatFishingRodRotationInput
{
	FRotator CurrentAim = FRotator::ZeroRotator;
	FRotator RequestedAim = FRotator::ZeroRotator;
	/** 鼠标正在移动且输入未超时才允许主动转杆；不改变猫的容量、鱼力或已有角速度。 */
	bool bCatDriveActive = false;
	FVector PullAxis = FVector::ForwardVector;
	/** 上一帧已应用的有向鱼线负载，跨固定步保持；不是额外的鱼端驱动力。 */
	FVector PreviousSmoothedFishPullStrengthMeters = FVector::ZeroVector;
	/** 上一实际步的世界空间角速度；预测只读副本，单位 rad/s。 */
	FVector PreviousAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	double CatTorqueCapacity = 0.0;
	double MaximumFishTorque = 0.0;
	double MaximumAngularSpeedDegreesPerSecond = 360.0;
	double ResponseSeconds = 0.08;
	/** 归一化转矩作用下的等效转动惯性时间，单位秒，不是物理 kg*m^2。 */
	double AngularInertiaSeconds = 0.08;
	double FishPullSmoothingSeconds = 0.15;
	/** 鱼负载下追加的粘性阻尼倍率；实际阻尼至少满足空载瞄准的临界阻尼。 */
	double LoadedAngularDampingRatio = 3.0;
	/** 身体俯仰限位；实际运动与鱼线候选预测共用，不属于瞄准输入限幅。 */
	double MinimumPitchDegrees = -89.0;
	double MaximumPitchDegrees = 89.0;
	double DeltaSeconds = 0.0;
};

struct CATFISHING_API FCatFishingRodRotationResult
{
	bool bSucceeded = false;
	FRotator ActualAim = FRotator::ZeroRotator;
	FVector NetTorque = FVector::ZeroVector;
	FVector SmoothedFishPullStrengthMeters = FVector::ZeroVector;
	FVector AngularVelocityRadiansPerSecond = FVector::ZeroVector;
	/** 最后亚步的实际速度变化率，含身体限位的制动。 */
	FVector AngularAccelerationRadiansPerSecondSquared = FVector::ZeroVector;
	bool bHitPitchLimit = false;
	double AngularSpeedDegreesPerSecond = 0.0;
	/** 本步最后一个亚步实际使用的阻尼倍率，供开发包诊断。 */
	double AppliedAngularDampingMultiplier = 1.0;
	/** 主动转矩/自身容量的平方随时间积分；受阻仍支撑，不借用最大转速收费。 */
	double CatExertionSquaredSeconds = 0.0;
	/** 沿主动转矩方向完成的真实转角 × 主动转矩/自身容量；反向被拖不计正功。 */
	double CatPositiveWorkRadians = 0.0;
	double IntegratedSeconds = 0.0;
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

/** Original held-aim rotation and effort integration; body movement remains in Chaos. */
class CATFISHING_API FCatFishingRodResistanceModel
{
public:
	static FCatFishingRodResistanceResult Evaluate(const FCatFishingRodResistanceInput& Input);
	static FCatFishingRodRotationResult StepRotation(const FCatFishingRodRotationInput& Input);
};
