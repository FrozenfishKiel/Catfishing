#include "Data/CatFishSelectionTypes.h"

#include "Data/CatFishDefinition.h"

bool FCatFishEligibilityPolicy::PassesTimeOfDay(const UCatFishDefinition& Definition,
	const ECatEnvironmentTimeOfDay TimeOfDay, const bool bFilterEnabled)
{
	return !bFilterEnabled || (TimeOfDay != ECatEnvironmentTimeOfDay::Unknown
		&& Definition.TimeOfDay.Contains(TimeOfDay));
}

bool FCatFishEligibilityPolicy::PassesWeather(const UCatFishDefinition& Definition,
	const ECatEnvironmentWeather Weather, const bool bFilterEnabled)
{
	return !bFilterEnabled || (Weather != ECatEnvironmentWeather::Unknown
		&& Definition.Weather.Contains(Weather));
}

bool FCatFishEligibilityPolicy::PassesActivePlayerCount(const UCatFishDefinition& Definition,
	const int32 ActivePlayerCount)
{
	return ActivePlayerCount >= Definition.MinimumFightParticipants;
}
