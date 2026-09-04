#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatFishPersonalityDefinition.generated.h"

UCLASS(BlueprintType)
class CATFISHING_API UCatBitePersonalityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName BitePersonalityId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double ProbeDurationSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double TrueBiteWindowSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double PerfectHookWindowSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectFishStrengthMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectFishStaminaMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) double PerfectInitialLineLengthMultiplier = 0.0;
};

UCLASS(BlueprintType)
class CATFISHING_API UCatFightPersonalityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName FightPersonalityId = NAME_None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double CalmMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) double StruggleMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.01")) double BaseDrainMultiplier = 1.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) double StruggleDrainMultiplier = 2.0;

	/** 每隔多长时间重新选择一次目标游向；不是每帧随机，避免白噪声抖动。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering")
	FVector2D DirectionRetargetDurationRangeSeconds = FVector2D(0.6, 1.4);

	/** 实际游向追向目标游向的最大角速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering", meta=(ClampMin="1"))
	double MaximumTurnRateDegreesPerSecond = 120.0;

	/** 挣扎时目标方向偏向“沿鱼线向外”的程度；1=几乎正面硬冲，0=更随机。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering", meta=(ClampMin="0", ClampMax="1"))
	double StruggleOutwardDirectionBias = 0.75;

	/** 平静时目标方向偏向“朝竿尖向内”的程度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering", meta=(ClampMin="0", ClampMax="1"))
	double CalmInwardDirectionBias = 0.65;

	/** 叠加横向绕游的强度；越高越容易绕着竿尖切线游动。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering", meta=(ClampMin="0", ClampMax="1"))
	double LateralMovementBias = 0.45;

	/** 挣扎阶段反向选择一次目标（先朝内/横向再转出）的概率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering", meta=(ClampMin="0", ClampMax="1"))
	double FeintProbability = 0.1;

	/**
	 * 满体力时，每次平静阶段重新选方向落入“朝竿尖向内扇区”的概率。
	 * 高体力鱼应以远离岸边为主，因此默认值较低。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Inward Probability",
		meta=(ClampMin="0", ClampMax="1"))
	double FullStaminaInwardProbability = 0.1;

	/**
	 * 体力归零附近的向内概率；运行时会按体力比例在满体力值与本值之间插值。
	 * 它必须不小于满体力概率，保证鱼越疲劳越容易给玩家创造靠岸窗口。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Inward Probability",
		meta=(ClampMin="0", ClampMax="1"))
	double ExhaustedInwardProbability = 0.8;

	/** 体力到向内概率的曲线指数；1=线性，>1 表示到低体力后概率才明显上升。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Inward Probability",
		meta=(ClampMin="0.1", ClampMax="4"))
	double InwardProbabilityExponent = 1.35;

	/**
	 * 朝鱼竿方向左右各多少度仍算“向内”。默认 60°，即完整向内扇区为 120°。
	 * Steering 选中向内后会把目标方向严格限制在这个扇区内，而不只做一个模糊 Bias。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Inward Probability",
		meta=(ClampMin="1", ClampMax="89", Units="deg"))
	double InwardConeHalfAngleDegrees = 60.0;

	/**
	 * 鱼游向在鱼线向外方向上的投影达到此比例，才记为强对抗。
	 * 强对抗只用于自然僵持的表现分类和诊断，不裁决终局；体力、做功与磨损仍按连续夹角投影计算。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Fight", meta=(ClampMin="0.01", ClampMax="1"))
	double StrongConfrontationAlignmentThreshold = 0.55;

	/** 强对抗角度至少持续多久才确认僵持，防止方向过阈值一帧就抖动状态。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Fight", meta=(ClampMin="0", ClampMax="2", Units="s"))
	double StrongConfrontationConfirmationSeconds = 0.2;

	/** 对 max(cos(夹角),0) 做幂变换；1=线性，>1 让斜向游动的有效力量衰减更快。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Steering|Fight", meta=(ClampMin="0.1", ClampMax="4"))
	double AngleStrengthExponent = 1.0;
};
