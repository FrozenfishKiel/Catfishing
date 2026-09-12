#include "Data/CatFishSelectionTypes.h"

#include "Data/CatFishDefinition.h"

// 时段门：关闭时全放行。开启后空数组＝这条鱼不受时段约束（鱼表格现在还没有时段列，
// 若把空数组读成「永不出现」，开关一打开整份目录会同时消失）；填了数组才按命中判定。
// 夜晚（TimeOfDay==Unknown）本来就不产生新咬钩，这里保持拒绝。
bool FCatFishEligibilityPolicy::PassesTimeOfDay(const UCatFishDefinition& Definition,
	const ECatEnvironmentTimeOfDay TimeOfDay, const bool bFilterEnabled)
{
	if (!bFilterEnabled || Definition.TimeOfDay.IsEmpty())
	{
		return true;
	}
	return TimeOfDay != ECatEnvironmentTimeOfDay::Unknown && Definition.TimeOfDay.Contains(TimeOfDay);
}

// 天气门：空数组语义与时段门一致——没填就是不受天气约束，不是不出现。
bool FCatFishEligibilityPolicy::PassesWeather(const UCatFishDefinition& Definition,
	const ECatEnvironmentWeather Weather, const bool bFilterEnabled)
{
	if (!bFilterEnabled || Definition.Weather.IsEmpty())
	{
		return true;
	}
	return Weather != ECatEnvironmentWeather::Unknown && Definition.Weather.Contains(Weather);
}

bool FCatFishEligibilityPolicy::PassesActivePlayerCount(const UCatFishDefinition& Definition,
	const int32 ActivePlayerCount)
{
	return ActivePlayerCount >= Definition.MinimumFightParticipants;
}
