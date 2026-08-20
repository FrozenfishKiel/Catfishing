#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Fishing/CatFishingSession.h"
#include "GameFramework/PlayerController.h"
#include <limits>
#include "GameFramework/PlayerState.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionFailClosedLifecycleTest,
	"Catfishing.Unit.Fishing.Session.StateTreeGateAndTerminateAreFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionBoundaryFightFailClosedTest,
	"Catfishing.Fishing.Session.BoundaryFightRejectsUninitializedSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionScoopPayloadDriftTest,
	"Catfishing.Unit.Fishing.Session.ScoopRejectsPayloadDriftBeforeInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSessionNearShoreTargetV0Test,
	"Catfishing.Unit.Fishing.Session.NearShoreTargetV0IsNearestWaterPointToFisher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingSessionTest
{
	// 测试身份装配流程：为裸 PlayerController 挂接服务器 UniqueId，FishingSession 只从 PlayerState 读取该私有身份。
	static bool AttachStablePlayerState(UWorld* World, APlayerController* Controller, const FString& StableNetId)
	{
		if (!World || !Controller || StableNetId.IsEmpty())
		{
			return false;
		}
		APlayerState* PlayerState = World->SpawnActor<APlayerState>();
		if (!PlayerState)
		{
			return false;
		}
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId,
			UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
		PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		Controller->SetPlayerState(PlayerState);
		return true;
	}
}

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

	const FCatFishingPhaseResult PhaseResult = Session->EnterPhaseFromStateTree(ECatFishingPhase::HookedFight);
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

// 测试流程：
// 1. 创建未初始化的真实 FishingSession Actor，保持 StateTree、Attempt 与 EncounterSpec 都缺失。
// 2. 调用 StateTree 搏斗推进口，要求在读任何资源前 fail-closed。
// 3. 验证失败不会推进公开 Phase、Revision 或伪造一次提交。
bool FCatFishingSessionBoundaryFightFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Boundary Fight Session 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Boundary Fight 测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("可生成 Boundary Fight 测试 Session"), Session);
	if (!Session)
	{
		return false;
	}

	const FCatFishingSessionSnapshot Initial = Session->GetSnapshot();
	const FCatFishingFightStepResult Result = Session->ApplyFightExchangeFromStateTree(0.016);
	TestEqual(TEXT("未初始化 Session 的 Fight 推进口返回 InvalidPhase"), Result.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("未初始化 Session 不产生任何搏斗终局"), Result.Outcome, ECatFishingFightOutcome::None);
	TestEqual(TEXT("未初始化 Fight 不推进 Revision"), Session->GetSnapshot().Revision, Initial.Revision);
	TestEqual(TEXT("未初始化 Fight 不改变 Phase"), Session->GetSnapshot().Phase, Initial.Phase);
	return !HasAnyErrors();
}

// 测试流程：未初始化 Session 仍可通过身份 gate 进入 Scoop fail-closed；首次拒绝会锁定鱼护、Revision 与命中位置，漂移请求不会读成重放。
bool FCatFishingSessionScoopPayloadDriftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Scoop 载荷漂移测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Scoop 载荷漂移测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	TestNotNull(TEXT("可生成 Scoop 载荷漂移测试 Session"), Session);
	TestNotNull(TEXT("可生成 Scoop 载荷漂移测试 Controller"), Controller);
	if (!Session || !Controller)
	{
		return false;
	}
	TestTrue(TEXT("Scoop 测试 Controller 绑定稳定身份"), CatFishingSessionTest::AttachStablePlayerState(
		World, Controller, TEXT("player:fishing-scoop-drift")));

	FCatScoopCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = Session->GetSnapshot().Revision;
	Command.TargetGuardContainerId = FGuid::NewGuid();
	Command.ScoopWorldLocation = FVector(1.0, 2.0, 3.0);
	const FCatScoopResult First = Session->RequestScoop(Controller, Command);
	TestFalse(TEXT("未初始化 Scoop 首次不提交"), First.Command.bCommitted);
	TestEqual(TEXT("未初始化 Scoop 返回 InvalidPhase"), First.Command.Error, ECatDomainCommandError::InvalidPhase);

	FCatScoopCommand DriftCommand = Command;
	DriftCommand.TargetGuardContainerId = FGuid::NewGuid();
	const FCatScoopResult Drift = Session->RequestScoop(Controller, DriftCommand);
	TestFalse(TEXT("同 RequestId 更换目标鱼护不提交"), Drift.Command.bCommitted);
	TestEqual(TEXT("Scoop 载荷漂移返回 InvalidPayload"), Drift.Command.Error, ECatDomainCommandError::InvalidPayload);

	const FCatScoopResult Replay = Session->RequestScoop(Controller, Command);
	TestFalse(TEXT("原始 Scoop 重放不提交"), Replay.Command.bCommitted);
	TestEqual(TEXT("原始 Scoop 重放返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("Scoop 重放不推进 Session Revision"), Session->GetSnapshot().Revision, Command.Context.ExpectedRevision);
	return !HasAnyErrors();
}

// 测试流程：对 v0 近岸目标近似做纯几何断言。钓手在水域外时目标必须落在 AABB 上且是离他最近的点（距离等于他到包围盒的间隙）；
// 钓手在水域内时目标就是他自己的位置；水域半尺寸未配置或钓手位置含 NaN 时必须拒绝，不得给出一个看起来合法的点。
bool FCatFishingSessionNearShoreTargetV0Test::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterRegionSnapshot Water;
	Water.RegionId = TEXT("UnitLake");
	Water.RegionRevision = 1;
	Water.WorldCenter = FVector(1000.0, 0.0, 0.0);
	Water.HalfExtent = FVector(500.0, 500.0, 400.0);
	const FBox WaterBounds = FBox::BuildAABB(Water.WorldCenter, Water.HalfExtent);

	// 钓手站在水域 X 负方向岸上 60cm 处（X=440，水域 X 范围 500~1500），并带一点横向和高度偏移。
	const FVector ShoreFisher(440.0, 120.0, 88.0);
	FVector Target = FVector::ZeroVector;
	TestTrue(TEXT("岸上钓手能算出近岸目标"), ACatFishingSession::TryComputeNearShoreTargetV0(ShoreFisher, Water, Target));
	TestTrue(TEXT("岸上钓手的目标落在水域 AABB 内或边界上"), WaterBounds.IsInsideOrOn(Target));
	TestEqual(TEXT("岸上钓手的目标贴在他正对的水域边界 X=500"), Target.X, 500.0);
	TestEqual(TEXT("岸上钓手的目标保留他的横向位置"), Target.Y, 120.0);
	TestEqual(TEXT("岸上钓手的目标保留他的高度（仍在水域 Z 范围内）"), Target.Z, 88.0);
	TestEqual(TEXT("岸上钓手到目标的距离等于他到水域的间隙 60cm"), FVector::Dist(ShoreFisher, Target), 60.0);

	const FVector InWaterFisher(900.0, -200.0, 10.0);
	TestTrue(TEXT("水中钓手能算出近岸目标"), ACatFishingSession::TryComputeNearShoreTargetV0(InWaterFisher, Water, Target));
	TestEqual(TEXT("水中钓手的目标就是他自己的位置"), Target, InWaterFisher);

	// 斜角站位：目标应是包围盒角点附近，距离不得超过钓手到角点的直线距离。
	const FVector CornerFisher(400.0, 600.0, 0.0);
	TestTrue(TEXT("斜角钓手能算出近岸目标"), ACatFishingSession::TryComputeNearShoreTargetV0(CornerFisher, Water, Target));
	TestEqual(TEXT("斜角钓手的目标是水域角点 (500,500,0)"), Target, FVector(500.0, 500.0, 0.0));
	TestTrue(TEXT("斜角钓手到目标的距离不超过到角点距离"),
		FVector::Dist(CornerFisher, Target) <= FVector::Dist(CornerFisher, FVector(500.0, 500.0, 0.0)) + KINDA_SMALL_NUMBER);

	FCatWaterRegionSnapshot UnconfiguredWater = Water;
	UnconfiguredWater.HalfExtent = FVector::ZeroVector;
	Target = FVector(1.0, 2.0, 3.0);
	TestFalse(TEXT("未配置半尺寸的水域拒绝计算近岸目标"),
		ACatFishingSession::TryComputeNearShoreTargetV0(ShoreFisher, UnconfiguredWater, Target));
	TestEqual(TEXT("拒绝时不改写输出目标"), Target, FVector(1.0, 2.0, 3.0));

	const FVector NaNFisher(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
	TestFalse(TEXT("含 NaN 的钓手位置拒绝计算近岸目标"),
		ACatFishingSession::TryComputeNearShoreTargetV0(NaNFisher, Water, Target));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS