#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Environment/CatWaterRegion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterRegionConfigurationTest,
	"Catfishing.Unit.Environment.WaterRegion.ConfigurationBoundsAndSnapshotAreExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatWaterRegionWetsCharactersInsideTest,
	"Catfishing.Unit.Environment.WaterRegion.ServerTickWetsOnlyCharactersStandingInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：在真实 Game World 生成 WaterRegion，从默认无效到完整 AABB 逐步验证 Contains 与 Snapshot；
// 水域必须由关卡显式配置才可命中，且快照只描述几何——窝料事实由 UCatChumSpotSubsystem 独立持有，这里不该出现。
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

	Region->SetActorLocation(FVector::ZeroVector);
	Region->RegionId = TEXT("LakeA");
	Region->bEnablePrototypeBounds = true;
	Region->HalfExtent = FVector(100.0, 100.0, 50.0);
	Region->RegionRevision = 7;
	TestTrue(TEXT("完整 prototype 配置可运行"), Region->IsRuntimeConfigured());
	TestTrue(TEXT("配置后 AABB 包含中心点"), Region->ContainsWorldPoint(FVector::ZeroVector));
	TestFalse(TEXT("配置后 AABB 拒绝远点"), Region->ContainsWorldPoint(FVector(1000.0, 0.0, 0.0)));

	const FCatWaterRegionSnapshot Snapshot = Region->MakeSnapshot();
	TestEqual(TEXT("快照复制稳定区域 ID"), Snapshot.RegionId, FName(TEXT("LakeA")));
	TestEqual(TEXT("快照复制区域 Revision"), Snapshot.RegionRevision, int64(7));
	TestEqual(TEXT("快照复制 AABB 半尺寸"), Snapshot.HalfExtent, FVector(100.0, 100.0, 50.0));
	TestEqual(TEXT("快照复制世界中心"), Snapshot.WorldCenter, FVector::ZeroVector);
	return !HasAnyErrors();
}

// 测试流程：在 Game World 里放一片已配置的水域和两只猫，一只站在 AABB 里、一只在外面；让水域 BeginPlay 注册 Tick 后推进 World。
// 水里的猫必须被置成 Wet 且 Revision 只推进一次（再推进几个节拍也不能再涨），岸上的猫必须保持干燥且 Revision 为 0；
// 这证明落水置湿不依赖天气、只写真实变化，并且不会把不在水里的人顺手弄湿。
bool FCatWaterRegionWetsCharactersInsideTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建落水置湿测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatWaterRegion* Region = World ? World->SpawnActor<ACatWaterRegion>() : nullptr;
	ACatCharacter* Swimmer = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatCharacter* Bystander = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	TestNotNull(TEXT("落水测试 World 可用"), World);
	TestNotNull(TEXT("可生成 WaterRegion"), Region);
	TestNotNull(TEXT("可生成水里的猫"), Swimmer);
	TestNotNull(TEXT("可生成岸上的猫"), Bystander);
	if (!World || !Region || !Swimmer || !Bystander || !Swimmer->GetConditionComponent() || !Bystander->GetConditionComponent())
	{
		return false;
	}

	Region->RegionId = TEXT("LakeWet");
	Region->bEnablePrototypeBounds = true;
	Region->HalfExtent = FVector(200.0, 200.0, 200.0);
	Region->RegionRevision = 1;
	Swimmer->SetActorLocation(FVector(50.0, 0.0, 0.0));
	Bystander->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	// Actor 的 Tick 函数在 BeginPlay 里才注册到关卡；测试世界没有整体 BeginPlay，所以只对水域本身派发一次。
	Region->DispatchBeginPlay();

	TestTrue(TEXT("推进第一个节拍"), WorldWrapper.TickTestWorld(ACatWaterRegion::WetCheckIntervalSeconds));
	TestTrue(TEXT("站在水里的猫被置成 Wet"), Swimmer->GetConditionComponent()->GetSnapshot().bWet);
	TestEqual(TEXT("落水只推进一次 Revision"), Swimmer->GetConditionComponent()->GetSnapshot().Revision, int64(1));
	TestFalse(TEXT("岸上的猫保持干燥"), Bystander->GetConditionComponent()->GetSnapshot().bWet);
	TestEqual(TEXT("岸上的猫 Revision 不动"), Bystander->GetConditionComponent()->GetSnapshot().Revision, int64(0));

	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		WorldWrapper.TickTestWorld(ACatWaterRegion::WetCheckIntervalSeconds);
	}
	TestTrue(TEXT("一直泡在水里仍然是 Wet"), Swimmer->GetConditionComponent()->GetSnapshot().bWet);
	TestEqual(TEXT("持续泡水不会每个节拍推高 Revision"), Swimmer->GetConditionComponent()->GetSnapshot().Revision, int64(1));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
