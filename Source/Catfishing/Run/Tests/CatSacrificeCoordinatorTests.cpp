#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Run/CatSacrificeCoordinator.h"
#include "ShopEconomy/CatShopEconomyService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSacrificeCoordinatorTeardownGateTest,
	"Catfishing.Unit.Run.SacrificeCoordinator.TeardownClosesNewSacrificeCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从真实 Game World 取得 SacrificeCoordinator，先确认跟局清空的商店经济服务存在，再执行空协议 teardown，最
// 后提交一条结构完整但无 Controller 的命令；关闭后的错误必须优先暴露 CommandsClosed。
// teardown 现在把 ShopEconomy 也算进必须收口的领域，所以这里断言它在同一 World 存在；商店关闭后的具体拒绝错误依赖商店
// runtime 配置，本用例不构造那套 fixture。
bool FCatSacrificeCoordinatorTeardownGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 SacrificeCoordinator 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatSacrificeCoordinator* Coordinator = World->GetSubsystem<UCatSacrificeCoordinator>();
	TestNotNull(TEXT("可取得 SacrificeCoordinator"), Coordinator);
	if (!Coordinator)
	{
		return false;
	}

	TestNotNull(TEXT("teardown 依赖的一局商店经济服务存在"), World->GetSubsystem<UCatShopEconomyService>());
	TestTrue(TEXT("无活跃协议时 teardown 可以收口"), Coordinator->PrepareForRunTeardown());

	FCatSacrificeCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = 1;
	Command.FishInstanceId = FGuid::NewGuid();
	Command.ContainerId = FGuid::NewGuid();
	Command.ExpectedRunRevision = 1;
	const FCatSacrificeResult Result = Coordinator->RequestSacrifice(nullptr, Command);
	TestFalse(TEXT("teardown 后不接受新献祭"), Result.bCompleted);
	TestEqual(TEXT("teardown 后返回 CommandsClosed"), Result.Error, ECatDomainCommandError::CommandsClosed);
	TestEqual(TEXT("拒绝结果仍关联原 RequestId"), Result.RequestId, Command.Context.RequestId);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
