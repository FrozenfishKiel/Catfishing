#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "OnlineSubsystemTypes.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceRodBoundSessionRoutingTest,
	"Catfishing.Unit.Fishing.Service.SessionSurvivesLeaveAndInputRoutesByCurrentRod",
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

// 多竿路由契约：角色离开第一根竿只释放竿位，原会话保持；进入第二根竿后，输入查询只能返回第二根竿的会话。
bool FCatFishingServiceRodBoundSessionRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建多竿会话路由测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	ACatfishingPlayerState* PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
	APlayerState* SecondRodOwner = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishingRodActor* FirstRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* SecondRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingSession* FirstSession = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishingSession* SecondSession = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	if (!TestNotNull(TEXT("FishingService 可用"), Fishing)
		|| !TestNotNull(TEXT("Controller 可用"), Controller)
		|| !TestNotNull(TEXT("PlayerState 可用"), PlayerState)
		|| !TestNotNull(TEXT("第二根竿所有者可用"), SecondRodOwner)
		|| !TestNotNull(TEXT("Character 可用"), Character)
		|| !TestNotNull(TEXT("第一根竿可用"), FirstRod)
		|| !TestNotNull(TEXT("第二根竿可用"), SecondRod)
		|| !TestNotNull(TEXT("第一会话可用"), FirstSession)
		|| !TestNotNull(TEXT("第二会话可用"), SecondSession))
	{
		return false;
	}

	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("MultiRodFisher"), FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	const FGuid FirstRodId = FGuid::NewGuid();
	const FGuid SecondRodId = FGuid::NewGuid();
	TestTrue(TEXT("第一根竿以玩家占据主位初始化"), FirstRod->InitializeAuthoritativeIdentity(
		FirstRodId, TEXT("RodA"), TEXT("SkinA"), PlayerState, PlayerState, true, false));
	TestTrue(TEXT("第二根竿以空主位初始化"), SecondRod->InitializeAuthoritativeIdentity(
		SecondRodId, TEXT("RodB"), TEXT("SkinB"), SecondRodOwner, nullptr, true, false));
	TestTrue(TEXT("登记第一根竿"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestTrue(TEXT("登记第二根竿"), Fishing->RegisterDeployedRod(SecondRodOwner, SecondRod));

	const FGuid FirstSessionId = FGuid::NewGuid();
	FirstSession->Snapshot.FishingSessionId = FirstSessionId;
	FirstSession->Snapshot.Phase = ECatFishingPhase::HookedFight;
	FirstSession->Snapshot.FisherPlayerState = PlayerState;
	FirstSession->Snapshot.RodActor = FirstRod;
	FirstSession->Snapshot.bReeling = true;
	Fishing->Sessions.Add(FirstSessionId, FirstSession);
	FGuid RoutedSessionId;
	FCatFishingSessionSnapshot RoutedSnapshot;
	TestTrue(TEXT("在第一根竿主位时路由第一会话"),
		Fishing->TryGetActiveSessionForController(Controller, RoutedSessionId, RoutedSnapshot));
	TestEqual(TEXT("第一会话身份匹配"), RoutedSessionId, FirstSessionId);

	FCatLeaveRodCommand Leave;
	Leave.Context.RequestId = FGuid::NewGuid();
	Leave.Context.RodActorId = FirstRodId;
	Leave.Context.ExpectedRodActorRevision = FirstRod->GetPresentationState().RodActorRevision;
	const FCatFishingCommandResult LeaveResult = Fishing->LeaveRod(Controller, Leave);
	TestTrue(TEXT("离开第一根竿成功"), LeaveResult.bCommitted);
	TestEqual(TEXT("离开竿位不终止搏斗会话"), FirstSession->Snapshot.Phase, ECatFishingPhase::HookedFight);
	TestFalse(TEXT("离开竿位清除旧会话收线输入"), FirstSession->Snapshot.bReeling);
	TestTrue(TEXT("搏斗离竿进入无人值守松线"), FirstSession->Snapshot.bSlacking);
	TestNull(TEXT("无人值守会话不再把旧玩家登记为当前钓手"), FirstSession->Snapshot.FisherPlayerState.Get());
	TestFalse(TEXT("离开后旧会话不再截获玩家输入"),
		Fishing->TryGetActiveSessionForController(Controller, RoutedSessionId, RoutedSnapshot));

	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("玩家进入第二根竿主位"), SecondRod->AddOperatorFromAuthority(
		PlayerState, SecondRod->GetPresentationState().RodActorRevision, JoinedSlot));
	TestEqual(TEXT("第二根竿进入主位"), JoinedSlot, 0);
	TestFalse(TEXT("第二根空竿尚无会话时允许走抛竿分支"),
		Fishing->TryGetActiveSessionForController(Controller, RoutedSessionId, RoutedSnapshot));

	const FGuid SecondSessionId = FGuid::NewGuid();
	SecondSession->Snapshot.FishingSessionId = SecondSessionId;
	SecondSession->Snapshot.Phase = ECatFishingPhase::Waiting;
	SecondSession->Snapshot.FisherPlayerState = PlayerState;
	SecondSession->Snapshot.RodActor = SecondRod;
	Fishing->Sessions.Add(SecondSessionId, SecondSession);
	TestTrue(TEXT("第二根竿建会话后路由第二会话"),
		Fishing->TryGetActiveSessionForController(Controller, RoutedSessionId, RoutedSnapshot));
	TestEqual(TEXT("当前输入不会回到第一会话"), RoutedSessionId, SecondSessionId);
	TestFalse(TEXT("第一根竿会话仍保持非终态"), FirstSession->IsTerminal());
	// 本测试已验证非终态保持；清掉服务夹具索引，避免 World teardown 的预期中断日志把成功用例标成 warning。
	Fishing->Sessions.Reset();
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
