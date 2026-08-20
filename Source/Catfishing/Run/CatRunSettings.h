#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatRunSettings.generated.h"

class UStateTree;

/** Run 每日额度的裁定方式；Undecided 阻止 Run 启动，其余取值各自对应一套必须被完整配置的额度来源。 */
UENUM()
enum class ECatRunScalingPolicy : uint8
{
	/** 额度规则尚未裁决，不能把固定测试目标冒充正式规则。 */
	Undecided,
	/** 明确使用配置中的单一固定额度目标；它不随天数或在场人数变化，是曲线数值到位前的可运行档。 */
	FixedQuotaTarget,
	/** 额度按局内天序号逐日上升，并在每天清晨按当时在场人数裁定；具体数值全部来自显式登记的曲线表，代码不推导首日值、斜率或人数公式。 */
	DailyCurveByMorningPlayerCount
};

/**
 * 每日额度曲线上的一条显式裁定：某个局内天序号在某个清晨在场人数下要求的额度条数。
 * 它刻意不表达任何公式，因为飞书只裁定了"逐日上升、按人数缩放"这条规则，没有给出首日值、斜率或缩放系数。
 */
USTRUCT()
struct FCatRunDailyQuotaEntry
{
	GENERATED_BODY()

	/** 本条裁定适用的局内天序号，从第 1 天开始；0 或负数表示这条没填完，会让整条曲线 fail-closed。 */
	UPROPERTY(EditAnywhere, Category = "Tuning", meta = (ClampMin = "1"))
	int32 DayIndex = 0;

	/** 本条裁定适用的清晨在场人数；额度当天冻结，所以这里指的是当天清晨确定人数的那一刻，不是白天中途的人数。 */
	UPROPERTY(EditAnywhere, Category = "Tuning", meta = (ClampMin = "1"))
	int32 PlayerCount = 0;

	/** 该天该人数下要求交付的鱼条数；0 或负数表示未填完，会让整条曲线 fail-closed。 */
	UPROPERTY(EditAnywhere, Category = "Tuning", meta = (ClampMin = "1"))
	int32 QuotaTarget = 0;
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

/** RunFlow、数值与准入的正式配置边界；所有产品项默认关闭或 Unset，只有完整显式配置才启动权威一局。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Run"))
class CATFISHING_API UCatRunSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决 GameMode 能否启动唯一 Run/StateTree 权威链；未启用、额度策略未选或选中的策略缺数值时返回 false，任何构建都保持 NotStarted。 */
	bool IsRuntimeReady() const;

	/** 读取白天秒数与固定额度；仅服务 FixedQuotaTarget 档，选用逐日曲线或任一值非法时清空输出并返回 false。 */
	bool TryGetDayParameters(float& OutDayLengthSeconds, int32& OutQuotaTarget) const;

	/** 按当前额度策略读取指定天序号与清晨在场人数下的额度；固定档忽略两个入参返回固定值，曲线档只接受精确登记过的条目，读取失败清空输出。 */
	bool TryGetDailyQuotaTarget(int32 DayIndex, int32 MorningPlayerCount, int32& OutQuotaTarget) const;

	/** 判断普通夜晚的新加入/重连确认是否可进入资格集合；两项策略必须都显式 Enabled 才允许。 */
	bool CanAdmitLateNightReady() const;

	/** 裁决 StateTree 是否可以选择成功结算分支；只有产品显式 Enabled 才返回 true，默认值不能推导终局天数。 */
	bool IsSuccessSettlementEnabled() const;
	/** 读取成功结算允许的最终天序号；成功策略未裁或天数非法时清空输出并返回 false。 */
	bool TryGetSuccessSettlementFinalDay(int32& OutFinalDayIndex) const;

	/** 判断当前 Run 天序号是否已经满足成功结算夜准入；StateTree 误连边时由 GameMode 用它二次兜底。 */
	bool CanEnterSuccessSettlementNight(int32 CurrentDayIndex) const;

	/** RunFlow 总 gate；默认关闭，关闭时 GameMode 只发布 NotStarted/StartupFailed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableRunRuntime = false;

	/** ST_RunFlow 的正式软引用；默认空，缺失时绝不回退到 C++ 状态机。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UStateTree> RunFlowStateTree;

	/** 白天时长，单位秒；0 表示 Unset，夜晚永远不读取该值。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	float DayLengthSeconds = 0.0f;

	/** 固定当日额度；0 表示 Unset，只在 FixedQuotaTarget 档被读取，不推导逐日曲线或人数缩放。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	int32 QuotaTarget = 0;

	/** 每日额度的裁定方式；默认 Undecided，必须显式选择一档并配齐该档数值才能启动 Run。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	ECatRunScalingPolicy PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;

	/** 逐日曲线档的全部额度裁定；默认空表示曲线数值一条都没裁，选中该档时空表或存在非法/重复条目都会让整个 Run 拒绝启动。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	TArray<FCatRunDailyQuotaEntry> DailyQuotaCurve;

	/** 普通夜晚新加入者是否可参与本夜 ready；默认未裁，资格外确认返回 PolicyUndecided。 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission")
	ECatRunPolicyDecision NightJoinReadyPolicy = ECatRunPolicyDecision::Undecided;

	/** 普通夜晚重连者是否可参与本夜 ready；默认未裁时与新加入共同 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission")
	ECatRunPolicyDecision NightReconnectReadyPolicy = ECatRunPolicyDecision::Undecided;

	/** 成功终局是否可进入 SuccessSettlementNight；默认未裁，必须配合最终天数 gate 才能被 GameMode 接受。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ending")
	ECatRunPolicyDecision SuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;

	/** 成功终局要求达到的局内天序号；当前 GDD/开发计划锚定第 10 天，非法值会让成功结算 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ending", meta = (ClampMin = "1"))
	int32 FinalDayIndex = 10;

private:
	/** 判断逐日曲线是否已经配成一张可用的裁定表；空表、条目字段非法或同一天同人数被裁两次都返回 false。 */
	bool HasValidDailyQuotaCurve() const;
};
