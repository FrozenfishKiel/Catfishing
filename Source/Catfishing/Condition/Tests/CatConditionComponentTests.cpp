#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Condition/CatConditionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentWetAuthorityTest,
	"Catfishing.Unit.Condition.Component.AuthorityWetChangesRevisionAndRepeatDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentCarryToCampTest,
	"Catfishing.Unit.Condition.Component.CarryToCampRequiresValidRescueFactAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatConditionComponentTest
{
	// 组件装配流程：用普通 authority Actor 承载 Condition 组件；这些测试只覆盖不依赖 ASC/Character 的公开合同。
	static UCatConditionComponent* AddConditionComponent(AActor* Host)
	{
		UCatConditionComponent* Component = Host ? NewObject<UCatConditionComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}
}

// 测试流程：在 authority Actor 上切换 Wet，读取公开 Snapshot 的 Revision/Wet；重复写相同值必须保持 Revision 不变。
bool FCatConditionComponentWetAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition Wet 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatConditionComponent* Component = CatConditionComponentTest::AddConditionComponent(Host);
		TestNotNull(TEXT("Wet 测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Wet 测试 Condition 组件可创建"), Component);
		if (Component)
		{
			TestEqual(TEXT("初始 Condition Revision 为 0"), Component->GetSnapshot().Revision, int64{0});
			TestFalse(TEXT("初始 Wet 为 false"), Component->GetSnapshot().bWet);
			Component->SetWetFromAuthority(true);
			TestTrue(TEXT("authority 设置 Wet 后为 true"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("authority 设置 Wet 推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(true);
			TestEqual(TEXT("重复设置相同 Wet 不推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(false);
			TestFalse(TEXT("authority 清 Wet 后为 false"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("Wet 真实变化再次推进 Revision"), Component->GetSnapshot().Revision, int64{2});
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先用无效救援事实证明 CarryToCamp 拒绝，再用真实 Controller 和营地事实提交并原样重放；Snapshot 只能提交一次。
bool FCatConditionComponentCarryToCampTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition 搬运测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		AController* Helper = World ? World->SpawnActor<APlayerController>() : nullptr;
		UCatConditionComponent* Component = CatConditionComponentTest::AddConditionComponent(Host);
		TestNotNull(TEXT("搬运测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("搬运测试 Controller 可创建"), Helper);
		TestNotNull(TEXT("搬运测试 Condition 组件可创建"), Component);
		if (Component)
		{
			const FGuid InvalidRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult Invalid = Component->CompleteCarryToCamp(Helper, InvalidRequestId, false);
			TestFalse(TEXT("未到营地救援点时搬运不提交"), Invalid.bCommitted);
			TestEqual(TEXT("未到营地救援点返回 InvalidPayload"), Invalid.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("失败搬运不推进 Revision"), Component->GetSnapshot().Revision, int64{0});

			const FGuid RequestId = FGuid::NewGuid();
			const FCatDomainCommandResult First = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestTrue(TEXT("有效搬运提交成功"), First.bCommitted);
			TestEqual(TEXT("有效搬运无错误"), First.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("有效搬运推进 Revision"), First.Revision, int64{1});
			TestEqual(TEXT("有效搬运写入恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::CarriedToCamp);

			const FCatDomainCommandResult Replay = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestFalse(TEXT("搬运重放不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("搬运重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("搬运重放不推进 Revision"), Replay.Revision, First.Revision);
			TestEqual(TEXT("搬运重放后 Snapshot Revision 不变"), Component->GetSnapshot().Revision, int64{1});
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
