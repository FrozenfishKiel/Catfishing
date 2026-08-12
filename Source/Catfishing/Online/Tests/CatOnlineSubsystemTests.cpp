#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Engine/GameInstance.h"
#include "Online/CatOnlineSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatOnlineSubsystemInitialSnapshotTest,
	"Catfishing.Unit.Online.Subsystem.UnknownWorldRejectsRequestsWithoutStartingOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在 FTestWorldWrapper 的临时 Game World 中取得真实 GameInstanceSubsystem；未知地图包名下所有请求必须同步拒绝，不得启动平台操作。
bool FCatOnlineSubsystemInitialSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 OnlineSubsystem 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	TestNotNull(TEXT("Online 测试 World 可用"), World);
	TestNotNull(TEXT("测试 GameInstance 可用"), GameInstance);
	TestNotNull(TEXT("真实 Online GameInstanceSubsystem 已创建"), Online);
	if (!Online)
	{
		return false;
	}

	const FCatOnlineSnapshot Initial = Online->GetSnapshot();
	TestEqual(TEXT("测试 World 不是 Frontend/Lake 时 WorldState 为 Error"), Initial.WorldState, ECatOnlineWorldState::Error);
	TestEqual(TEXT("初始没有 NamedSession"), Initial.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("初始没有活动操作"), Initial.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("初始没有结构化错误"), Initial.LastError, ECatOnlineError::None);

	const FCatOnlineResult HostResult = Online->RequestCreateSession();
	TestFalse(TEXT("未知 World 下 Host 请求不被受理"), HostResult.bAccepted);
	TestEqual(TEXT("未知 World 下 Host 返回 InvalidState"), HostResult.Error, ECatOnlineError::InvalidState);
	const FCatOnlineSnapshot AfterHost = Online->GetSnapshot();
	TestEqual(TEXT("拒绝后仍没有活动操作"), AfterHost.ActiveOperation, ECatOnlineOperation::None);
	TestEqual(TEXT("拒绝后仍没有会话"), AfterHost.SessionState, ECatOnlineSessionState::NoSession);
	TestEqual(TEXT("拒绝错误进入快照"), AfterHost.LastError, ECatOnlineError::InvalidState);

	FCatSessionSearchHandle InvalidSearchHandle;
	InvalidSearchHandle.Value = FGuid::NewGuid();
	const FCatOnlineResult JoinResult = Online->RequestJoinSession(InvalidSearchHandle);
	TestFalse(TEXT("未知句柄 Join 不被受理"), JoinResult.bAccepted);
	TestEqual(TEXT("未知句柄 Join 返回 InvalidHandle"), JoinResult.Error, ECatOnlineError::InvalidHandle);

	const FCatOnlineResult LeaveResult = Online->RequestLeave();
	TestFalse(TEXT("无 Lake/Session 时 Leave 不被受理"), LeaveResult.bAccepted);
	TestEqual(TEXT("无 Lake/Session Leave 返回 InvalidState"), LeaveResult.Error, ECatOnlineError::InvalidState);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
