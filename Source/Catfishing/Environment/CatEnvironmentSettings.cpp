#include "Environment/CatEnvironmentSettings.h"

// 可用性流程：要求显式启用、已裁天气和严格递增的晨/暮分界；无效比例不会被夹取成隐藏产品默认。
bool UCatEnvironmentSettings::IsRuntimeReady() const
{
	return bEnableEnvironmentRuntime && ConfiguredWeather != ECatEnvironmentWeather::Unknown
		&& FMath::IsFinite(MorningEndFraction) && FMath::IsFinite(DuskStartFraction)
		&& MorningEndFraction > 0.0 && DuskStartFraction > MorningEndFraction && DuskStartFraction < 1.0;
}

// 时段计算流程：只接受 DayActive 和有效服务器白天区间；把当前世界时间规范化到 0..1 后按显式分界返回 Morning/Day/Dusk，绝不读取现实时间。
ECatEnvironmentTimeOfDay UCatEnvironmentSettings::ResolveTimeOfDay(const FCatRunPhaseSnapshot& RunSnapshot,
	const double ServerNowSeconds) const
{
	if (!IsRuntimeReady() || RunSnapshot.Phase != ECatRunPhase::DayActive || !RunSnapshot.bHasDeadline
		|| !FMath::IsFinite(ServerNowSeconds) || !FMath::IsFinite(RunSnapshot.ServerTimeAnchorSeconds)
		|| !FMath::IsFinite(RunSnapshot.DeadlineServerTimeSeconds)
		|| RunSnapshot.DeadlineServerTimeSeconds <= RunSnapshot.ServerTimeAnchorSeconds)
	{
		return ECatEnvironmentTimeOfDay::Unknown;
	}
	const double Progress = FMath::Clamp((ServerNowSeconds - RunSnapshot.ServerTimeAnchorSeconds)
		/ (RunSnapshot.DeadlineServerTimeSeconds - RunSnapshot.ServerTimeAnchorSeconds), 0.0, 1.0);
	if (Progress < MorningEndFraction)
	{
		return ECatEnvironmentTimeOfDay::Morning;
	}
	return Progress < DuskStartFraction ? ECatEnvironmentTimeOfDay::Day : ECatEnvironmentTimeOfDay::Dusk;
}

// 白天刷新点计算流程：复用 Run 公开的服务器锚点和截止点，把 Morning/Day/Dusk 分界换算成绝对服务器秒；调用方只拿它安排一次性刷新，不生成新的阶段状态。
bool UCatEnvironmentSettings::TryResolveTimeOfDayRefreshTimes(const FCatRunPhaseSnapshot& RunSnapshot,
	double& OutMorningEndServerTimeSeconds, double& OutDuskStartServerTimeSeconds) const
{
	OutMorningEndServerTimeSeconds = 0.0;
	OutDuskStartServerTimeSeconds = 0.0;
	if (!IsRuntimeReady() || RunSnapshot.Phase != ECatRunPhase::DayActive || !RunSnapshot.bHasDeadline
		|| !FMath::IsFinite(RunSnapshot.ServerTimeAnchorSeconds)
		|| !FMath::IsFinite(RunSnapshot.DeadlineServerTimeSeconds)
		|| RunSnapshot.DeadlineServerTimeSeconds <= RunSnapshot.ServerTimeAnchorSeconds)
	{
		return false;
	}
	const double DayDurationSeconds = RunSnapshot.DeadlineServerTimeSeconds - RunSnapshot.ServerTimeAnchorSeconds;
	OutMorningEndServerTimeSeconds = RunSnapshot.ServerTimeAnchorSeconds + DayDurationSeconds * MorningEndFraction;
	OutDuskStartServerTimeSeconds = RunSnapshot.ServerTimeAnchorSeconds + DayDurationSeconds * DuskStartFraction;
	return OutMorningEndServerTimeSeconds > RunSnapshot.ServerTimeAnchorSeconds
		&& OutDuskStartServerTimeSeconds > OutMorningEndServerTimeSeconds
		&& OutDuskStartServerTimeSeconds < RunSnapshot.DeadlineServerTimeSeconds;
}

// 事件裁决流程：现阶段只允许配置事件在可钓白天、已知时段和满足天气约束时生效；夜晚事件以后接正式事件表，而不是让单个配置字段越权代表所有自然事件。
bool UCatEnvironmentSettings::TryResolveActiveEvent(const FCatRunPhaseSnapshot& RunSnapshot,
	const ECatEnvironmentTimeOfDay TimeOfDay, const ECatEnvironmentWeather Weather, FName& OutEventId) const
{
	OutEventId = NAME_None;
	if (!IsRuntimeReady() || !RunSnapshot.RunId.IsValid() || ActiveEventId.IsNone()
		|| Weather == ECatEnvironmentWeather::Unknown
		|| RunSnapshot.Phase != ECatRunPhase::DayActive || TimeOfDay == ECatEnvironmentTimeOfDay::Unknown)
	{
		return false;
	}
	if (ActiveEventRequiredWeather != ECatEnvironmentWeather::Unknown && ActiveEventRequiredWeather != Weather)
	{
		return false;
	}
	OutEventId = ActiveEventId;
	return true;
}

// 自然聚鱼读取流程：先清输出，再要求 Environment 总配置、公共事件、目标区域和合法三轴同时存在；成功只复制数据，不寻找 Actor 或写 WaterRegion。
bool UCatEnvironmentSettings::TryGetNaturalChumField(FName& OutChumDefinitionId, FName& OutAnchorId) const
{
	OutChumDefinitionId = NAME_None;
	OutAnchorId = NAME_None;
	if (!IsRuntimeReady() || ActiveEventId.IsNone() || NaturalChumDefinitionId.IsNone()
		|| NaturalChumAnchorId.IsNone())
	{
		return false;
	}
	OutChumDefinitionId = NaturalChumDefinitionId;
	OutAnchorId = NaturalChumAnchorId;
	return true;
}
