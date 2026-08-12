#include "Run/CatRunSettings.h"

// Runtime gate 计算流程：所有构建都要求显式总开关与当前唯一支持的 FixedQuotaTarget 策略；任何未裁人数规则都不能启动 RunFlow。
bool UCatRunSettings::IsRuntimeReady() const
{
	return bEnableRunRuntime && PlayerScalingPolicy == ECatRunScalingPolicy::FixedQuotaTarget;
}

// 白天参数读取流程：先把输出恢复为 Unset；随后复用 runtime gate，并只接受有限正秒数和正额度，失败不留下部分可用参数。
bool UCatRunSettings::TryGetDayParameters(float& OutDayLengthSeconds, int32& OutQuotaTarget) const
{
	OutDayLengthSeconds = 0.0f;
	OutQuotaTarget = 0;
	if (!IsRuntimeReady() || !FMath::IsFinite(DayLengthSeconds)
		|| DayLengthSeconds <= 0.0f || QuotaTarget <= 0)
	{
		return false;
	}
	OutDayLengthSeconds = DayLengthSeconds;
	OutQuotaTarget = QuotaTarget;
	return true;
}

// 夜间资格 gate 计算流程：新加入与重连需要分别裁决，因此两项策略都显式 Enabled 前统一拒绝资格外 ready，避免误把其中一种当成另一种。
bool UCatRunSettings::CanAdmitLateNightReady() const
{
	return NightJoinReadyPolicy == ECatRunPolicyDecision::Enabled
		&& NightReconnectReadyPolicy == ECatRunPolicyDecision::Enabled;
}

// 成功终局 gate 读取流程：只有显式 Enabled 才允许 StateTree 的 EnterPhase Task 发布成功结算夜；Undecided 与 Disabled 都保持不可达。
bool UCatRunSettings::IsSuccessSettlementEnabled() const
{
	return SuccessSettlementPolicy == ECatRunPolicyDecision::Enabled;
}
