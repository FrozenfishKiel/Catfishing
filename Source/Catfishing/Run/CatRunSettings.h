#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatRunSettings.generated.h"

class UStateTree;

/** Run 每日供品目标缩放策略；Undecided 阻止 Run 启动，固定日程只表达当前已支持的明确规则，不推导未裁人数曲线。 */
UENUM()
enum class ECatRunScalingPolicy : uint8
{
	/** 单人/多人目标缩放尚未裁决，不能把固定测试目标冒充正式规则。 */
	Undecided,
	/** 明确使用配置中的每日供品目标日程；当前实现不支持人数曲线或静默缩放。 */
	FixedDailyOfferingTarget
};

/** 供品鱼的重量积分档位；它只由本条鱼的实际重量决定，不读取鱼表中的静态贡献。 */
UENUM(BlueprintType)
enum class ECatOfferingWeightClass : uint8
{
	/** 小型供品；实际重量小于 1kg，结算时价值 1 点。 */
	Small,
	/** 中型供品；实际重量大于等于 1kg 且小于 5kg，结算时价值 2 点。 */
	Medium,
	/** 大型供品；实际重量大于等于 5kg 且小于 15kg，结算时价值 4 点。 */
	Large,
	/** 巨型供品；实际重量大于等于 15kg，结算时价值 10 点。 */
	Giant
};

/** Run 专属二值策略 gate；与 Online 策略分域，避免 Run Core 反向依赖会话实现。 */
UENUM()
enum class ECatRunPolicyDecision : uint8
{
	/** 产品尚未裁决，依赖路径返回 PolicyUndecided。 */
	Undecided,
	/** 策略明确禁止。 */
	Disabled,
	/** 策略明确允许。 */
	Enabled
};

/** 单日供品结算调参；早晨生成目标、夜晚结算世界进度时都读取同一条日程，避免目标和奖惩分散成两份表。 */
USTRUCT(BlueprintType)
struct FCatRunDailyOfferingTuning
{
	GENERATED_BODY()

	/** 本日圣猫要求的供品点数；StartDay GE 读取后写入 DailyOfferingTarget，UI 和夜晚结算都看这个属性投影。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "1"))
	int32 DailyOfferingTarget = 0;

	/** 本日达标时可增加的世界进度基础值；夜晚结算 ExecCalc 读取后再乘增益倍率并受臭鱼惩罚。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0"))
	int32 WorldProgressGain = 0;

	/** 本日未达标时扣除的世界进度基础值；夜晚结算 ExecCalc 读取后再乘损失倍率，最终夹在 0 到 100。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0"))
	int32 WorldProgressLoss = 0;
};

/** RunFlow、数值与准入的正式配置边界；所有产品项默认关闭或 Unset，只有完整显式配置才启动权威一局。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Run"))
class CATFISHING_API UCatRunSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决 GameMode 能否启动唯一 Run/StateTree 权威链；未启用或供品目标策略未选时返回 false，任何构建都保持 NotStarted。 */
	bool IsRuntimeReady() const;

	/** 读取指定天的白天秒数与供品结算日程；任一值非有限/非正或运行链未启用时清空输出并返回 false。 */
	bool TryGetDayParameters(int32 DayIndex, float& OutDayLengthSeconds, FCatRunDailyOfferingTuning& OutTuning) const;

	/** 读取开局世界进度；配置必须落在 1 到 100，失败时 GameMode 不启动正式 RunFlow。 */
	bool TryGetInitialWorldProgress(int32& OutWorldProgress) const;

	/** 按实际重量解析供品档位与点数；供品提交入口只提供鱼事实，不能读取鱼表外的第二套贡献字段。 */
	bool TryClassifyOfferingWeight(double WeightKilograms, ECatOfferingWeightClass& OutWeightClass, int32& OutOfferingPoints) const;

	/** 臭鱼供品 ID 是配置层对鱼定义的污染标记；夜晚结算只用它计算增益折扣，不改变鱼本身或捕获记录。 */
	bool IsStinkyOfferingFish(FName FishDefinitionId) const;

	/** 裁决 StateTree 是否可以选择成功结算分支；只有产品显式 Enabled 且世界进度达到 100，才返回 true。 */
	bool CanEnterSuccessSettlementNight(int32 WorldProgress) const;

	/** 只读返回成功结算策略是否被允许；它只表达总开关，不包含第几天可结束的判断。 */
	bool IsSuccessSettlementEnabled() const;

	/** RunFlow 总 gate；默认关闭，关闭时 GameMode 只发布 NotStarted/StartupFailed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableRunRuntime = false;

	/** ST_RunFlow 的正式软引用；默认空，缺失时绝不回退到 C++ 状态机。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UStateTree> RunFlowStateTree;

	/** 白天时长，单位秒；0 表示 Unset，夜晚永远不读取该值。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	float DayLengthSeconds = 0.0f;

	/** 开局世界进度；GameMode 启动时投影到 Run ASC 的 WorldProgress，0 或越界都阻止 RunFlow。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "1", ClampMax = "100"))
	int32 InitialWorldProgress = 10;

	/** 每日供品目标和世界进度奖惩日程；超过表长的天数复用最后一项，避免未裁天数读取空值。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	TArray<FCatRunDailyOfferingTuning> DailyOfferingSchedule;

	/** 小鱼档的最大重量边界，单位千克；小于该值的鱼在夜晚供品结算中计 1 点。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning|Offering", meta = (ClampMin = "0.0", Units = "kg"))
	double SmallOfferingMaxWeightKilograms = 1.0;

	/** 中鱼档的最大重量边界，单位千克；大于等于小鱼边界且小于该值的鱼在夜晚供品结算中计 2 点。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning|Offering", meta = (ClampMin = "0.0", Units = "kg"))
	double MediumOfferingMaxWeightKilograms = 5.0;

	/** 大鱼档的最大重量边界，单位千克；大于等于中鱼边界且小于该值的鱼在夜晚供品结算中计 4 点。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning|Offering", meta = (ClampMin = "0.0", Units = "kg"))
	double LargeOfferingMaxWeightKilograms = 15.0;

	/** 会污染供品增益的鱼定义 ID；每条命中鱼让达标世界进度增益减少 25%，最多减到 0 且不会倒扣。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning|Offering")
	TArray<FName> StinkyOfferingFishDefinitionIds;

	/** 人数缩放策略；默认 Undecided，必须显式选择 FixedDailyOfferingTarget 才能使用配置日程。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	ECatRunScalingPolicy PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;

	/** 成功终局是否可进入 SuccessSettlementNight；它只作为总开关，具体结果由世界进度属性裁决。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ending")
	ECatRunPolicyDecision SuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;
};
