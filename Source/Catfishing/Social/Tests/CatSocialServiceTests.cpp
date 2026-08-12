#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Social/CatSocialService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceFailClosedTest,
	"Catfishing.Unit.Social.Service.EmptyTeardownAndMissingIdentityCommandsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：取得真实 Social WorldSubsystem，先验证空协议 teardown 可安全收口，再从公开命令入口提交缺身份请求并确认没有伪造成功。
bool FCatSocialServiceFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 SocialService 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatSocialService* Social = World ? World->GetSubsystem<UCatSocialService>() : nullptr;
	TestNotNull(TEXT("SocialService 测试 World 可用"), World);
	TestNotNull(TEXT("真实 SocialService 已创建"), Social);
	if (!Social)
	{
		return false;
	}

	TestTrue(TEXT("没有活跃偷鱼协议时 teardown 可以安全收口"), Social->CloseCommandsAndResolveAll());

	FCatTheftCommand TheftCommand;
	TheftCommand.Context.RequestId = FGuid::NewGuid();
	TheftCommand.Context.ExpectedRevision = 1;
	TheftCommand.FishInstanceId = FGuid::NewGuid();
	TheftCommand.SourceContainerId = FGuid::NewGuid();
	const FCatTheftResult TheftResult = Social->BeginTheft(nullptr, TheftCommand);
	TestFalse(TEXT("缺身份偷鱼不会提交"), TheftResult.Command.bCommitted);
	TestEqual(TEXT("缺身份偷鱼返回 PolicyUndecided"), TheftResult.Command.Error, ECatDomainCommandError::PolicyUndecided);
	TestFalse(TEXT("缺身份偷鱼不分配服务器 ProtocolId"), TheftResult.TheftProtocolId.IsValid());

	const FGuid MischiefRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult Mischief = Social->RequestMischief(nullptr, nullptr, MischiefRequestId, FVector::ZeroVector);
	TestFalse(TEXT("缺身份恶作剧不会提交"), Mischief.bCommitted);
	TestEqual(TEXT("缺身份恶作剧返回 PolicyUndecided"), Mischief.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("恶作剧拒绝保留 RequestId"), Mischief.RequestId, MischiefRequestId);

	const FGuid ManualHelpRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult ManualHelp = Social->RequestManualHelp(
		nullptr, ManualHelpRequestId, ECatHelpSignalKind::ManualFishing);
	TestFalse(TEXT("缺身份手动求助不会提交"), ManualHelp.bCommitted);
	TestEqual(TEXT("缺身份手动求助返回 PolicyUndecided"), ManualHelp.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("手动求助拒绝保留 RequestId"), ManualHelp.RequestId, ManualHelpRequestId);

	const FCatTheftResult CatchUnknown = Social->CatchTheft(nullptr, FGuid::NewGuid());
	TestFalse(TEXT("缺身份追回不会提交"), CatchUnknown.Command.bCommitted);
	TestEqual(TEXT("缺身份追回返回 NotFound"), CatchUnknown.Command.Error, ECatDomainCommandError::NotFound);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
