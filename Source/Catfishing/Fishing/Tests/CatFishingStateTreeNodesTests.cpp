#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/CatFishingStateTreeNodes.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Behavior/CatFishBehaviorStateTree.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/StateTree/CatFishingSessionStateTreeSchema.h"

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
	TestTrue(TEXT("historical exchange task preserves its data type pending asset reference audit"),
		FightExchangeTask.GetInstanceDataType() == FCatFishingFightExchangeTaskInstanceData::StaticStruct());
	const FCatFishingFightExchangeTaskInstanceData FightData;
	TestEqual(TEXT("fish stamina cost defaults to zero"), FightData.FishStaminaCost, 0.0);
	TestEqual(TEXT("participant stamina cost defaults to zero"), FightData.ParticipantStaminaCost, 0.0);
	const FCatFishingFailureBudgetTaskInstanceData FailureData;
	TestEqual(TEXT("failure penalty defaults to None"), FailureData.Penalty, ECatFishingFailurePenalty::None);
	const FCatFishingScheduleWaitingProbeTask ScheduleProbeTask;
	const FCatFishingOpenTrueBiteWindowTask OpenBiteWindowTask;
	const FCatFishingResolveTrueBiteSelectionTask LegacyOpenBiteWindowTask;
	TestTrue(TEXT("waiting scheduler exposes no overrides"),
		ScheduleProbeTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	TestTrue(TEXT("open bite window task exposes no overrides"),
		OpenBiteWindowTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	TestTrue(TEXT("legacy serialized selection node remains load-compatible"),
		LegacyOpenBiteWindowTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	const FCatFishBehaviorStateTask FishBehaviorTask;
	TestTrue(TEXT("fish behavior task exposes only intent and per-entry remaining time"),
		FishBehaviorTask.GetInstanceDataType() == FCatFishBehaviorStateTaskInstanceData::StaticStruct());
	const FCatFishBehaviorStateTaskInstanceData FishBehaviorData;
	TestEqual(TEXT("fish behavior intent fails closed"), FishBehaviorData.MotionIntent, ECatFishMotionIntent::None);
	TestEqual(TEXT("fish behavior duration starts unset"), FishBehaviorData.RemainingSeconds, 0.0);
	const UCatFishBehaviorStateTreeSchema* FishSchema = GetDefault<UCatFishBehaviorStateTreeSchema>();
	TestTrue(TEXT("fish behavior schema binds directly to encounter actor"),
		FishSchema && FishSchema->GetContextActorClass() == ACatFishEncounterActor::StaticClass());
	const UCatFishingSessionStateTreeSchema* SessionSchema = GetDefault<UCatFishingSessionStateTreeSchema>();
	TestTrue(TEXT("fishing session schema binds directly to session actor"),
		SessionSchema && SessionSchema->GetContextActorClass() == ACatFishingSession::StaticClass());
	return !HasAnyErrors();
}

#endif
