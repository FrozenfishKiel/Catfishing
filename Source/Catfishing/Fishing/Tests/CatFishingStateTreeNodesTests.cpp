#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/CatFishingStateTreeNodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStateTreeNodesDefaultsTest,
	"Catfishing.Unit.Fishing.StateTreeNodes.DefaultParametersAreFailClosedAndExposeOnlyExpectedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStateTreeNodesDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFishingEnterPhaseTask EnterPhaseTask;
	TestTrue(TEXT("phase task exposes phase data"),
		EnterPhaseTask.GetInstanceDataType() == FCatFishingEnterPhaseTaskInstanceData::StaticStruct());
	const FCatFishingEnterPhaseTaskInstanceData EnterPhaseData;
	TestEqual(TEXT("phase defaults to Created"), EnterPhaseData.Phase, ECatFishingPhase::Created);
	TestNull(TEXT("phase data has no static near-shore world target"),
		FindFProperty<FProperty>(FCatFishingEnterPhaseTaskInstanceData::StaticStruct(), TEXT("AuthoritativeNearShoreTarget")));

	const FCatFishingWaitTask WaitTask;
	TestTrue(TEXT("wait task exposes empty data"),
		WaitTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	const FCatFishingFightExchangeTask FightExchangeTask;
	TestTrue(TEXT("legacy exchange task remains compatibility-only"),
		FightExchangeTask.GetInstanceDataType() == FCatFishingFightExchangeTaskInstanceData::StaticStruct());
	const FCatFishingFightExchangeTaskInstanceData FightData;
	TestEqual(TEXT("fish stamina cost defaults to zero"), FightData.FishStaminaCost, 0.0);
	TestEqual(TEXT("participant stamina cost defaults to zero"), FightData.ParticipantStaminaCost, 0.0);
	const FCatFishingFailureBudgetTaskInstanceData FailureData;
	TestEqual(TEXT("failure penalty defaults to None"), FailureData.Penalty, ECatFishingFailurePenalty::None);
	const FCatFishingScheduleWaitingProbeTask ScheduleProbeTask;
	const FCatFishingResolveTrueBiteSelectionTask ResolveSelectionTask;
	TestTrue(TEXT("waiting scheduler exposes no overrides"),
		ScheduleProbeTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	TestTrue(TEXT("selection task exposes no overrides"),
		ResolveSelectionTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	return !HasAnyErrors();
}

#endif
