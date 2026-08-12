#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Framework/Game/CatGameplayTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeRunCommandFailClosedTest,
	"Catfishing.Unit.Framework.GameMode.RunCommandsFailClosedBeforeRuntimeStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：生成真实 Lake GameMode 但不启动 Run StateTree；协调器额度写口必须在命令门关闭时拒绝，并把首次终态缓存为可重放结果。
bool FCatGameModeRunCommandFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 GameMode 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatfishingGameModeBase* GameMode = World->SpawnActor<ACatfishingGameModeBase>();
	TestNotNull(TEXT("可生成项目 Lake GameMode"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	FCatQuotaContributionCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = 0;
	Command.Context.StableNetId = TEXT("CoordinatorStableId");
	Command.Contribution = 3;
	const FCatRunCommandResult First = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("Run 未启动前额度写口不提交"), First.bCommitted);
	TestEqual(TEXT("Run 未启动前返回 CommandsClosed"), First.Error, ECatRunCommandError::CommandsClosed);
	TestEqual(TEXT("拒绝结果关联原 RequestId"), First.RequestId, Command.Context.RequestId);

	const FCatRunCommandResult Replay = GameMode->SubmitCommittedQuotaContributionFromCoordinator(Command);
	TestFalse(TEXT("同一请求重放不提交"), Replay.bCommitted);
	TestEqual(TEXT("同一请求重放返回 AlreadyResolved"), Replay.Error, ECatRunCommandError::AlreadyResolved);
	TestEqual(TEXT("重放保留首次 Revision"), Replay.Revision, First.Revision);
	TestEqual(TEXT("默认 Run 公开状态仍未开始"), GameMode->GetRunPublicState().Phase.Phase, ECatRunPhase::NotStarted);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
