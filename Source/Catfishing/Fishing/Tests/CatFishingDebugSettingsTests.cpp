#if WITH_DEV_AUTOMATION_TESTS && ENABLE_DRAW_DEBUG

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingDebugSettingsDefaultsTest,
	"Catfishing.Unit.Fishing.Debug.WorldMarkersDefaultOffAndStatsDefaultOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingDebugSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	IConsoleVariable* WorldDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.Debug"));
	IConsoleVariable* StatsDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.Stats"));
	TestNotNull(TEXT("world debug CVar is registered"), WorldDebug);
	TestNotNull(TEXT("stats debug CVar is registered independently"), StatsDebug);
	if (WorldDebug && StatsDebug)
	{
		TestTrue(TEXT("world markers and stats are separate console variables"), WorldDebug != StatsDebug);
		TestEqual(TEXT("world markers default off"), WorldDebug->GetDefaultValue(), FString(TEXT("0")));
		TestEqual(TEXT("stats panel default on"), StatsDebug->GetDefaultValue(), FString(TEXT("1")));
	}
	return !HasAnyErrors();
}

#endif
