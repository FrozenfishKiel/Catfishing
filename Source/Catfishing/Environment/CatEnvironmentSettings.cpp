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
		|| !FMath::IsFinite(ServerNowSeconds) || RunSnapshot.DeadlineServerTimeSeconds <= RunSnapshot.ServerTimeAnchorSeconds)
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

// 自然聚鱼读取流程：先清输出，再要求 Environment 总配置、公共事件、目标区域和合法三轴同时存在；成功只复制数据，不寻找 Actor 或写 WaterRegion。
bool UCatEnvironmentSettings::TryGetNaturalAggregation(FName& OutRegionId, FCatChumVector& OutContribution) const
{
	OutRegionId = NAME_None;
	OutContribution = FCatChumVector();
	if (!IsRuntimeReady() || ActiveEventId.IsNone() || NaturalAggregationRegionId.IsNone()
		|| !NaturalAggregationContribution.IsValidContribution())
	{
		return false;
	}
	OutRegionId = NaturalAggregationRegionId;
	OutContribution = NaturalAggregationContribution;
	return true;
}
