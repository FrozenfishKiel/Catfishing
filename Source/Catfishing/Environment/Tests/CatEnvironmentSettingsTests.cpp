#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Environment/CatEnvironmentSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSkipMorningTest,
	"Catfishing.Unit.Environment.TimeOfDay.SkipMorning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatEnvironmentSkipMorningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatEnvironmentSettings* Settings = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
	Settings->bEnableEnvironmentRuntime = true;
	Settings->ConfiguredWeather = ECatEnvironmentWeather::Clear;
	Settings->MorningEndFraction = 0.0;
	Settings->DuskStartFraction = 0.8;

	FCatRunPhaseSnapshot Run;
	Run.Phase = ECatRunPhase::DayActive;
	Run.bHasDeadline = true;
	Run.ServerTimeAnchorSeconds = 100.0;
	Run.DeadlineServerTimeSeconds = 100.0 + 99999.0;
	double MorningAt = 0.0;
	double DuskAt = 0.0;
	TestTrue(TEXT("zero morning is a valid configured day"), Settings->IsRuntimeReady());
	TestEqual(TEXT("first snapshot is Day at the exact start"),
		Settings->ResolveTimeOfDay(Run, 100.0), ECatEnvironmentTimeOfDay::Day);
	TestTrue(TEXT("skipping morning preserves refresh scheduling"),
		Settings->TryResolveTimeOfDayRefreshTimes(Run, MorningAt, DuskAt));
	TestEqual(TEXT("morning boundary is already reached, so no morning timer is needed"), MorningAt, 100.0);
	TestEqual(TEXT("dusk still uses the full 99999 second window"), DuskAt, 100.0 + 99999.0 * 0.8);
	TestEqual(TEXT("day lasts until dusk"),
		Settings->ResolveTimeOfDay(Run, DuskAt - 1.0), ECatEnvironmentTimeOfDay::Day);
	TestEqual(TEXT("dusk starts on its boundary"),
		Settings->ResolveTimeOfDay(Run, DuskAt), ECatEnvironmentTimeOfDay::Dusk);
	Run.Phase = ECatRunPhase::NormalNight;
	TestEqual(TEXT("skip morning never turns night into day"),
		Settings->ResolveTimeOfDay(Run, 100.0), ECatEnvironmentTimeOfDay::Unknown);
	Run.Phase = ECatRunPhase::DayActive;

	Settings->MorningEndFraction = 0.3;
	TestEqual(TEXT("positive fraction restores morning"),
		Settings->ResolveTimeOfDay(Run, 100.0), ECatEnvironmentTimeOfDay::Morning);
	TestTrue(TEXT("normal morning refresh remains valid"),
		Settings->TryResolveTimeOfDayRefreshTimes(Run, MorningAt, DuskAt));
	TestTrue(TEXT("normal morning boundary remains in the future"), MorningAt > 100.0);
	TestEqual(TEXT("normal morning transitions to day"),
		Settings->ResolveTimeOfDay(Run, MorningAt + 1.0), ECatEnvironmentTimeOfDay::Day);

	Settings->MorningEndFraction = -0.1;
	TestFalse(TEXT("negative morning is rejected"), Settings->IsRuntimeReady());
	Settings->MorningEndFraction = Settings->DuskStartFraction;
	TestFalse(TEXT("overlapping boundaries are rejected"), Settings->IsRuntimeReady());
	Settings->MorningEndFraction = 0.0;
	Settings->DuskStartFraction = 0.0;
	TestFalse(TEXT("unset boundaries remain invalid"), Settings->IsRuntimeReady());
	Settings->DuskStartFraction = 1.0;
	TestFalse(TEXT("dusk at deadline remains invalid"), Settings->IsRuntimeReady());
	return !HasAnyErrors();
}

#endif
