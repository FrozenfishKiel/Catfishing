#include "Fishing/Debug/CatFishingMotionDiagnostics.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFishingMotionLog(TEXT("cat.Fishing.MotionLog"), 1,
		TEXT("Detailed fight motion logging in development builds: 1 enables phase/fixed-step and up to 60 Hz motion samples; 0 restores baseline sampling."));
}

bool CatFishingMotionDiagnostics::IsDetailedEnabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return CVarFishingMotionLog.GetValueOnGameThread() != 0;
#endif
}

double CatFishingMotionDiagnostics::SampleIntervalSeconds()
{
	return IsDetailedEnabled() ? 1.0 / 60.0 : 1.0;
}
