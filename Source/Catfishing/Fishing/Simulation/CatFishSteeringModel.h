#pragma once

#include "CoreMinimal.h"
#include "Fishing/Behavior/CatFishBehaviorTypes.h"
#include "CatFishSteeringModel.generated.h"

/** 鱼种冻结的行为执行参数。策略转换只在 StateTree 中配置，数值不另建第二套阶段机。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishSteeringConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Direction")
	FVector2D RetargetDurationRangeSeconds = FVector2D(0.6, 1.4);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Direction", meta=(ClampMin="1", Units="deg/s"))
	double MaximumTurnRateDegreesPerSecond = 120.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Direction", meta=(ClampMin="0", ClampMax="75", Units="deg"))
	double OutwardAngularSpreadDegrees = 25.0;
	/** 横切方向中保留多少向外分量；线方向改变时保持同一侧，形成连续弧线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Direction", meta=(ClampMin="0", ClampMax="1"))
	double LateralOutwardBias = 0.9;
	/** 缓游向内混合权重；默认0保持横游，只降低出力，避免主动帮玩家收线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Direction", meta=(ClampMin="0", ClampMax="1"))
	double EaseOffInwardBias = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effort")
	FVector2D OutwardEffortRange = FVector2D(0.8, 1.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effort")
	FVector2D LateralEffortRange = FVector2D(0.75, 0.95);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effort")
	FVector2D EaseOffEffortRange = FVector2D(0.3, 0.45);
	/** 单位为出力比例/秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effort", meta=(ClampMin="0.01"))
	double EffortRisePerSecond = 0.8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effort", meta=(ClampMin="0.01"))
	double EffortFallPerSecond = 0.6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration")
	FVector2D OutwardDurationRangeSeconds = FVector2D(2.0, 4.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration")
	FVector2D LateralDurationRangeSeconds = FVector2D(1.5, 3.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration")
	FVector2D EaseOffDurationRangeSeconds = FVector2D(1.25, 2.0);
	/** 连续外冲/横切的总时限；换路线不重置，只在缓游后重新开始一轮。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration")
	FVector2D ActiveBoutDurationRangeSeconds = FVector2D(6.0, 10.0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration", meta=(ClampMin="0", Units="s"))
	double MinimumBehaviorDurationSeconds = 1.25;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback", meta=(ClampMin="0", ClampMax="1"))
	double LowStaminaRatio = 0.3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration", meta=(ClampMin="0.1", ClampMax="1"))
	double LowStaminaActiveDurationMultiplier = 0.7;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Duration", meta=(ClampMin="1"))
	double LowStaminaEaseOffDurationMultiplier = 1.5;
	/** 负载为张力/固定正常最大推力，不以瞬时出力作分母。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback", meta=(ClampMin="0", ClampMax="1"))
	double BlockedLoadThreshold = 0.2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback", meta=(ClampMin="0", ClampMax="1"))
	double BlockedProgressFraction = 0.4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback", meta=(ClampMin="0", Units="s"))
	double BlockedConfirmationSeconds = 0.35;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback", meta=(ClampMin="0", Units="s"))
	double LoadSmoothingSeconds = 0.15;

	bool IsValid() const;
};

/** 上一完整物理步的权威观察。速度均为 cm/s，方向为该步采用的主动游向。 */
struct CATFISHING_API FCatFishBehaviorFeedback
{
	double NormalizedLineLoad = 0.0;
	FVector ActualFishVelocityCentimetersPerSecond = FVector::ZeroVector;
	FVector ActiveSwimDirection = FVector::ForwardVector;
	double ExpectedFreeSpeedCentimetersPerSecond = 0.0;
	double FishStaminaRatio = 1.0;
	bool bLineTaut = false;
};

/** Runner 唯一拥有的执行记忆；行为计时只由固定步 AdvanceFeedback 推进。 */
struct CATFISHING_API FCatFishSteeringState
{
	FVector CurrentDirection = FVector::ForwardVector;
	FVector TargetDirection = FVector::ForwardVector;
	double RetargetSecondsRemaining = 0.0;
	ECatFishBehavior Behavior = ECatFishBehavior::None;
	double CurrentEffortRatio = 1.0;
	double TargetEffortRatio = 1.0;
	double BehaviorElapsedSeconds = 0.0;
	double BehaviorDurationSeconds = 0.0;
	double ActiveBoutElapsedSeconds = 0.0;
	double ActiveBoutDurationSeconds = 0.0;
	double FishStaminaRatio = 1.0;
	double BlockedSeconds = 0.0;
	double SmoothedLineLoad = 0.0;
	double LateralSign = 1.0;
	double DirectionOffsetDegrees = 0.0;
	bool bInitialized = false;
	FVector BoundaryWaterwardDirection = FVector::ZeroVector;
	double BoundaryAvoidanceSecondsRemaining = 0.0;
};

/** 无 World/Actor 的连续执行器；绝不替 StateTree 选择下一种行为。 */
class CATFISHING_API FCatFishSteeringModel
{
public:
	static bool Initialize(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		ECatFishBehavior Behavior, double FishStaminaRatio, FRandomStream& Random,
		FCatFishSteeringState& InOutState);
	/** 不重置当前方向、实际出力与岸线反馈；仅冻结此次命令目标和最长时长。 */
	static bool BeginBehavior(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		ECatFishBehavior Behavior, double FishStaminaRatio, FRandomStream& Random,
		FCatFishSteeringState& InOutState);
	static bool AdvanceFeedback(const FCatFishSteeringConfig& Config, const FCatFishBehaviorFeedback& Feedback,
		double DeltaSeconds, FCatFishSteeringState& InOutState);
	static bool TestCondition(const FCatFishSteeringConfig& Config, const FCatFishSteeringState& State,
		ECatFishBehaviorCondition Condition);
	/** 强拖覆盖方向/出力目标，不抽随机、不重置行为或推进其计时。 */
	static bool Step(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		double DeltaSeconds, FRandomStream& Random, FCatFishSteeringState& InOutState,
		FVector& OutDesiredDirection, bool bForceOutward = false);
	static bool RedirectFromWaterBoundary(const FCatFishSteeringConfig& Config,
		const FVector& WaterwardDirection, FRandomStream& Random, FCatFishSteeringState& InOutState);

private:
	static void UpdateTargetDirection(const FCatFishSteeringConfig& Config, const FVector& LineOutwardDirection,
		FCatFishSteeringState& InOutState);
};
