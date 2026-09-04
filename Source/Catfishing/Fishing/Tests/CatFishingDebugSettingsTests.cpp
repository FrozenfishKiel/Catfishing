#if WITH_DEV_AUTOMATION_TESTS && ENABLE_DRAW_DEBUG

#include "HAL/IConsoleManager.h"
#include "Fishing/Debug/CatFishingDebugSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingDebugSettingsDefaultsTest,
	"Catfishing.Unit.Fishing.Debug.WorldMarkersDefaultOffAndStatsDefaultOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingDebugFishTypeLineTest,
	"Catfishing.Unit.Fishing.Debug.StatsPanelShowsCurrentFishDefinitionId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingDebugFishTypeLineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("没有当前鱼时鱼种行显示占位"),
		UCatFishingDebugSubsystem::FormatFishTypeLine(NAME_None), FString(TEXT("FISH TYPE  --")));
	TestEqual(TEXT("存在当前鱼时显示复制快照里的稳定鱼种 ID"),
		UCatFishingDebugSubsystem::FormatFishTypeLine(TEXT("RiverPattern")),
		FString(TEXT("FISH TYPE  RiverPattern")));
	return !HasAnyErrors();
}

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
