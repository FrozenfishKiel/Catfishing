#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatRunSettings.generated.h"

class UStateTree;

/** Run 额度人数缩放策略；Undecided 阻止 Run 启动，固定目标只表达当前已支持的明确规则，不推导未裁人数曲线。 */
UENUM()
enum class ECatRunScalingPolicy : uint8
{
	/** 单人/多人额度缩放尚未裁决，不能把固定测试目标冒充正式规则。 */
	Undecided,
	/** 明确使用配置中的固定额度目标；当前实现不支持人数曲线或静默缩放。 */
	FixedQuotaTarget
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
	/** 裁决 GameMode 能否启动唯一 Run/StateTree 权威链；未启用或额度策略未选时返回 false，任何构建都保持 NotStarted。 */
	bool IsRuntimeReady() const;

	/** 读取白天秒数与固定额度；任一值非有限/非正或运行链未启用时清空输出并返回 false。 */
	bool TryGetDayParameters(float& OutDayLengthSeconds, int32& OutQuotaTarget) const;

	/** 判断普通夜晚的新加入/重连确认是否可进入资格集合；两项策略必须都显式 Enabled 才允许。 */
	bool CanAdmitLateNightReady() const;

	/** 裁决 StateTree 是否可以选择成功结算分支；只有产品显式 Enabled 才返回 true，默认值不能推导终局天数。 */
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

	/** 固定当日额度；0 表示 Unset，不推导逐日曲线或人数缩放。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	int32 QuotaTarget = 0;

	/** 人数缩放策略；默认 Undecided，必须显式选择 FixedQuotaTarget 才能使用配置额度。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning")
	ECatRunScalingPolicy PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;

	/** 普通夜晚新加入者是否可参与本夜 ready；默认未裁，资格外确认返回 PolicyUndecided。 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission")
	ECatRunPolicyDecision NightJoinReadyPolicy = ECatRunPolicyDecision::Undecided;

	/** 普通夜晚重连者是否可参与本夜 ready；默认未裁时与新加入共同 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission")
	ECatRunPolicyDecision NightReconnectReadyPolicy = ECatRunPolicyDecision::Undecided;

	/** 成功终局是否可进入 SuccessSettlementNight；具体天数与达成条件不在本配置表达，未有正式事实时保持不可达。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ending")
	ECatRunPolicyDecision SuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;
};
