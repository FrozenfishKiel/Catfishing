#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"

/**
 * 冻结自鱼战斗性格 DA 的游向配置。随机只决定“下一段想往哪里游”，不直接随机世界位置。
 * Runner 在服务器持有 FRandomStream；同一输入种子、固定步长和配置会得到完全相同的方向序列。
 */
struct CATFISHING_API FCatFishSteeringConfig
{
	FVector2D RetargetDurationRangeSeconds = FVector2D(0.6, 1.4);
	double MaximumTurnRateDegreesPerSecond = 120.0;
	double StruggleOutwardBias = 0.75;
	double CalmInwardBias = 0.65;
	double LateralMovementBias = 0.45;
	double FeintProbability = 0.1;
	double FullStaminaInwardProbability = 0.1;
	double ExhaustedInwardProbability = 0.8;
	double InwardProbabilityExponent = 1.35;
	double InwardConeHalfAngleDegrees = 60.0;

	bool IsValid() const;
};

/** 跨固定步保存的相关随机（correlated random）状态；避免每帧白噪声造成鱼抖动。 */
struct CATFISHING_API FCatFishSteeringState
{
	FVector CurrentDirection = FVector::ForwardVector;
	FVector TargetDirection = FVector::ForwardVector;
	double RetargetSecondsRemaining = 0.0;
	ECatFishMotionIntent LastMotionIntent = ECatFishMotionIntent::None;
	bool bInitialized = false;
	/** 最近真实岸线反馈；强制外冲也需暂时保持朝水内的安全半平面。 */
	FVector BoundaryWaterwardDirection = FVector::ZeroVector;
	double BoundaryAvoidanceSecondsRemaining = 0.0;
};

/** 无 World、无 Actor 的确定性鱼游向模型。 */
class CATFISHING_API FCatFishSteeringModel
{
public:
	static bool Initialize(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		ECatFishMotionIntent MotionIntent, double FishStaminaRatio, FRandomStream& Random,
		FCatFishSteeringState& InOutState);

	static bool Step(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		ECatFishMotionIntent MotionIntent, double FishStaminaRatio, double DeltaSeconds, FRandomStream& Random,
		FCatFishSteeringState& InOutState, FVector& OutDesiredDirection, bool bForceOutward = false);

	/** 纯函数：把剩余体力比例 [0,1] 映射为本次平静重选落入向内 60°扇区的概率。 */
	static double ComputeInwardProbability(const FCatFishSteeringConfig& Config, double FishStaminaRatio);

	/** 活鱼的候选位置被真实岸线修正时，把当前/目标游向反射回水里，避免持续朝岸导致原地钳制。 */
	static bool RedirectFromWaterBoundary(const FCatFishSteeringConfig& Config,
		const FVector& WaterwardDirection, FRandomStream& Random, FCatFishSteeringState& InOutState);

private:
	static bool SelectTarget(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		ECatFishMotionIntent MotionIntent, double FishStaminaRatio, FRandomStream& Random,
		FCatFishSteeringState& InOutState);
};
