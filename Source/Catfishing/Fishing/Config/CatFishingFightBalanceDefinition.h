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
	UFUNCTION(BlueprintPure, Category = "验证", meta = (DisplayName = "搏斗平衡可正式运行"))
	bool IsRuntimeDefinitionReady() const;

	/** 日志和后续多套预设使用的稳定 ID。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "身份",
		meta = (DisplayName = "平衡方案 ID"))
	FName BalanceDefinitionId = NAME_None;

	/** 显式启用后才允许成为正式运行配置，避免半成品资产被误接入。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "身份",
		meta = (DisplayName = "启用正式运行"))
	bool bEnableRuntimeDefinition = false;

	/** 鱼使用实际重量生成力量；猫端质量读取真实刚体。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "每公斤力量", ClampMin = "0.001"))
	double StrengthPerKilogram = 0.0;

	/** DA_FishingFightBalance_Default 仍序列化的旧载荷，无运行读取；迁移并重存该资产后才能删除。 */
	UPROPERTY(BlueprintReadOnly, Category="已废弃（仅资产载荷）", meta=(DeprecationMessage="Use ForcePerStrengthNewtons"))
	double AccelerationPerStrength = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="已废弃（仅资产载荷）", meta=(DeprecationMessage="Force integration replaces drive response"))
	double DriveResponseSeconds = 0.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="力量与运动", meta=(DisplayName="每点力量推力（牛顿）", ClampMin="0.001"))
	double ForcePerStrengthNewtons = 1.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="力量与运动", meta=(DisplayName="力竭鱼回收辅助力（牛顿）", ClampMin="0.001"))
	double ExhaustedReelForceNewtons = 200.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="力量与运动", meta=(DisplayName="猫力竭拖行辅助加速度", ClampMin="0.0"))
	double ExhaustedCatTowAccelerationCentimetersPerSecondSquared = 300.0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="鱼线与张力", meta=(DisplayName="满表现张力（牛顿）", ClampMin="0.001"))
	double DisplayTensionNewtons = 50.0;
	/** 显式资产迁移版本；不会据此重置已经编辑的新参数。 */
	UPROPERTY(EditDefaultsOnly, Category="身份", AdvancedDisplay)
	int32 ForceModelVersion = 0;

	/** 左键收线意图的速度上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "收线速度", ClampMin = "0.001", Units = "cm/s"))
	double ReelSpeedCentimetersPerSecond = 0.0;

	/** 持竿主控力竭时，按鱼较快的配置游速持续外冲；拖拽保持锁线直到落水或获救。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "力量与运动",
		meta = (DisplayName = "猫力竭后鱼外冲速度倍率", ClampMin = "1.0"))
	double ExhaustedCatEscapeSpeedMultiplier = 2.0;

	/** 收线每标准力量·cm 实际正功单价；身体移动改用 CatPhysicalEffortSettings 的意图缺失米价。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫做功体力消耗系数", ClampMin = "0.0"))
	double CatStaminaCostPerStrengthCentimeter = -1.0;

	/** 转杆实际正功的独立单价；按标准转矩乘归一化转矩加权的实际转角，不借用线性距离单价。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫转杆每标准转矩弧度体力系数", ClampMin = "0.0"))
	double CatRodStaminaCostPerStrengthRadian = 0.03;

	/** 实际动作无负载时的基础成本倍率；微调较便宜，叠加负载部分仍可独立调节。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫无负载动作成本倍率", ClampMin = "0.0"))
	double CatUnloadedWorkMultiplier = 0.15;

	/** 满用力/满负载每秒支撑成本，按用力比例平方缩放；各操作共享的支撑只收一次。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫满用力每秒支撑耗体", ClampMin = "0.0"))
	double CatSupportStaminaPerSecond = 2.0;

	/** 沿鱼本步主动意图未完成的位移单价（体力点/m）；180 cm/s 满出力且完全受阻时独立标定为 3 点/s。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "鱼每米未完成意图耗体", ClampMin = "0.0"))
	double FishStaminaPerUnfulfilledMeter = 5.0 / 3.0;

	/** 仅保留旧资产和未完成审计的 Blueprint 字段身份；旧每秒价不读取、不换算为每米价。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧每秒鱼价格不再使用；请设置FishStaminaPerUnfulfilledMeter，单位为体力点/m。"))
	double FishEffortStaminaPerSecond = 3.0;

	/** 仅保留旧资产序列化兼容；旧力量乘厘米单价不换算为沿意图缺失位移单价。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧每厘米鱼价格不再使用；请设置FishStaminaPerUnfulfilledMeter。"))
	double FishStaminaCostPerStrengthCentimeter = -1.0;

	/** 主控身体意图缺失米价的无量纲倍率；辅助使用独立身体默认价，不计入转杆或收线。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫移动体力倍率", ClampMin = "0.0"))
	double CatMovementStaminaMultiplier = 1.0;

	/** 猫主动收线实际做功的体力倍率；受阻费用由同一主控的持竿支撑承担。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫收线体力倍率", ClampMin = "0.0"))
	double CatReelStaminaMultiplier = 1.0;

	/** 猫主动转杆形成的对抗努力体力倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫转杆体力倍率", ClampMin = "0.0"))
	double CatRodStaminaMultiplier = 1.0;

	/** 猫沿线持续支撑体力倍率；与转杆支撑取较高者，避免同一负担重复计费。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫持竿体力倍率", ClampMin = "0.0"))
	double CatHoldStaminaMultiplier = 1.0;

	/** 猫实际做功价格乘以 (无负载动作倍率 + 归一化负载 × 本参数)，不影响支撑计时。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "猫负载体力倍率", ClampMin = "0.0"))
	double CatLoadStaminaMultiplier = 1.0;

	/** 仅保留旧资产序列化兼容，运行不再叠加负载价格倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧鱼负载倍率不再使用；请设置FishStaminaPerUnfulfilledMeter。"))
	double FishLoadStaminaMultiplier = 1.0;

	/** 仅保留旧资产序列化兼容；当前沿意图缺失位移不使用旧等效受阻倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧受阻距离倍率不再使用；沿意图缺失位移按FishStaminaPerUnfulfilledMeter结算。"))
	double IsometricEffortMultiplier = -1.0;

	/** 正常右键且身体/杆完全卸载时主控每秒恢复的体力；受外部负载、满线或强制力竭拖拽时不恢复。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "放线体力恢复速度", ClampMin = "0.0"))
	double SlackStaminaRegenPerSecond = -1.0;

	/** 本步实际扣体后，剩余鱼体力不高于该绝对值时吸附为 0；零费用不触发。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DisplayName = "鱼力竭吸附阈值", ClampMin = "0.0", ClampMax = "1.0"))
	double FishExhaustionThreshold = -1.0;

	/** 仅保留旧资产序列化兼容；疲劳反馈由连续行为配置控制。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧平静计时不再使用；疲劳反馈由鱼行为配置控制。"))
	double LowStaminaRestThreshold = -1.0;

	/** 仅保留旧资产序列化兼容；疲劳反馈由连续行为配置控制。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "体力",
		meta = (DeprecatedProperty, DeprecationMessage = "旧平静计时不再使用；疲劳反馈由鱼行为配置控制。"))
	double LowStaminaRestMultiplier = 0.0;

	/** 废弃几何表现阈值，仅保留旧资产载荷，不参与新张力计算。 */
	UPROPERTY(BlueprintReadOnly, Category="已废弃（仅资产载荷）", meta=(DeprecationMessage="Use DisplayTensionNewtons"))
	double TensionResponseRangeCentimeters = 0.0;

	/** 无人持竿且鱼超出最大线长后的逃脱余量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "逃脱松线余量", ClampMin = "0.0", Units = "cm"))
	double EscapeSlackCentimeters = -1.0;

	/** 存在鱼向外负载时，按双方沿线力量与实际负载连续缩放的鱼竿磨损系数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "僵持鱼竿磨损系数", ClampMin = "0.0"))
	double StalemateRodWearPerFishStrength = -1.0;

	/** 竿身偏离鱼线方向时仍保留的最低有效杠杆。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "持竿最低杠杆倍率", ClampMin = "0.05", ClampMax = "1.0"))
	double HeldRodMinimumLeverageMultiplier = 0.0;

	/** 鱼端每秒允许承担的最大约束修正速度，只限制静态锚点模型的鱼端历史误差修正，物理身体不读该上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "鱼线与张力",
		meta = (DisplayName = "最大约束修正速度", ClampMin = "1.0", Units = "cm/s"))
	double MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 0.0;

	/** DA_FishingFightBalance_Default 仍序列化的旧速度截断载荷；没有运行读取，删除前须迁移该资产。 */
	UPROPERTY(BlueprintReadOnly, Category="已废弃（仅资产载荷）")
	double MinimumCarrierAwaySpeedMultiplier = -1.0;
};
