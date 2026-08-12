#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Fishing/CatFishingSession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionFailClosedLifecycleTest,
	"Catfishing.Unit.Fishing.Session.StateTreeGateAndTerminateAreFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：生成真实 FishingSession Actor，但不注入 StateTree 资产；阶段入口必须拒绝 C++ fallback，Terminate 只能幂等写入终态一次。
bool FCatFishingSessionFailClosedLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 FishingSession 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("可生成 FishingSession Actor"), Session);
	if (!Session)
	{
		return false;
	}

	const FCatFishingSessionSnapshot Initial = Session->GetSnapshot();
	TestEqual(TEXT("未初始化 Session 初始阶段为 Created"), Initial.Phase, ECatFishingPhase::Created);
	TestFalse(TEXT("未初始化 Session 不是终态"), Session->IsTerminal());

	const FCatFishingPhaseResult PhaseResult = Session->EnterPhaseFromStateTree(
		ECatFishingPhase::HookedFight,
		false,
		FVector::ZeroVector);
	TestFalse(TEXT("缺 StateTree 运行时阶段入口不应用"), PhaseResult.bApplied);
	TestEqual(TEXT("缺 StateTree 返回 DependencyUnavailable"), PhaseResult.Error, ECatDomainCommandError::DependencyUnavailable);
	TestEqual(TEXT("失败阶段入口不改公开阶段"), Session->GetSnapshot().Phase, ECatFishingPhase::Created);
	TestEqual(TEXT("失败阶段入口不推进 Revision"), Session->GetSnapshot().Revision, Initial.Revision);

	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 1);
	Session->TerminateSession(TEXT("Automation"));
	TestTrue(TEXT("Terminate 后进入终态"), Session->IsTerminal());
	TestEqual(TEXT("Terminate 写入 Terminated 阶段"), Session->GetSnapshot().Phase, ECatFishingPhase::Terminated);
	TestEqual(TEXT("Terminate 只推进一次 Revision"), Session->GetSnapshot().Revision, Initial.Revision + 1);
	Session->TerminateSession(TEXT("AutomationReplay"));
	TestEqual(TEXT("重复 Terminate 不再次推进 Revision"), Session->GetSnapshot().Revision, Initial.Revision + 1);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
