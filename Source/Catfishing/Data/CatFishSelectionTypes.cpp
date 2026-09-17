#include "Data/CatFishSelectionTypes.h"

#include "Data/CatFishDefinition.h"
#include "Logging/CatLog.h"

// T23：关闭开关保持 D-31；开启后缺值拒绝并告警，空数组不是不限。
bool FCatFishEligibilityPolicy::PassesTimeOfDay(const UCatFishDefinition& Definition,
	const ECatEnvironmentTimeOfDay TimeOfDay, const bool bFilterEnabled)
{
	if (!bFilterEnabled)
	{
		return true;
	}
	if (Definition.TimeOfDay.IsEmpty())
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fish_eligibility_unconfigured Fish=%s Axis=TimeOfDay Result=Rejected"), *FString::FromInt(Definition.ItemId));
		return false;
	}
	return TimeOfDay != ECatEnvironmentTimeOfDay::Unknown && Definition.TimeOfDay.Contains(TimeOfDay);
}

// 天气门与时段门同样 fail-closed。
bool FCatFishEligibilityPolicy::PassesWeather(const UCatFishDefinition& Definition,
	const ECatEnvironmentWeather Weather, const bool bFilterEnabled)
{
	if (!bFilterEnabled)
	{
		return true;
	}
	if (Definition.Weather.IsEmpty())
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fish_eligibility_unconfigured Fish=%s Axis=Weather Result=Rejected"), *FString::FromInt(Definition.ItemId));
		return false;
	}
	return Weather != ECatEnvironmentWeather::Unknown && Definition.Weather.Contains(Weather);
}

bool FCatFishEligibilityPolicy::PassesActivePlayerCount(const UCatFishDefinition& Definition,
	const int32 ActivePlayerCount)
{
	return ActivePlayerCount >= Definition.MinimumFightParticipants;
}
