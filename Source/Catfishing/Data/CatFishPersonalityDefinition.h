#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"
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
	virtual void PostLoad() override;
	/** 旧资产只在载入迁移时读取旧值；新会话只消费以下新字段。 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Adaptive Motion")
	bool MigrateLegacyMotionSettings();
	UFUNCTION(BlueprintPure, Category="Adaptive Motion")
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName FightPersonalityId = NAME_None;
	/** 0 仅为旧序列化格式；完成迁移后为 1，后续非法零游速不得回退旧参数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Adaptive Motion")
	int32 AdaptiveMotionVersion = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Adaptive Motion", meta=(ClampMin="1", Units="cm/s"))
	double FullEffortMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Adaptive Motion")
	FCatFishSteeringConfig AdaptiveSteeringConfig;
	/** 仅保留已序列化资产的迁移载荷；正式蓝图引用未全部迁移前不移除反射身份。 */
	UPROPERTY(meta=(DeprecatedProperty)) FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(meta=(DeprecatedProperty)) FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	UPROPERTY(meta=(DeprecatedProperty)) double CalmMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(meta=(DeprecatedProperty)) double StruggleMovementSpeedCentimetersPerSecond = 0.0;
	UPROPERTY(meta=(DeprecatedProperty)) double BaseDrainMultiplier = 1.0;
	UPROPERTY(meta=(DeprecatedProperty)) double StruggleDrainMultiplier = 2.0;
	UPROPERTY(meta=(DeprecatedProperty)) FVector2D DirectionRetargetDurationRangeSeconds = FVector2D(0.6, 1.4);
	UPROPERTY(meta=(DeprecatedProperty)) double MaximumTurnRateDegreesPerSecond = 120.0;
	UPROPERTY(meta=(DeprecatedProperty)) double StruggleOutwardDirectionBias = 0.75;
	UPROPERTY(meta=(DeprecatedProperty)) double CalmInwardDirectionBias = 0.65;
	UPROPERTY(meta=(DeprecatedProperty)) double LateralMovementBias = 0.45;
	UPROPERTY(meta=(DeprecatedProperty)) double FeintProbability = 0.1;
	UPROPERTY(meta=(DeprecatedProperty)) double FullStaminaInwardProbability = 0.1;
	UPROPERTY(meta=(DeprecatedProperty)) double ExhaustedInwardProbability = 0.8;
	UPROPERTY(meta=(DeprecatedProperty)) double InwardProbabilityExponent = 1.35;
	UPROPERTY(meta=(DeprecatedProperty)) double InwardConeHalfAngleDegrees = 60.0;
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
