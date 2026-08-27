#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Collection/CatRunImprintService.h"
#include "GameFramework/Actor.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCollectionCommitCaptureSliceTest,
	"Catfishing.Slice.ItemsCollection.CommitCaptureRecordsSingleFishGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatItemsCollectionSliceTest
{
	/** 已注册地面鱼护的切片上下文；它把真实 Actor 宿主、复制组件、容器 ID 和服务器身份绑成一次测试输入。 */
	struct FFishGuardFixture
	{
		/** authority Actor 宿主；复制组件依附它取得真实 Owner 生命周期。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** Items 服务发布公开快照的正式组件；切片测试不直接写它的数组。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本次测试地面鱼护的稳定 ID；Items 命令通过它定位容器聚合。 */
		FGuid ContainerId;

		/** 服务器私有玩家身份；Items owner 校验与 Collection grant 接收者使用同一个值。 */
		FString StableNetId;
	};

	// 注册流程：创建真实 Actor 与正式复制组件，再通过 Items public API 注册地面鱼护；失败时返回空组件并让调用方断言。
	static FFishGuardFixture RegisterFishGuard(UWorld* World, UCatItemsService* ItemsService)
	{
		FFishGuardFixture Fixture;
		Fixture.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Fixture.Component = Fixture.Owner ? NewObject<UCatContainerReplicationComponent>(Fixture.Owner) : nullptr;
		Fixture.ContainerId = FGuid::NewGuid();
		Fixture.StableNetId = TEXT("SlicePlayerA");
		if (Fixture.Owner && Fixture.Component && ItemsService)
		{
			Fixture.Owner->AddInstanceComponent(Fixture.Component);
			Fixture.Component->RegisterComponent();
			ItemsService->RegisterContainer(Fixture.Component, Fixture.ContainerId,
				ECatContainerKind::FishGuard, Fixture.StableNetId, 2);
		}
		return Fixture;
	}

	// 捕获命令流程：构造一条合法实物鱼提交；ExpectedRevision 固定为注册后的初始 Revision=1，保证切片从真实 Items 写口进入。
	static FCatCaptureCommitCommand MakeCaptureCommand(const FFishGuardFixture& Guard)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = 1;
		Command.Context.StableNetId = Guard.StableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FGuid::NewGuid();
		Command.FishDefinitionId = TEXT("SliceFish");
		Command.TargetContainerId = Guard.ContainerId;
		Command.WeightKilograms = 3.25;
		Command.SacrificeContribution = 2;
		return Command;
	}
}

// 测试流程：用 Items 提交一条真实实物鱼，再把同一 committed 捕获事实交给 Collection 归档；重放必须返回同一 GrantId，pending ACK 数量不能增加。
bool FCatItemsCollectionCommitCaptureSliceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建 Items→Collection 切片测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("切片 World 可创建"), World);
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	if (!World || !ItemsService || !ImprintService)
	{
		return false;
	}

	const CatItemsCollectionSliceTest::FFishGuardFixture Guard =
		CatItemsCollectionSliceTest::RegisterFishGuard(World, ItemsService);
	TestNotNull(TEXT("地面鱼护组件已创建"), Guard.Component.Get());

	const FCatCaptureCommitCommand CaptureCommand = CatItemsCollectionSliceTest::MakeCaptureCommand(Guard);
	const FCatCaptureCommitResult CaptureResult = ItemsService->CommitCapture(CaptureCommand);
	TestTrue(TEXT("Items 捕获提交成功"), CaptureResult.Command.bCommitted);
	TestEqual(TEXT("Items 捕获返回 None"), CaptureResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("Committed 保留 Capture RequestId"), CaptureResult.Committed.CaptureRequestId,
		CaptureCommand.Context.RequestId);
	TestEqual(TEXT("Committed 保留 FishDefinition"), CaptureResult.Committed.FishInstance.FishDefinitionId,
		CaptureCommand.FishDefinitionId);

	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = TEXT("SliceRegion");
	Condition.TimeOfDayId = TEXT("Day");
	Condition.WeatherId = TEXT("Clear");

	TestTrue(TEXT("Collection 当前允许记录 committed capture"), ImprintService->CanRecordCommittedCapture());
	const FGuid FirstGrantId = ImprintService->RecordCommittedCapture(
		CaptureResult.Committed, Guard.StableNetId, Condition);
	TestTrue(TEXT("Collection 生成 FishRecorded GrantId"), FirstGrantId.IsValid());
	TestEqual(TEXT("首个 Grant 等待 durable ACK"), ImprintService->GetPendingGrantAckCount(), 1);
	TestFalse(TEXT("仍有 Grant 未 ACK"), ImprintService->AreAllGrantAcksComplete());

	const FGuid ReplayGrantId = ImprintService->RecordCommittedCapture(
		CaptureResult.Committed, Guard.StableNetId, Condition);
	TestEqual(TEXT("同一 CaptureRequestId 重放返回同一 GrantId"), ReplayGrantId, FirstGrantId);
	TestEqual(TEXT("重放不增加 pending Grant 数量"), ImprintService->GetPendingGrantAckCount(), 1);
	TestFalse(TEXT("teardown 前仍需等待 durable ACK"), ImprintService->PrepareForRunTeardown());
	TestEqual(TEXT("teardown 重投不复制 Grant"), ImprintService->GetPendingGrantAckCount(), 1);

	ItemsService->UnregisterContainer(Guard.Component.Get());
	FCatContainerSnapshot RemovedSnapshot;
	TestFalse(TEXT("切片结束后容器已注销"), ItemsService->TryGetContainerSnapshot(Guard.ContainerId, RemovedSnapshot));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
