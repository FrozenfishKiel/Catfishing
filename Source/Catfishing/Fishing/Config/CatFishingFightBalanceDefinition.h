#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatFishingFightBalanceDefinition.generated.h"

/**
 * 钓鱼搏斗的全局策划平衡资产。
 *
 * 技术开关和资产入口仍由 UCatFishingSettings 管理；力量、运动、体力和鱼线裁决只从本资产读取，
 * 避免 DefaultGame.ini 与 DataAsset 同时保存两套可生效数值。
 */
UCLASS(BlueprintType, meta = (DisplayName = "钓鱼搏斗平衡"))
class CATFISHING_API UCatFishingFightBalanceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 只有 ID、启用位和全部数值均合法时，资产才可进入服务器权威搏斗。 */
	bool IsRuntimeDefinitionReady() const;

	/** 日志和后续多套预设使用的稳定 ID。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "身份",
		meta = (DisplayName = "平衡方案 ID"))
	FName BalanceDefinitionId = NAME_None;

	/** 显式启用后才允许成为正式运行配置，避免半成品资产被误接入。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "身份",
		meta = (DisplayName = "启用正式运行"))
	bool bEnableRuntimeDefinition = false;

	/** 鱼使用实际重量生成力量；猫使用基础力量反推玩法等效质量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "每公斤力量", ClampMin = "0.001"))
	double StrengthPerKilogram = 0.0;

	/** 猫和鱼共享的“力量→对抗加速度”换算，单位 cm/s² / Strength；鱼的自由游速由性格资产决定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "每点力量加速度", ClampMin = "0.001"))
	double AccelerationPerStrength = 0.0;

	/** 将猫端对抗加速度投影为收线/牵引响应速度的时长；不限制鱼在松线阶段的自由游速。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "猫端驱动力响应时间", ClampMin = "0.001", Units = "s"))
	double DriveResponseSeconds = 0.0;

	/** 左键收线意图的速度上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "收线速度", ClampMin = "0.001", Units = "cm/s"))
	double ReelSpeedCentimetersPerSecond = 0.0;

	/** 猫每 1 点力量、每 1 cm 沿线有效努力消耗的体力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫做功体力消耗系数", ClampMin = "0.0"))
	double CatStaminaCostPerStrengthCentimeter = -1.0;

	/** 鱼每 1 点力量、每 1 cm 沿线有效努力消耗的体力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "鱼做功体力消耗系数", ClampMin = "0.0"))
	double FishStaminaCostPerStrengthCentimeter = -1.0;

	/** 未形成实际位移的意图距离折算系数；大于 0 时僵持仍消耗体力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "僵持努力折算倍率", ClampMin = "0.0"))
	double IsometricEffortMultiplier = -1.0;

	/** 右键完全放线时猫每秒恢复的搏斗体力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "放线体力恢复速度", ClampMin = "0.0"))
	double SlackStaminaRegenPerSecond = -1.0;

	/** 剩余鱼体力不高于该绝对值时吸附为 0。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "鱼力竭吸附阈值", ClampMin = "0.0", ClampMax = "1.0"))
	double FishExhaustionThreshold = -1.0;

	/** 鱼体力比例低于该值后延长平静期。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "低体力休息触发比例", ClampMin = "0.0", ClampMax = "1.0"))
	double LowStaminaRestThreshold = -1.0;

	/** 低体力状态下平静期时长倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "低体力休息时长倍率", ClampMin = "1.0"))
	double LowStaminaRestMultiplier = 0.0;

	/** 超出线长多少厘米视为满表现张力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "满张力响应距离", ClampMin = "0.1", Units = "cm"))
	double TensionResponseRangeCentimeters = 0.0;

	/** 无人持竿且鱼超出最大线长后的逃脱余量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "逃脱松线余量", ClampMin = "0.0", Units = "cm"))
	double EscapeSlackCentimeters = -1.0;

	/** 强对抗僵持时按鱼线力量结算的每步磨损系数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "僵持鱼竿磨损系数", ClampMin = "0.0"))
	double StalemateRodWearPerFishStrength = -1.0;

	/** 竿身偏离鱼线方向时仍保留的最低有效杠杆。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "持竿最低杠杆倍率", ClampMin = "0.05", ClampMax = "1.0"))
	double HeldRodMinimumLeverageMultiplier = 0.0;

	/** 鱼端每秒允许承担的最大约束修正速度，同时限制猫端目标牵引速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "最大约束修正速度", ClampMin = "1.0", Units = "cm/s"))
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 0.0;

	/** 满负载且鱼占优时，玩家沿远离鱼方向保留的最小速度比例。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "背离鱼方向最低速度倍率", ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumCarrierAwaySpeedMultiplier = -1.0;
};
