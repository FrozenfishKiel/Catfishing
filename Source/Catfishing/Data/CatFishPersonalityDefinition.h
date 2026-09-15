#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"
#include "CatFishPersonalityDefinition.generated.h"

/**
 * 咬钩节奏的测试期模板。
 * 2026-09-09 晚裁「四套性格模板是测试用，正式口径逐鱼配」；咬钩节奏那几列还没进鱼表格，
 * T10 已移除生产时间消费者，仅保留旧资产反射兼容；最终鱼资产迁移后再核查退役。
 */
UCLASS(BlueprintType)
class CATFISHING_API UCatBitePersonalityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	bool IsRuntimeDefinitionReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName BitePersonalityId = NAME_None;
	/** 墓碑（T10，钓鱼规则 §3.4）：只为旧 Bite 资产保留反射，生产改读鱼定义。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0", Units="s")) double ProbeDurationSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0")) double TrueBiteWindowSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0")) double PerfectHookWindowSeconds = 0.0;
	/**
	 * 以下三项完美削减倍率已退为过渡字段：正式口径按鱼册稀有度档取，
	 * 见 UCatFishCatalogSettings::ResolvePerfectHookReduction（2026-09-09 晚裁「四套性格模板是测试用」）。
	 * 2026-09-13：生产会话与 Debug 均已改读目录；这三项不参与就绪校验，也不产生玩法输出。
	 * 旧 Bite 资产仍序列化它们，二进制 Blueprint/外部消费者尚未经编辑器迁移核实；本轮只保留反射兼容。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0", ClampMax="1")) double PerfectFishStrengthMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0", ClampMax="1")) double PerfectFishStaminaMultiplier = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(DeprecatedProperty, ClampMin="0", ClampMax="1")) double PerfectInitialLineLengthMultiplier = 0.0;
};

/**
 * 搏斗节奏的测试期模板。
 * 2026-09-09 晚裁「四套性格模板是测试用，正式口径逐鱼配（鱼表格食性、发力段长、休息段长、游速系数四列）」，
 * 台账 D-16「不恢复旧模型」同批被推翻。四套 Fight_* 资产不删，但只作为鱼表某列未填时的兜底。
 * 取值必须走 UCatFishingSettings::TryResolveFishBehavior，不要直接读下面的 AdaptiveSteeringConfig
 * 与 FullEffortMovementSpeedCentimetersPerSecond ——直接读会绕过鱼表，让那四列白填。
 */
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
	/** 满力游速基数（厘米/秒）；本鱼实际满力游速 ＝ 本值 × 鱼表「游速系数」列，见 TryResolveFishBehavior。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Adaptive Motion", meta=(ClampMin="1", Units="cm/s"))
	double FullEffortMovementSpeedCentimetersPerSecond = 0.0;
	/** 转向/出力/段长的模板值；其中发力段长与休息段长两项会被鱼表同名列覆盖，其余仍以本模板为准。 */
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
