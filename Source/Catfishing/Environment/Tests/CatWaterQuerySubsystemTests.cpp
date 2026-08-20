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
		Region->RegionRevision = 1;
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

// 测试流程：在真实 World 中通过 WaterQuerySubsystem 查询已配置 WaterRegion；先验证无效 RunRevision，再验证唯一命中，
// 最后用重叠区域证明优先级未裁时拒绝猜测。
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

	CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("LakeA"), FVector::ZeroVector);

	FCatWaterQueryResult BadRevisionResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 0));
	TestFalse(TEXT("RunRevision 无效时查询失败"), BadRevisionResult.bSucceeded);
	TestEqual(TEXT("RunRevision 无效返回 RevisionConflict"), BadRevisionResult.Error, ECatWaterQueryError::RevisionConflict);

	FCatWaterQueryResult UniqueResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 3));
	TestTrue(TEXT("唯一配置水域查询成功"), UniqueResult.bSucceeded);
	TestEqual(TEXT("唯一查询返回 None"), UniqueResult.Error, ECatWaterQueryError::None);
	TestEqual(TEXT("唯一查询返回区域 ID"), UniqueResult.Region.RegionId, FName(TEXT("LakeA")));
	TestEqual(TEXT("唯一查询保留 RunRevision"), UniqueResult.RunRevision, int64(3));

	CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("LakeB"), FVector::ZeroVector);
	FCatWaterQueryResult AmbiguousResult = QuerySubsystem->QueryWaterRegion(
		CatWaterQuerySubsystemTest::MakeDayQuery(FVector::ZeroVector, 4));
	TestFalse(TEXT("两个水域重叠时查询失败"), AmbiguousResult.bSucceeded);
	TestEqual(TEXT("两个水域重叠返回 AmbiguousRegion"), AmbiguousResult.Error, ECatWaterQueryError::AmbiguousRegion);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterQuerySubsystemRegionCenterTest,
	"Catfishing.Unit.Environment.WaterQuery.RegionCenterLookupRequiresOneConfiguredRegionPerId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：验证按 RegionId 反查水域中心这条路——自然事件靠它拿到落点，所以空 ID、找不到和重名都必须失败并清空输出，
// 只有关卡里恰好有一片同 ID 水域时才给出中心。这里刻意造一次重名，确认它不会退化成"随便挑第一片"。
bool FCatWaterQuerySubsystemRegionCenterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建区域中心查找测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatWaterQuerySubsystem* QuerySubsystem = World ? World->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	TestNotNull(TEXT("可取得 WaterQuery 子系统"), QuerySubsystem);
	if (!World || !QuerySubsystem)
	{
		return false;
	}

	FVector Center(9.0, 9.0, 9.0);
	TestFalse(TEXT("空 RegionId 查不到中心"), QuerySubsystem->TryGetRegionCenter(NAME_None, Center));
	TestEqual(TEXT("空 RegionId 时输出清零"), Center, FVector::ZeroVector);
	TestFalse(TEXT("关卡里没有该 RegionId 时查不到中心"),
		QuerySubsystem->TryGetRegionCenter(TEXT("ForestLake"), Center));

	CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("ForestLake"), FVector(-4000.0, 200.0, 0.0));
	TestTrue(TEXT("唯一同名水域可查到中心"), QuerySubsystem->TryGetRegionCenter(TEXT("ForestLake"), Center));
	TestEqual(TEXT("中心与水域几何一致"), Center, FVector(-4000.0, 200.0, 0.0));

	CatWaterQuerySubsystemTest::SpawnConfiguredRegion(World, TEXT("ForestLake"), FVector(1000.0, 0.0, 0.0));
	TestFalse(TEXT("同一个 RegionId 出现两次时拒绝猜测"),
		QuerySubsystem->TryGetRegionCenter(TEXT("ForestLake"), Center));
	TestEqual(TEXT("重名拒绝时输出清零"), Center, FVector::ZeroVector);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
