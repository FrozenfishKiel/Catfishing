#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatWaterRegion.h"

namespace CatWaterRegionTest
{
	// 配置流程：把测试水域设置成完整 prototype AABB，并按参数开放聚鱼预算；调用方仍通过公开接口验证运行结果。
	static void ConfigureRegion(ACatWaterRegion* Region, const FName RegionId, const FVector& Location,
		const int64 Revision, const bool bEnableAggregation, const double AggregationBudget)
	{
		if (!Region)
		{
			return;
		}
		Region->SetActorLocation(Location);
		Region->RegionId = RegionId;
		Region->bEnablePrototypeBounds = true;
		Region->HalfExtent = FVector(100.0, 100.0, 50.0);
		Region->GeometryRevision = Revision;
		Region->bEnableAggregation = bEnableAggregation;
		Region->AggregationBudget = AggregationBudget;
	}

	// 命令流程：创建聚鱼写口的最小正式命令，RequestId/身份/Revision/贡献由调用方决定以覆盖成功、重放和并发拒绝。
	static FCatAggregationCommand MakeAggregationCommand(const FString& StableNetId, const FGuid RequestId,
		const int64 ExpectedAggregationRevision, const double Fishy)
	{
		FCatAggregationCommand Command;
		Command.Context.StableNetId = StableNetId;
		Command.Context.RequestId = RequestId;
		Command.ExpectedAggregationRevision = ExpectedAggregationRevision;
		Command.RegionId = TEXT("LakeA");
		Command.Contribution.Fishy = Fishy;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterRegionConfigurationTest,
	"Catfishing.Unit.Environment.WaterRegion.ConfigurationBoundsAndSnapshotAreExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterRegionAggregationTransactionTest,
	"Catfishing.Unit.Environment.WaterRegion.AggregationReplayRevisionAndBudgetContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 Game World 生成 WaterRegion，从默认无效到完整 AABB 逐步验证 Contains 与 Snapshot；水域必须由关卡显式配置才可命中。
bool FCatWaterRegionConfigurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 WaterRegion 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 WaterRegion 测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
	TestNotNull(TEXT("可生成 WaterRegion"), Region);
	if (!Region)
	{
		return false;
	}

	TestFalse(TEXT("默认 WaterRegion 不可运行"), Region->IsRuntimeConfigured());
	TestFalse(TEXT("默认 WaterRegion 不包含任何点"), Region->ContainsWorldPoint(FVector::ZeroVector));

	CatWaterRegionTest::ConfigureRegion(Region, TEXT("LakeA"), FVector::ZeroVector, 7, false, 0.0);
	TestTrue(TEXT("完整 prototype 配置可运行"), Region->IsRuntimeConfigured());
	TestTrue(TEXT("配置后 AABB 包含中心点"), Region->ContainsWorldPoint(FVector::ZeroVector));
	TestFalse(TEXT("配置后 AABB 拒绝远点"), Region->ContainsWorldPoint(FVector(1000.0, 0.0, 0.0)));

	const FCatWaterRegionSnapshot Snapshot = Region->MakeSnapshot();
	TestEqual(TEXT("快照复制稳定区域 ID"), Snapshot.RegionId, FName(TEXT("LakeA")));
	TestEqual(TEXT("快照复制几何 Revision"), Snapshot.GeometryRevision, int64(7));
	TestEqual(TEXT("快照初始聚鱼 Revision"), Snapshot.AggregationRevision, int64(1));
	TestEqual(TEXT("快照复制 AABB 半尺寸"), Snapshot.HalfExtent, FVector(100.0, 100.0, 50.0));
	return !HasAnyErrors();
}

// 测试流程：同一 WaterRegion 先成功提交聚鱼，再重放、用陈旧 Revision 写入和超预算写入；ChumPool 与 RegionRevision 只能在首次成功时推进。
bool FCatWaterRegionAggregationTransactionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建聚鱼测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建聚鱼测试 World"), World);
	if (!World)
	{
		return false;
	}

	ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
	TestNotNull(TEXT("可生成聚鱼 WaterRegion"), Region);
	if (!Region)
	{
		return false;
	}
	CatWaterRegionTest::ConfigureRegion(Region, TEXT("LakeA"), FVector::ZeroVector, 7, true, 2.0);

	const FGuid RequestId = FGuid::NewGuid();
	FCatAggregationResult FirstResult = Region->ContributeAggregation(
		CatWaterRegionTest::MakeAggregationCommand(TEXT("PlayerA"), RequestId, 1, 1.0));
	TestTrue(TEXT("首次聚鱼提交成功"), FirstResult.Command.bCommitted);
	TestEqual(TEXT("首次聚鱼返回 None"), FirstResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("首次聚鱼推进 AggregationRevision"), FirstResult.AggregationRevision, int64(2));
	TestEqual(TEXT("首次聚鱼兼容命令 Revision"), FirstResult.Command.Revision, int64(2));
	TestEqual(TEXT("首次聚鱼增加 ChumPool"), FirstResult.ChumPool.Fishy, 1.0);
	TestEqual(TEXT("首次聚鱼不改变 GeometryRevision"), Region->MakeSnapshot().GeometryRevision, int64(7));

	FCatAggregationResult ReplayResult = Region->ContributeAggregation(
		CatWaterRegionTest::MakeAggregationCommand(TEXT("PlayerA"), RequestId, 1, 1.0));
	TestFalse(TEXT("聚鱼重放不再次提交"), ReplayResult.Command.bCommitted);
	TestEqual(TEXT("聚鱼重放返回 AlreadyResolved"), ReplayResult.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("聚鱼重放不二次加池"), ReplayResult.ChumPool.Fishy, 1.0);
	TestEqual(TEXT("聚鱼重放保留 AggregationRevision"), ReplayResult.AggregationRevision, int64(2));

	FCatAggregationResult StaleResult = Region->ContributeAggregation(
		CatWaterRegionTest::MakeAggregationCommand(TEXT("PlayerA"), FGuid::NewGuid(), 1, 1.0));
	TestFalse(TEXT("陈旧 Revision 聚鱼不提交"), StaleResult.Command.bCommitted);
	TestEqual(TEXT("陈旧 Revision 聚鱼返回 RevisionConflict"), StaleResult.Command.Error, ECatDomainCommandError::RevisionConflict);
	TestEqual(TEXT("陈旧 Revision 聚鱼不改变池"), StaleResult.ChumPool.Fishy, 1.0);

	FCatAggregationResult CapacityResult = Region->ContributeAggregation(
		CatWaterRegionTest::MakeAggregationCommand(TEXT("PlayerA"), FGuid::NewGuid(), 2, 2.0));
	TestFalse(TEXT("超预算聚鱼不提交"), CapacityResult.Command.bCommitted);
	TestEqual(TEXT("超预算聚鱼返回 CapacityExceeded"), CapacityResult.Command.Error, ECatDomainCommandError::CapacityExceeded);
	TestEqual(TEXT("超预算聚鱼不改变池"), CapacityResult.ChumPool.Fishy, 1.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
