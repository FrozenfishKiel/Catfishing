#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatWaterGeometry.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Framework/Game/CatGameplayTypes.h"

namespace CatGameplayTypesTest
{
	static FCatWaterGeometryCache BuildLakeCache()
	{
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = TEXT("LakeA");
		Input.PlaneToWorld = FTransform::Identity;
		Input.WaterPointVerticalToleranceCm = 10;
		Input.BankHeightToleranceCm = 20;
		Input.BoundaryToleranceCm = 1;
		Input.MaxLandingCorrectionCm = 10;
		Input.MinimumWaterInsetCm = 2;
		FCatWaterPolygonBuildInput& Boundary = Input.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("Outer");
		Boundary.Vertices = {{-100,-100}, {100,-100}, {100,100}, {-100,100}};
		return FCatWaterGeometry::Build(Input).Cache;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGameModeRunCommandFailClosedTest,
	"Catfishing.Unit.Framework.GameMode.RunCommandsFailClosedBeforeRuntimeStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPlayerChumInvalidPayloadReportsCurrentAggregationRevisionTest,
	"Catfishing.Unit.Framework.PlayerChum.InvalidPayloadReportsCurrentAggregationRevision",
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

bool FCatPlayerChumInvalidPayloadReportsCurrentAggregationRevisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 PlayerChum 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 PlayerChum 测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
	TestNotNull(TEXT("可创建 WaterRegion"), Region);
	if (!Region)
	{
		return false;
	}
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, CatGameplayTypesTest::BuildLakeCache());
	Region->bEnableAggregation = true;
	Region->AggregationBudget = 2.0;

	FCatAggregationCommand Contribution;
	Contribution.Context.StableNetId = TEXT("PlayerA");
	Contribution.Context.RequestId = FGuid::NewGuid();
	Contribution.RegionId = Region->RegionId;
	Contribution.ExpectedAggregationRevision = 1;
	Contribution.Contribution.Fishy = 1.0;
	const FCatAggregationResult ContributionResult = Region->ContributeAggregation(Contribution);
	TestTrue(TEXT("合法聚鱼推进 AggregationRevision"), ContributionResult.Command.bCommitted);
	TestEqual(TEXT("聚鱼后当前 AggregationRevision"), Region->MakeSnapshot().AggregationRevision, int64(2));

	const FGuid RequestId = FGuid::NewGuid();
	const FCatAggregationResult InvalidPayload = ACatfishingPlayerController::MakeInvalidPlayerChumResult(RequestId, Region);
	TestFalse(TEXT("非空水域的 InvalidPayload 不提交"), InvalidPayload.Command.bCommitted);
	TestEqual(TEXT("非空水域的 InvalidPayload 状态"), InvalidPayload.Command.Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("非空水域保留 RequestId"), InvalidPayload.Command.RequestId, RequestId);
	TestEqual(TEXT("非空水域返回当前 AggregationRevision"), InvalidPayload.AggregationRevision, int64(2));
	TestEqual(TEXT("非空水域兼容命令 Revision"), InvalidPayload.Command.Revision, int64(2));

	const FCatAggregationResult NullRegionInvalidPayload = ACatfishingPlayerController::MakeInvalidPlayerChumResult(RequestId, nullptr);
	TestEqual(TEXT("空水域的 AggregationRevision 为零"), NullRegionInvalidPayload.AggregationRevision, int64(0));
	TestEqual(TEXT("空水域的命令 Revision 为零"), NullRegionInvalidPayload.Command.Revision, int64(0));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
