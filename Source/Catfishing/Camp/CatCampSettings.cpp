#include "Camp/CatCampSettings.h"
#include "Logging/CatLog.h"

double UCatCampSettings::GetPlayerEntryRingRadiusCentimeters() const
{
	if (FMath::IsFinite(PlayerEntryRingRadiusCentimeters) && PlayerEntryRingRadiusCentimeters > 0.0)
		return PlayerEntryRingRadiusCentimeters;
	static bool bWarned = false;
	if (!bWarned)
	{
		bWarned = true;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=camp_spawn_radius_invalid Result=LegacyDefault RadiusCentimeters=300"));
	}
	return 300.0;
}

// 营地 gate 流程：只接受显式启用和有限正交互范围；未裁时所有营地命令 fail-closed，Actor 仍可作为固定美术宿主。
bool UCatCampSettings::IsRuntimeReady() const
{
	return bEnableCampRuntime && FMath::IsFinite(InteractionRadiusCentimeters) && InteractionRadiusCentimeters > 0.0;
}
