#include "Run/CatRunSettings.h"

// Runtime gate 计算流程：先要求显式总开关，再按选中的额度档各自验数值；固定档只要选了就算配齐，曲线档必须已经登记出一
// 张合法裁定表，Undecided 与未来新增取值一律不启动 RunFlow。
bool UCatRunSettings::IsRuntimeReady() const
{
	if (!bEnableRunRuntime)
	{
		return false;
	}
	switch (PlayerScalingPolicy)
	{
	case ECatRunScalingPolicy::FixedQuotaTarget:
		return true;
	case ECatRunScalingPolicy::DailyCurveByMorningPlayerCount:
		return HasValidDailyQuotaCurve();
	default:
		return false;
	}
}

// 白天参数读取流程：先把输出恢复为 Unset；随后复用 runtime gate，再明确排除曲线档，因为曲线档没有"一个固定额度"可以交
// 给调用者，最后只接受有限正秒数和正额度，失败不留下部分可用参数。
bool UCatRunSettings::TryGetDayParameters(float& OutDayLengthSeconds, int32& OutQuotaTarget) const
{
	OutDayLengthSeconds = 0.0f;
	OutQuotaTarget = 0;
	if (!IsRuntimeReady() || PlayerScalingPolicy != ECatRunScalingPolicy::FixedQuotaTarget
		|| !FMath::IsFinite(DayLengthSeconds) || DayLengthSeconds <= 0.0f || QuotaTarget <= 0)
	{
		return false;
	}
	OutDayLengthSeconds = DayLengthSeconds;
	OutQuotaTarget = QuotaTarget;
	return true;
}

// 当日额度读取流程：先清输出并复用 runtime gate，再拒绝非正的天序号与人数；固定档原样返回单一配置值（这一档本来就不随
// 天数和人数变化），曲线档只做精确匹配查表。
// 曲线档不做插值、外推或就近取值：飞书只裁定了"逐日上升、按人数缩放"这条规则，没有给出任何数值，缺条目时必须让调用方拿不到额度而不是拿到一个推算值。
bool UCatRunSettings::TryGetDailyQuotaTarget(const int32 DayIndex, const int32 MorningPlayerCount, int32& OutQuotaTarget) const
{
	OutQuotaTarget = 0;
	if (!IsRuntimeReady() || DayIndex <= 0 || MorningPlayerCount <= 0)
	{
		return false;
	}
	if (PlayerScalingPolicy == ECatRunScalingPolicy::FixedQuotaTarget)
	{
		if (QuotaTarget <= 0)
		{
			return false;
		}
		OutQuotaTarget = QuotaTarget;
		return true;
	}
	for (const FCatRunDailyQuotaEntry& Entry : DailyQuotaCurve)
	{
		if (Entry.DayIndex == DayIndex && Entry.PlayerCount == MorningPlayerCount)
		{
			// 条目的 QuotaTarget 已经由 HasValidDailyQuotaCurve 在 runtime gate 里验过为正，这里不再二次判定。
			OutQuotaTarget = Entry.QuotaTarget;
			return true;
		}
	}
	return false;
}

// 曲线校验流程：空表直接判未裁；随后逐条要求天序号、人数和额度都为正，并用打包成 64 位的 (天,人数) 键检测重复裁定。
// 重复条目会让查表静默取到先出现的那条，因此这里选择整表 fail-closed，而不是让运行期在两份冲突裁定里挑一个。
bool UCatRunSettings::HasValidDailyQuotaCurve() const
{
	if (DailyQuotaCurve.IsEmpty())
	{
		return false;
	}
	TSet<uint64> SeenDayPlayerKeys;
	SeenDayPlayerKeys.Reserve(DailyQuotaCurve.Num());
	for (const FCatRunDailyQuotaEntry& Entry : DailyQuotaCurve)
	{
		if (Entry.DayIndex <= 0 || Entry.PlayerCount <= 0 || Entry.QuotaTarget <= 0)
		{
			return false;
		}
		const uint64 DayPlayerKey = (static_cast<uint64>(static_cast<uint32>(Entry.DayIndex)) << 32)
			| static_cast<uint64>(static_cast<uint32>(Entry.PlayerCount));
		bool bAlreadySeen = false;
		SeenDayPlayerKeys.Add(DayPlayerKey, &bAlreadySeen);
		if (bAlreadySeen)
		{
			return false;
		}
	}
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
// 成功终局天数读取流程：先清输出，再要求成功结算策略显式开启且最终天序号为正；失败不保留旧输出，避免调用方用残留值放行终局。
bool UCatRunSettings::TryGetSuccessSettlementFinalDay(int32& OutFinalDayIndex) const
{
	OutFinalDayIndex = 0;
	if (!IsSuccessSettlementEnabled() || FinalDayIndex <= 0)
	{
		return false;
	}
	OutFinalDayIndex = FinalDayIndex;
	return true;
}

// 成功结算准入流程：读取正式最终天序号，再比较当前公开 DayIndex；未裁策略、非法天数或尚未抵达最终天都会拒绝 StateTree 的成功结算尝试。
bool UCatRunSettings::CanEnterSuccessSettlementNight(const int32 CurrentDayIndex) const
{
	int32 RequiredFinalDayIndex = 0;
	return TryGetSuccessSettlementFinalDay(RequiredFinalDayIndex)
		&& CurrentDayIndex == RequiredFinalDayIndex;
}
