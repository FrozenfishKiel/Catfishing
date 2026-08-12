#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "GameFramework/Actor.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatContainerReplicationAuthoritySnapshotTest,
	"Catfishing.Unit.Items.ContainerReplication.AuthoritySnapshotReplacesPublishedFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatContainerReplicationTest
{
	// 鱼实例构造流程：用完整公开字段建立可比较 DTO；复制组件测试只观察这些字段是否被组件原样发布。
	static FCatFishInstance MakeFish(const TCHAR* DefinitionId, const double Weight)
	{
		FCatFishInstance Fish;
		Fish.FishInstanceId = FGuid::NewGuid();
		Fish.FishDefinitionId = FName(DefinitionId);
		Fish.OwnerStableNetId = TEXT("AuthorityOwner");
		Fish.SourceFishingSessionId = FGuid::NewGuid();
		Fish.SacrificeContribution = 2;
		Fish.WeightKilograms = Weight;
		return Fish;
	}

	// 快照构造流程：只填容器公开元数据与鱼数组；容量、预留和私有 owner 不属于组件出口。
	static FCatContainerSnapshot MakeSnapshot(const FGuid ContainerId, const int64 Revision, const TArray<FCatFishInstance>& Fish)
	{
		FCatContainerSnapshot Snapshot;
		Snapshot.ContainerId = ContainerId;
		Snapshot.Kind = ECatContainerKind::PersonalGuard;
		Snapshot.Revision = Revision;
		Snapshot.Fish = Fish;
		return Snapshot;
	}
}

// 测试流程：在真实 authority Actor 上挂真实复制组件，先发布两条鱼，再发布只剩第二条鱼的新快照；GetSnapshot 必须反映替换后的公开事实。
bool FCatContainerReplicationAuthoritySnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建容器复制组件测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatContainerReplicationComponent* Component = Host ? NewObject<UCatContainerReplicationComponent>(Host) : nullptr;
		TestNotNull(TEXT("复制组件宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("真实容器复制组件可创建"), Component);
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();

			const FGuid ContainerId = FGuid::NewGuid();
			const FCatFishInstance FirstFish = CatContainerReplicationTest::MakeFish(TEXT("FirstFish"), 1.0);
			const FCatFishInstance SecondFish = CatContainerReplicationTest::MakeFish(TEXT("SecondFish"), 2.0);
			Component->SetSnapshotFromAuthority(
				CatContainerReplicationTest::MakeSnapshot(ContainerId, 2, {FirstFish, SecondFish}));
			const FCatContainerSnapshot& InitialSnapshot = Component->GetSnapshot();
			TestEqual(TEXT("首次发布保留容器 ID"), InitialSnapshot.ContainerId, ContainerId);
			TestEqual(TEXT("首次发布保留 Revision"), InitialSnapshot.Revision, int64{2});
			TestEqual(TEXT("首次发布两条鱼"), InitialSnapshot.Fish.Num(), 2);

			Component->SetSnapshotFromAuthority(CatContainerReplicationTest::MakeSnapshot(ContainerId, 3, {SecondFish}));
			const FCatContainerSnapshot& UpdatedSnapshot = Component->GetSnapshot();
			TestEqual(TEXT("替换发布后 Revision 前进"), UpdatedSnapshot.Revision, int64{3});
			TestEqual(TEXT("替换发布后只保留一条鱼"), UpdatedSnapshot.Fish.Num(), 1);
			if (UpdatedSnapshot.Fish.Num() == 1)
			{
				TestEqual(TEXT("替换发布保留第二条鱼实例"), UpdatedSnapshot.Fish[0].FishInstanceId, SecondFish.FishInstanceId);
				TestEqual(TEXT("替换发布保留第二条鱼定义"), UpdatedSnapshot.Fish[0].FishDefinitionId, SecondFish.FishDefinitionId);
				TestEqual(TEXT("替换发布保留第二条鱼重量"), UpdatedSnapshot.Fish[0].WeightKilograms, SecondFish.WeightKilograms);
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
