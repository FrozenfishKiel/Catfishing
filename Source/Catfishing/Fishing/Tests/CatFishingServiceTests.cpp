#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceFailClosedTest,
	"Catfishing.Unit.Fishing.Service.InvalidIdentityAndUnknownSessionFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceUnknownQueriesTest,
	"Catfishing.Unit.Fishing.Service.UnknownSessionQueriesAreSideEffectFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceWindowClosureReleasesOperatorMovementTest,
	"Catfishing.Unit.Fishing.Service.WindowClosureReleasesOperatorMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：取得真实 Fishing WorldSubsystem 后从三个公开入口提交缺身份/未知会话命令；结果必须明确拒绝且不会创建可观察会话。
bool FCatFishingServiceFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 FishingService 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	TestNotNull(TEXT("FishingService 测试 World 可用"), World);
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	if (!Fishing)
	{
		return false;
	}

	const FGuid AssistRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult AssistResult = Fishing->SubmitFightAssist(
		FGuid::NewGuid(), nullptr, AssistRequestId, 1);
	TestFalse(TEXT("未知会话协作不提交"), AssistResult.bCommitted);
	TestEqual(TEXT("未知会话协作返回 NotFound"), AssistResult.Error, ECatDomainCommandError::NotFound);
	TestEqual(TEXT("协作拒绝保留 RequestId"), AssistResult.RequestId, AssistRequestId);

	FCatScoopCommand ScoopCommand;
	ScoopCommand.Context.RequestId = FGuid::NewGuid();
	ScoopCommand.Context.ExpectedRevision = 1;
	const FCatScoopResult ScoopResult = Fishing->RequestScoop(FGuid::NewGuid(), nullptr, ScoopCommand);
	TestFalse(TEXT("未知会话抢抄不提交"), ScoopResult.Command.bCommitted);
	TestEqual(TEXT("未知会话抢抄返回 NotFound"), ScoopResult.Command.Error, ECatDomainCommandError::NotFound);
	TestEqual(TEXT("抢抄拒绝保留 RequestId"), ScoopResult.Command.RequestId, ScoopCommand.Context.RequestId);

	Fishing->CloseCommandsAndTerminateAll();
	return !HasAnyErrors();
}

// 查询契约：无效/未知 Session、Controller 与 PlayerState 必须清空输出且不能创建任何 Session/Rod 索引项。
bool FCatFishingServiceUnknownQueriesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 FishingService 查询测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	APlayerState* UnknownPlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	TestNotNull(TEXT("未知 PlayerState 夹具已创建"), UnknownPlayerState);
	if (!Fishing || !UnknownPlayerState)
	{
		return false;
	}

	const int32 SessionCountBefore = Fishing->GetTrackedSessionCountForDiagnostics();
	const int32 RodCountBefore = Fishing->GetDeployedRodCountForDiagnostics();
	TestNull(TEXT("无效 SessionId 查询返回空"), Fishing->FindSession(FGuid()));
	TestNull(TEXT("未知 SessionId 查询返回空"), Fishing->FindSession(FGuid::NewGuid()));

	FGuid OutFishingSessionId = FGuid::NewGuid();
	FCatFishingSessionSnapshot OutSnapshot;
	OutSnapshot.FishingSessionId = FGuid::NewGuid();
	OutSnapshot.Revision = 41;
	OutSnapshot.SnapshotSequence = 42;
	OutSnapshot.Phase = ECatFishingPhase::Resolved;
	OutSnapshot.Outcome = ECatFishingOutcome::Caught;
	OutSnapshot.bReeling = true;
	TestFalse(TEXT("无 Controller 的活动 Session 查询失败"),
		Fishing->TryGetActiveSessionForController(nullptr, OutFishingSessionId, OutSnapshot));
	TestFalse(TEXT("失败查询清空 SessionId 输出"), OutFishingSessionId.IsValid());
	TestFalse(TEXT("失败查询清空 Snapshot SessionId"), OutSnapshot.FishingSessionId.IsValid());
	TestEqual(TEXT("失败查询恢复默认 Revision"), OutSnapshot.Revision, int64{0});
	TestEqual(TEXT("失败查询恢复默认 SnapshotSequence"), OutSnapshot.SnapshotSequence, int64{0});
	TestEqual(TEXT("失败查询恢复默认 Phase"), OutSnapshot.Phase, ECatFishingPhase::Created);
	TestEqual(TEXT("失败查询恢复默认 Outcome"), OutSnapshot.Outcome, ECatFishingOutcome::None);
	TestFalse(TEXT("失败查询恢复默认收线状态"), OutSnapshot.bReeling);

	TestNull(TEXT("空 PlayerState 鱼竿查询返回空"), Fishing->FindDeployedRod(nullptr));
	TestNull(TEXT("未知 PlayerState 鱼竿查询返回空"), Fishing->FindDeployedRod(UnknownPlayerState));
	TestEqual(TEXT("未知查询不改变 Session 计数"),
		Fishing->GetTrackedSessionCountForDiagnostics(), SessionCountBefore);
	TestEqual(TEXT("未知查询不改变鱼竿计数"),
		Fishing->GetDeployedRodCountForDiagnostics(), RodCountBefore);
	return !HasAnyErrors();
}

// 钓鱼资格关闭契约：角色已在竿位且移动为 MOVE_None 时，Run 钓鱼窗口关闭必须先清操作槽并恢复 Walking；
// 同一角色随后再次上竿，倒地/失去资格的 Character 中断路径也必须完成相同补偿。
bool FCatFishingServiceWindowClosureReleasesOperatorMovementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Fishing 窗口关闭测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	WorldWrapper.BeginPlayInTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	APlayerState* PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	if (!TestNotNull(TEXT("FishingService 存在"), Fishing)
		|| !TestNotNull(TEXT("操作角色存在"), Character)
		|| !TestNotNull(TEXT("操作 PlayerState 存在"), PlayerState)
		|| !TestNotNull(TEXT("部署鱼竿存在"), Rod))
	{
		return false;
	}

	Character->SetPlayerState(PlayerState);
	TestTrue(TEXT("鱼竿以当前角色占据主位初始化"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), TEXT("Rod"), TEXT("Skin"), PlayerState, PlayerState, true, false));
	TestTrue(TEXT("部署鱼竿登记成功"), Fishing->RegisterDeployedRod(PlayerState, Rod));
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	TestNotNull(TEXT("角色移动组件存在"), Movement);
	if (!Movement)
	{
		return false;
	}

	Movement->DisableMovement();
	TestEqual(TEXT("夹具进入竿位移动锁"), Movement->MovementMode.GetValue(), MOVE_None);
	Fishing->SuspendFishingAndReleaseOperators();
	TestEqual(TEXT("窗口关闭清空全部操作槽"), Rod->GetOperatorCount(), 0);
	TestEqual(TEXT("窗口关闭恢复 Walking"), Movement->MovementMode.GetValue(), MOVE_Walking);
	TestTrue(TEXT("窗口关闭不收走已部署鱼竿"), Rod->GetPresentationState().bDeployed);

	int32 RejoinedSlot = INDEX_NONE;
	TestTrue(TEXT("下一钓鱼窗口可重新占据原鱼竿"), Rod->AddOperatorFromAuthority(
		PlayerState, Rod->GetPresentationState().RodActorRevision, RejoinedSlot));
	TestEqual(TEXT("重新进入主位"), RejoinedSlot, 0);
	Movement->DisableMovement();
	Fishing->TerminateSessionsForCharacter(Character);
	TestEqual(TEXT("Character 中断清空自身操作槽"), Rod->GetOperatorCount(), 0);
	TestEqual(TEXT("Character 中断恢复 Walking"), Movement->MovementMode.GetValue(), MOVE_Walking);

	TestTrue(TEXT("异常销毁前可再次占据原鱼竿"), Rod->AddOperatorFromAuthority(
		PlayerState, Rod->GetPresentationState().RodActorRevision, RejoinedSlot));
	Movement->DisableMovement();
	TestTrue(TEXT("销毁鱼竿触发 EndPlay 清理"), Rod->Destroy());
	World->Tick(ELevelTick::LEVELTICK_All, 0.01f);
	TestEqual(TEXT("鱼竿异常销毁恢复 Walking"), Movement->MovementMode.GetValue(), MOVE_Walking);
	TestNull(TEXT("鱼竿异常销毁移除部署登记"), Fishing->FindDeployedRod(PlayerState));

	Movement->DisableMovement();
	Fishing->TerminateSessionsForCharacter(Character);
	TestEqual(TEXT("鱼竿登记已失效时仍恢复 Walking"), Movement->MovementMode.GetValue(), MOVE_Walking);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
