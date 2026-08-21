#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatTeamEquipmentLibraryAdapterLifecycleTest,
	"Catfishing.Unit.Equipment.TeamLibrary.GrantTakeReplayAndClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatTeamEquipmentLibraryAdapterLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建团队装备库测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UCatTeamEquipmentLibrary* Library = WorldWrapper.GetTestWorld()
		? WorldWrapper.GetTestWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	TestNotNull(TEXT("服务器团队装备库可创建"), Library);
	if (!Library)
	{
		return false;
	}

	FCatTeamEquipmentGrantCommand Grant;
	Grant.Context.RequestId = FGuid::NewGuid();
	Grant.Context.ExpectedRevision = 0;
	Grant.Context.StableNetId = TEXT("TeamLibraryPlayer");
	Grant.SourceTransactionId = FGuid::NewGuid();
	Grant.DefinitionId = TEXT("Rod_Basic");
	const FCatTeamEquipmentGrantResult Granted = Library->GrantFromShopOrder(Grant);
	TestTrue(TEXT("本地基础竿首次入库成功"), Granted.Command.bCommitted);
	TestEqual(TEXT("首次入库无错误"), Granted.Command.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("装备实例 ID 由服务器创建"), Granted.Instance.InstanceId.IsValid());
	TestEqual(TEXT("入库后版本推进"), Library->GetSnapshot().Revision, static_cast<int64>(1));
	TestEqual(TEXT("库里只有一件实物"), Library->GetSnapshot().Instances.Num(), 1);

	const FCatTeamEquipmentGrantResult GrantReplay = Library->GrantFromShopOrder(Grant);
	TestFalse(TEXT("相同入库命令重放不再创建"), GrantReplay.Command.bCommitted);
	TestEqual(TEXT("入库重放返回 AlreadyResolved"), GrantReplay.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("入库重放返回同一实例"), GrantReplay.Instance.InstanceId,
		Granted.Instance.InstanceId);
	TestEqual(TEXT("重放后仍只有一件"), Library->GetSnapshot().Instances.Num(), 1);

	FCatTeamEquipmentTakeCommand Take;
	Take.Context.RequestId = FGuid::NewGuid();
	Take.Context.ExpectedRevision = 1;
	Take.Context.StableNetId = TEXT("TeamLibraryPlayer");
	Take.InstanceId = Granted.Instance.InstanceId;
	FCatTeamEquipmentInstance Preview;
	TestEqual(TEXT("取用预检通过"), Library->ValidateTake(Take, Preview),
		ECatDomainCommandError::None);
	TestEqual(TEXT("预检返回目标实例"), Preview.InstanceId, Granted.Instance.InstanceId);

	const FCatTeamEquipmentGrantResult Taken = Library->TakeInstance(Take);
	TestTrue(TEXT("首次取用提交成功"), Taken.Command.bCommitted);
	TestEqual(TEXT("取用后版本推进"), Library->GetSnapshot().Revision, static_cast<int64>(2));
	TestTrue(TEXT("取用后公库为空"), Library->GetSnapshot().Instances.IsEmpty());

	const FCatTeamEquipmentGrantResult TakeReplay = Library->TakeInstance(Take);
	TestFalse(TEXT("取用重放不再删除"), TakeReplay.Command.bCommitted);
	TestEqual(TEXT("取用重放返回 AlreadyResolved"), TakeReplay.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("取用重放仍返回同一实例"), TakeReplay.Instance.InstanceId,
		Granted.Instance.InstanceId);

	Library->CloseCommands();
	FCatTeamEquipmentGrantCommand ClosedGrant = Grant;
	ClosedGrant.Context.RequestId = FGuid::NewGuid();
	ClosedGrant.Context.ExpectedRevision = Library->GetSnapshot().Revision;
	ClosedGrant.SourceTransactionId = FGuid::NewGuid();
	const FCatTeamEquipmentGrantResult Closed = Library->GrantFromShopOrder(ClosedGrant);
	TestEqual(TEXT("关门后新订单被拒绝"), Closed.Command.Error,
		ECatDomainCommandError::CommandsClosed);
	return !HasAnyErrors();
}

#endif
