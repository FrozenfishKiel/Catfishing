#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
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
	FCatFishingServiceRodOperationsPreserveMovementTest,
	"Catfishing.Unit.Fishing.Service.RodOperationsPreserveCharacterMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingHeldFacingFollowsControlRotationTest,
	"Catfishing.Unit.Fishing.Service.HeldRodFacingUsesPhysicalIntentAndPreservesMomentum",
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

// 手持鱼竿不再写 MOVE_None：局不可用清理、角色中断、重新拾取和 Actor 销毁都只能改鱼竿操作身份，不能改 CharacterMovement。
bool FCatFishingServiceRodOperationsPreserveMovementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Fishing 局不可用清理测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
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
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Rod"), TEXT("Skin"), PlayerState, PlayerState, true, false));
	TestTrue(TEXT("部署鱼竿登记成功"), Fishing->RegisterDeployedRod(PlayerState, Rod));
	TestTrue(TEXT("窗口清理契约明确发布本人操作位"),Rod->SetPrimaryOperatorFromAuthority(PlayerState,Rod->GetPresentationState().RodActorRevision));
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	TestNotNull(TEXT("角色移动组件存在"), Movement);
	if (!Movement)
	{
		return false;
	}

	const EMovementMode InitialMovementMode=Movement->MovementMode.GetValue();
	TestEqual(TEXT("夹具从可移动状态开始"), Movement->MovementMode.GetValue(), InitialMovementMode);
	Fishing->SuspendFishingAndReleaseOperators();
	TestEqual(TEXT("局不可用清理清空全部操作槽"), Rod->GetOperatorCount(), 0);
	TestEqual(TEXT("局不可用清理不改角色移动模式"), Movement->MovementMode.GetValue(), InitialMovementMode);
	TestEqual(TEXT("局不可用清理让鱼竿落地"), Rod->GetPresentationState().PoseMode,
		ECatFishingRodPoseMode::Grounded);
	TestTrue(TEXT("局不可用清理不收走已部署鱼竿"), Rod->GetPresentationState().bDeployed);

	TestTrue(TEXT("局重新开放后可占据原鱼竿"), Rod->SetPrimaryOperatorFromAuthority(PlayerState, Rod->GetPresentationState().RodActorRevision));
	TestTrue(TEXT("主位投影一致"),Rod->IsPrimaryOperator(PlayerState));
	TestEqual(TEXT("重新拾取切回手持姿态"), Rod->GetPresentationState().PoseMode,
		ECatFishingRodPoseMode::Held);
	Fishing->ReleaseFishingOperatorForCharacter(Character);
	TestEqual(TEXT("Character 中断清空自身操作槽"), Rod->GetOperatorCount(), 0);
	TestEqual(TEXT("Character 中断不改角色移动模式"), Movement->MovementMode.GetValue(), InitialMovementMode);

	TestTrue(TEXT("异常销毁前可再次占据原鱼竿"), Rod->SetPrimaryOperatorFromAuthority(PlayerState, Rod->GetPresentationState().RodActorRevision));
	TestTrue(TEXT("销毁鱼竿触发 EndPlay 清理"), Rod->Destroy());
	World->Tick(ELevelTick::LEVELTICK_All, 0.01f);
	TestEqual(TEXT("鱼竿异常销毁不改角色移动模式"), Movement->MovementMode.GetValue(), InitialMovementMode);
	TestNull(TEXT("鱼竿异常销毁移除部署登记"), Fishing->FindDeployedRod(PlayerState));

	Fishing->ReleaseFishingOperatorForCharacter(Character);
	TestEqual(TEXT("鱼竿登记失效后的中断仍不改移动模式"), Movement->MovementMode.GetValue(), InitialMovementMode);
	return !HasAnyErrors();
}

// Input-consumer contract: the physical body receives current view intent; rod observation never writes a second body pose.
bool FCatFishingHeldFacingFollowsControlRotationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game) || !Scene.BeginPlayInTestWorld()) return false;
	Scene.ForwardErrorMessages(this);
	UWorld* World=Scene.GetTestWorld();
	auto* Fishing=World->GetSubsystem<UCatFishingService>();
	auto* Controller=World->SpawnActor<ACatfishingPlayerController>();
	auto* Player=World->SpawnActor<ACatfishingPlayerState>();
	auto* Character=World->SpawnActor<ACatCharacter>();
	auto* Rod=World->SpawnActor<ACatFishingRodActor>();
	if (!Fishing || !Controller || !Player || !Character || !Rod) return false;
	Controller->PlayerState=Player; Character->SetPlayerState(Player); Controller->Possess(Character);
	auto* Body=Character->GetPhysicalBodyComponent();
	if (!Body || !Body->GetBody()) return false;
	const auto MovementMode=Character->GetCharacterMovement()->MovementMode.GetValue();
	const FTransform BodyBefore=Body->GetBody()->GetComponentTransform();
	Controller->SetControlRotation(FRotator(0,95,0));
	Controller->UpdateRotation(1.0f/60.0f);
	TestEqual(TEXT("free view submits physical motor intent"),Body->GetViewIntent().Yaw,95.0,1.e-6);
	TestTrue(TEXT("input observation cannot teleport body orientation"),Body->GetBody()->GetComponentTransform().Equals(BodyBefore,1.e-8));
	if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(),FGuid::NewGuid(),TEXT("FacingRod"),NAME_None,Player,Player,true,false)
		|| !Fishing->RegisterDeployedRod(Player,Rod)
		|| !Rod->SetPrimaryOperatorFromAuthority(Player,Rod->GetPresentationState().RodActorRevision)) return false;
	Character->Jump(); Controller->StartJump();
	TestFalse(TEXT("primary held rod blocks jump input"),Character->bPressedJump);
	Rod->SetFightConstraintObservationFromAuthority(FVector::ForwardVector, 1, 0, true, 100, 50);
	Controller->SetControlRotation(FRotator(0,120,0));
	Controller->UpdateRotation(1.0f/60.0f);
	TestTrue(TEXT("fight facing consumes actual rod direction"),Body->GetViewIntent().Equals(UCatFishingCameraComponent::ResolveFacingRotation(Controller),1.e-6));
	FCatFishingRodControlObservation Observation;
	TestTrue(TEXT("physical rod supplies read-only control observation"),Rod->GetControlObservationFromAuthority(Observation));
	const FTransform RodBefore=Rod->GetPhysicalRodBody()->GetComponentTransform();
	Rod->RefreshHeldTransformFromAuthority(1.0);
	TestTrue(TEXT("held refresh does not perform a second physics integration"),Rod->GetPhysicalRodBody()->GetComponentTransform().Equals(RodBefore,1.e-8));
	Rod->ClearFightConstraintAndLoadFromAuthority();
	TestTrue(TEXT("fight cleanup leaves the unattended rod fixed"),Rod->GetPhysicalRodBody()->GetComponentTransform().Equals(RodBefore,1.e-8)
		&& Rod->GetPhysicalRodComponent()->GetAngularVelocityRadiansPerSecond().IsZero());
	TestTrue(TEXT("consumer fixture publishes empty membership"),Rod->SetPrimaryOperatorFromAuthority(nullptr,Rod->GetPresentationState().RodActorRevision));
	Controller->SetControlRotation(FRotator(0,-40,0)); Controller->UpdateRotation(1.0f/60.0f);
	TestEqual(TEXT("leave resumes current free physical view intent"),Body->GetViewIntent().Yaw,-40.0,1.e-6);
	TestEqual(TEXT("rod lifecycle never enables a second CMC mover"),Character->GetCharacterMovement()->MovementMode.GetValue(),MovementMode);
	return !HasAnyErrors();
}

// 多竿路由契约：角色离开第一根竿只释放竿位，原会话保持；进入第二根竿后，输入查询只能返回第二根竿的会话。
bool FCatFishingServiceRodBoundSessionRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建多竿会话路由测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	if (!WorldWrapper.BeginPlayInTestWorld()) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	ACatfishingPlayerState* PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
	APlayerState* ReplacementFisher = World ? World->SpawnActor<APlayerState>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishingRodActor* FirstRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingRodActor* SecondRod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingSession* FirstSession = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	ACatFishingSession* SecondSession = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	if (!TestNotNull(TEXT("FishingService 可用"), Fishing)
		|| !TestNotNull(TEXT("Controller 可用"), Controller)
		|| !TestNotNull(TEXT("PlayerState 可用"), PlayerState)
		|| !TestNotNull(TEXT("接力玩家可用"), ReplacementFisher)
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
	const FGuid FirstRodItemInstanceId = FGuid::NewGuid();
	const FGuid SecondRodItemInstanceId = FGuid::NewGuid();
	TestTrue(TEXT("第一根竿以玩家占据主位初始化"), FirstRod->InitializeAuthoritativeIdentity(
		FirstRodId, FirstRodItemInstanceId, TEXT("RodA"), TEXT("SkinA"), PlayerState, PlayerState, true, false));
	TestTrue(TEXT("第二根竿以空主位初始化"), SecondRod->InitializeAuthoritativeIdentity(
		SecondRodId, SecondRodItemInstanceId, TEXT("RodB"), TEXT("SkinB"), PlayerState, nullptr, true, false));
	TestTrue(TEXT("登记第一根竿"), Fishing->RegisterDeployedRod(PlayerState, FirstRod));
	TestTrue(TEXT("登记第二根竿"), Fishing->RegisterDeployedRod(PlayerState, SecondRod));
	TestTrue(TEXT("首竿的主控投影明确属于所有者"),FirstRod->SetPrimaryOperatorFromAuthority(PlayerState,FirstRod->GetPresentationState().RodActorRevision));
	const FTransform ObservedRodPose=FirstRod->GetPhysicalRodBody()->GetComponentTransform();
	Character->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FVector(120,0,20)),TEXT("RoutingOnlyFixture"));
	TestTrue(TEXT("read-only rod refresh succeeds"),FirstRod->RefreshHeldTransformFromAuthority(.05));
	TestTrue(TEXT("metadata-only routing fixture cannot attach or teleport physical rod"),FirstRod->GetPhysicalRodBody()->GetComponentTransform().Equals(ObservedRodPose,1.e-8));
	TestTrue(TEXT("formal character routes forces to upright CMC"),Character->GetPhysicalBodyComponent()->UsesCharacterMovement());
	TestFalse(TEXT("formal body cannot freely tumble"),Character->GetPhysicalBodyComponent()->GetBody()->IsSimulatingPhysics());

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
	// This routing-only session has no fight runner; its intentional unattended publication reports that absent consumer.
	AddExpectedErrorPlain(FString::Printf(TEXT("Event=fishing_operator_suspended SessionId=%s Phase=ECatFishingPhase::HookedFight Mode=UnattendedSlack RunnerTransition=false"),
		*FirstSessionId.ToString(EGuidFormats::DigitsWithHyphens)),EAutomationExpectedErrorFlags::Contains,1);
	const FCatFishingCommandResult LeaveResult = Fishing->LeaveRod(Controller, Leave);
	TestTrue(TEXT("离开第一根竿成功"), LeaveResult.bCommitted);
	TestEqual(TEXT("离开竿位不终止搏斗会话"), FirstSession->Snapshot.Phase, ECatFishingPhase::HookedFight);
	TestFalse(TEXT("离开竿位清除旧会话收线输入"), FirstSession->Snapshot.bReeling);
	TestTrue(TEXT("搏斗离竿进入无人值守松线"), FirstSession->Snapshot.bSlacking);
	TestNull(TEXT("无人值守会话不再把旧玩家登记为当前钓手"), FirstSession->Snapshot.FisherPlayerState.Get());
	TestEqual(TEXT("主操作手离开后鱼竿占位数组为空"), FirstRod->GetOperatorCount(), 0);
	TestEqual(TEXT("主操作手离开后同一鱼竿切到地面姿态"),
		FirstRod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Grounded);
	TestFalse(TEXT("旁人不能从物理接触获得本人鱼竿操作权"), FirstRod->SetPrimaryOperatorFromAuthority(ReplacementFisher,FirstRod->GetPresentationState().RodActorRevision));
	TestFalse(TEXT("旁人不会自动接任空出的操作位"),FirstRod->IsPrimaryOperator(ReplacementFisher));
	TestFalse(TEXT("离开后旧会话不再截获玩家输入"),
		Fishing->TryGetActiveSessionForController(Controller, RoutedSessionId, RoutedSnapshot));

	TestTrue(TEXT("玩家进入第二根竿主位"), SecondRod->SetPrimaryOperatorFromAuthority(PlayerState,SecondRod->GetPresentationState().RodActorRevision));
	TestTrue(TEXT("第二根竿主位投影一致"),SecondRod->IsPrimaryOperator(PlayerState));
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
	// The two live sessions publish terminal state synchronously. Consumers may query/compact the service in that callback.
	int32 TerminalCallbacks = 0;
	for (ACatFishingSession* Session : {FirstSession, SecondSession})
		Session->OnSnapshotChanged.AddLambda([this, Fishing, Session, &TerminalCallbacks]()
		{
			if (!Session->IsTerminal()) return;
			++TerminalCallbacks;
			TestNull(TEXT("terminal observer can query the registry while batch shutdown is publishing"),
				Fishing->FindSession(Session->GetSnapshot().FishingSessionId));
		});
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 2);
	Fishing->CloseCommandsAndTerminateAll();
	TestEqual(TEXT("both sessions publish exactly one terminal callback despite reentrant registry compaction"), TerminalCallbacks, 2);
	TestTrue(TEXT("both sessions reach the existing terminal state"), FirstSession->IsTerminal() && SecondSession->IsTerminal());
	TestEqual(TEXT("batch shutdown empties the active registry"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
	TestEqual(TEXT("batch shutdown releases the last primary control"), SecondRod->GetOperatorCount(), 0);
	FirstSession->OnSnapshotChanged.Clear(); SecondSession->OnSnapshotChanged.Clear();
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
