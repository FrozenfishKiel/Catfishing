#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/CatWaterRegion.h"

namespace CatWaterQuerySubsystemTest
{
	// 配置流程：创建一个能被查询命中的完整 WaterRegion；位置和区域 ID 由调用方控制，用于覆盖唯一命中与重叠拒绝。
	static ACatWaterRegion* SpawnConfiguredRegion(UWorld* World, const FName RegionId, const FVector& Location)
	{
		ACatWaterRegion* Region = World ? World->SpawnActor<ACatWaterRegion>() : nullptr;
		if (!Region)
		{
			return nullptr;
		}
		Region->SetActorLocation(Location);
		Region->RegionId = RegionId;
		Region->bEnablePrototypeBounds = true;
		Region->HalfExtent = FVector(100.0, 100.0, 50.0);
		Region->GeometryRevision = 1;
		Region->bEnableAggregation = true;
		Region->AggregationBudget = 2.0;
		return Region;
	}

	// 查询流程：构造白天可钓的最小查询快照；调用方只替换位置和 Run Revision 来验证水域子系统的公开拒绝语义。
	static FCatWaterQuery MakeDayQuery(const FVector& WorldLocation, const int64 RunRevision)
	{
		FCatWaterQuery Query;
		Query.WorldLocation = WorldLocation;
		Query.RunRevision = RunRevision;
		Query.RunPhase.Phase = ECatRunPhase::DayActive;
		Query.RunPhase.bFishingAllowed = true;
		return Query;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterQuerySubsystemUniqueRegionTest,
	"Catfishing.Unit.Environment.WaterQuery.RequiresDayRevisionAndUniqueConfiguredRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 World 中通过 WaterQuerySubsystem 查询已配置 WaterRegion；先验证无效 RunRevision，再验证唯一命中，最后用重叠区域证明优先级未裁时拒绝猜测。
bool FCatWaterQuerySubsystemUniqueRegionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 WaterQuery 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 WaterQuery 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatWaterQuerySubsystem* QuerySubsystem = World->GetSubsystem<UCatWaterQuerySubsystem>();
	TestNotNull(TEXT("可取得 WaterQuerySubsystem"), QuerySubsystem);
	if (!QuerySubsystem)
	{
		return false;
	}

	ACatWaterRegion* LakeA = CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("LakeA"), FVector::ZeroVector);
	TestNotNull(TEXT("可创建可聚鱼 WaterRegion"), LakeA);
	if (!LakeA)
	{
		return false;
	}

	FCatWaterQueryResult BadRevisionResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 0));
	TestFalse(TEXT("RunRevision 无效时查询失败"), BadRevisionResult.bSucceeded);
	TestEqual(TEXT("RunRevision 无效返回 RevisionConflict"), BadRevisionResult.Error, ECatWaterQueryError::RevisionConflict);

	FCatWaterQueryResult UniqueResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 3));
	TestTrue(TEXT("唯一配置水域查询成功"), UniqueResult.bSucceeded);
	TestEqual(TEXT("唯一查询返回 None"), UniqueResult.Error, ECatWaterQueryError::None);
	TestEqual(TEXT("唯一查询返回区域 ID"), UniqueResult.Region.RegionId, FName(TEXT("LakeA")));
	TestEqual(TEXT("聚鱼前查询返回 GeometryRevision"), UniqueResult.Region.GeometryRevision, int64(1));
	TestEqual(TEXT("唯一查询保留 RunRevision"), UniqueResult.RunRevision, int64(3));

	FCatAggregationCommand AggregationCommand;
	AggregationCommand.Context.StableNetId = TEXT("PlayerA");
	AggregationCommand.Context.RequestId = FGuid::NewGuid();
	AggregationCommand.RegionId = TEXT("LakeA");
	AggregationCommand.ExpectedAggregationRevision = 1;
	AggregationCommand.Contribution.Fishy = 1.0;
	const FCatAggregationResult AggregationResult = LakeA->ContributeAggregation(AggregationCommand);
	TestTrue(TEXT("查询后聚鱼提交成功"), AggregationResult.Command.bCommitted);

	FCatWaterQueryResult AfterAggregationResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 3));
	TestTrue(TEXT("聚鱼后水域查询成功"), AfterAggregationResult.bSucceeded);
	TestEqual(TEXT("聚鱼后查询保持 GeometryRevision"), AfterAggregationResult.Region.GeometryRevision, int64(1));

	CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("LakeB"), FVector::ZeroVector);
	FCatWaterQueryResult AmbiguousResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 4));
	TestFalse(TEXT("两个水域重叠时查询失败"), AmbiguousResult.bSucceeded);
	TestEqual(TEXT("两个水域重叠返回 AmbiguousRegion"), AmbiguousResult.Error, ECatWaterQueryError::AmbiguousRegion);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
