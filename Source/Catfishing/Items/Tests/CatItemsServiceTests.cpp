#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCommitCaptureReplayTest,
	"Catfishing.Unit.Items.CommitCapture.ReplayDoesNotDuplicateFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCommitCaptureStaleRevisionTest,
	"Catfishing.Unit.Items.CommitCapture.StaleExpectedRevisionDoesNotMutateContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCommitCaptureWrongContainerKindTest,
	"Catfishing.Unit.Items.CommitCapture.WrongContainerKindDoesNotMutateContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCommitCaptureFullContainerTest,
	"Catfishing.Unit.Items.CommitCapture.FullContainerDoesNotMutateContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：
// 1. 先确认引擎可用并记录现有 WorldContext 数量；引擎不可用时立即失败，Game World 创建失败时也不继续触碰 Items 状态。
// 2. 在真实 Game World 中取得 Items 子系统，创建并注册地面鱼护，随后从正式查询入口核对初始身份、空内容和 Revision。
// 3. 通过正式捕获入口提交一条鱼，再原样重放同一命令；分别核对首次提交事实与 AlreadyResolved 结果，证明重复请求没有推进 Revision。
// 4. 从公开快照核对最终只有一条完整的鱼记录，以容器对外可见状态证明重放没有造成重复写入。
// 5. 注销容器并确认旧快照不可再查询；离开作用域时由 FTestWorldWrapper 成对销毁 World 与 WorldContext，最后核对全局上下文数量已恢复。
bool FCatItemsCommitCaptureReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (!TestNotNull(TEXT("引擎实例可用于创建测试 World"), GEngine))
	{
		return false;
	}

	const int32 InitialWorldContextCount = GEngine->GetWorldContexts().Num();
	{
		FTestWorldWrapper WorldWrapper;
		if (TestTrue(TEXT("创建最小 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
		{
			UWorld* World = WorldWrapper.GetTestWorld();
			UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
			AActor* ContainerHost = World ? World->SpawnActor<AActor>() : nullptr;
			UCatContainerReplicationComponent* ReplicationComponent = ContainerHost
				? NewObject<UCatContainerReplicationComponent>(ContainerHost)
				: nullptr;

			const bool bHasRuntimeObjects = TestNotNull(TEXT("测试 World 可用"), World)
				&& TestNotNull(TEXT("真实 Items WorldSubsystem 已创建"), ItemsService)
				&& TestNotNull(TEXT("容器宿主 Actor 已创建"), ContainerHost)
				&& TestNotNull(TEXT("真实容器复制组件已创建"), ReplicationComponent);
			if (bHasRuntimeObjects)
			{
				ContainerHost->AddInstanceComponent(ReplicationComponent);
				ReplicationComponent->RegisterComponent();

				const FString StableNetId = TEXT("TestStableNetId");
				const FGuid ContainerId = FGuid::NewGuid();
				const bool bRegistered = ItemsService->RegisterContainer(
					ReplicationComponent,
					ContainerId,
					ECatContainerKind::FishGuard,
					StableNetId,
					2);
				TestTrue(TEXT("地面鱼护通过正式入口注册"), bRegistered);

				FCatContainerSnapshot InitialSnapshot;
				const bool bHasInitialSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, InitialSnapshot);
				TestTrue(TEXT("注册后可从正式查询入口读取初始快照"), bHasInitialSnapshot);
				if (bHasInitialSnapshot)
				{
					TestEqual(TEXT("初始快照容器身份正确"), InitialSnapshot.ContainerId, ContainerId);
					TestEqual(TEXT("初始快照为地面鱼护"), InitialSnapshot.Kind, ECatContainerKind::FishGuard);
					TestEqual(TEXT("初始 Revision 从 1 开始"), InitialSnapshot.Revision, int64{1});
					TestEqual(TEXT("初始鱼护为空"), InitialSnapshot.Fish.Num(), 0);

					FCatCaptureCommitCommand Command;
					Command.Context.RequestId = FGuid::NewGuid();
					Command.Context.ExpectedRevision = InitialSnapshot.Revision;
					Command.Context.StableNetId = StableNetId;
					Command.FishingSessionId = FGuid::NewGuid();
					Command.FishInstanceId = FGuid::NewGuid();
					Command.FishDefinitionId = TEXT("TestFish");
					Command.TargetContainerId = ContainerId;
					Command.WeightKilograms = 1.25;
					Command.SacrificeContribution = 3;

					const FCatCaptureCommitResult FirstResult = ItemsService->CommitCapture(Command);
					TestTrue(TEXT("首次合法捕获发生不可逆提交"), FirstResult.Command.bCommitted);
					TestEqual(TEXT("首次提交没有拒绝原因"), FirstResult.Command.Error, ECatDomainCommandError::None);
					TestEqual(TEXT("首次结果关联原请求"), FirstResult.Command.RequestId, Command.Context.RequestId);
					TestEqual(TEXT("首次提交把容器推进到 Revision 2"), FirstResult.Command.Revision, int64{2});
					TestEqual(TEXT("提交事实保留捕获请求"), FirstResult.Committed.CaptureRequestId, Command.Context.RequestId);
					TestEqual(TEXT("提交事实保留钓鱼会话"), FirstResult.Committed.FishingSessionId, Command.FishingSessionId);
					TestEqual(TEXT("提交事实保留目标容器"), FirstResult.Committed.ContainerId, Command.TargetContainerId);
					TestEqual(TEXT("提交事实 Revision 与命令结果一致"), FirstResult.Committed.ContainerRevision, FirstResult.Command.Revision);
					TestEqual(TEXT("提交事实保留鱼实例身份"), FirstResult.Committed.FishInstance.FishInstanceId, Command.FishInstanceId);
					TestEqual(TEXT("提交事实保留鱼定义"), FirstResult.Committed.FishInstance.FishDefinitionId, Command.FishDefinitionId);
					TestEqual(TEXT("提交事实保留鱼主人"), FirstResult.Committed.FishInstance.OwnerStableNetId, Command.Context.StableNetId);
					TestEqual(TEXT("提交事实保留来源会话"), FirstResult.Committed.FishInstance.SourceFishingSessionId, Command.FishingSessionId);
					TestEqual(TEXT("提交事实冻结献祭贡献"), FirstResult.Committed.FishInstance.SacrificeContribution, Command.SacrificeContribution);
					TestEqual(TEXT("提交事实冻结鱼重量"), FirstResult.Committed.FishInstance.WeightKilograms, Command.WeightKilograms);

					const FCatCaptureCommitResult ReplayResult = ItemsService->CommitCapture(Command);
					TestFalse(TEXT("原样重放不发生第二次提交"), ReplayResult.Command.bCommitted);
					TestEqual(TEXT("原样重放返回 AlreadyResolved"), ReplayResult.Command.Error, ECatDomainCommandError::AlreadyResolved);
					TestEqual(TEXT("重放仍关联原请求"), ReplayResult.Command.RequestId, FirstResult.Command.RequestId);
					TestEqual(TEXT("重放不推进 Revision"), ReplayResult.Command.Revision, FirstResult.Command.Revision);
					TestEqual(TEXT("重放保留首次捕获请求"), ReplayResult.Committed.CaptureRequestId, FirstResult.Committed.CaptureRequestId);
					TestEqual(TEXT("重放保留首次钓鱼会话"), ReplayResult.Committed.FishingSessionId, FirstResult.Committed.FishingSessionId);
					TestEqual(TEXT("重放保留首次目标容器"), ReplayResult.Committed.ContainerId, FirstResult.Committed.ContainerId);
					TestEqual(TEXT("重放保留首次提交 Revision"), ReplayResult.Committed.ContainerRevision, FirstResult.Committed.ContainerRevision);
					TestEqual(TEXT("重放保留首次鱼实例"), ReplayResult.Committed.FishInstance.FishInstanceId, FirstResult.Committed.FishInstance.FishInstanceId);
					TestEqual(TEXT("重放保留首次鱼定义"), ReplayResult.Committed.FishInstance.FishDefinitionId, FirstResult.Committed.FishInstance.FishDefinitionId);
					TestEqual(TEXT("重放保留首次鱼主人"), ReplayResult.Committed.FishInstance.OwnerStableNetId, FirstResult.Committed.FishInstance.OwnerStableNetId);
					TestEqual(TEXT("重放保留首次来源会话"), ReplayResult.Committed.FishInstance.SourceFishingSessionId, FirstResult.Committed.FishInstance.SourceFishingSessionId);
					TestEqual(TEXT("重放保留首次献祭贡献"), ReplayResult.Committed.FishInstance.SacrificeContribution, FirstResult.Committed.FishInstance.SacrificeContribution);
					TestEqual(TEXT("重放保留首次鱼重量"), ReplayResult.Committed.FishInstance.WeightKilograms, FirstResult.Committed.FishInstance.WeightKilograms);

					FCatContainerSnapshot FinalSnapshot;
					const bool bHasFinalSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, FinalSnapshot);
					TestTrue(TEXT("重放后仍可读取容器公开快照"), bHasFinalSnapshot);
					if (bHasFinalSnapshot)
					{
						TestEqual(TEXT("重放后 Revision 仍为 2"), FinalSnapshot.Revision, int64{2});
						TestEqual(TEXT("重放后鱼护仍只有一条鱼"), FinalSnapshot.Fish.Num(), 1);
						if (FinalSnapshot.Fish.Num() == 1)
						{
							const FCatFishInstance& Fish = FinalSnapshot.Fish[0];
							TestEqual(TEXT("最终快照保留唯一鱼实例"), Fish.FishInstanceId, Command.FishInstanceId);
							TestEqual(TEXT("最终快照保留鱼定义"), Fish.FishDefinitionId, Command.FishDefinitionId);
							TestEqual(TEXT("最终快照保留鱼主人"), Fish.OwnerStableNetId, Command.Context.StableNetId);
							TestEqual(TEXT("最终快照保留来源会话"), Fish.SourceFishingSessionId, Command.FishingSessionId);
							TestEqual(TEXT("最终快照保留献祭贡献"), Fish.SacrificeContribution, Command.SacrificeContribution);
							TestEqual(TEXT("最终快照保留鱼重量"), Fish.WeightKilograms, Command.WeightKilograms);
						}
					}
				}

				ItemsService->UnregisterContainer(ReplicationComponent);
				FCatContainerSnapshot RemovedSnapshot;
				TestFalse(TEXT("注销后正式查询入口不再暴露旧容器"),
					ItemsService->TryGetContainerSnapshot(ContainerId, RemovedSnapshot));
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}

	TestEqual(TEXT("测试结束后 WorldContext 数量恢复"), GEngine->GetWorldContexts().Num(), InitialWorldContextCount);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在独立 Game World 中注册容量为 2 的地面鱼护，先合法提交种子鱼，把聚合推进到 Revision=2 且保留一个可核对的既有实物。
// 2. 保存正式查询入口返回的完整失败前快照，再用新的请求、会话和鱼实例提交 ExpectedRevision=1 的捕获命令，确保拒绝原因只来自调用方版本陈旧。
// 3. 从 CommitCapture 的公开 Result 核对 RevisionConflict、未提交和当前 Revision，再重新查询容器快照。
// 4. 逐字段比较失败前后公开快照，并显式确认待捕获鱼未出现，以证明拒绝分支没有留下部分写入。
// 5. 注销容器后由 FTestWorldWrapper 释放 World；作用域外核对 WorldContext 数量恢复，避免测试间共享环境状态。
bool FCatItemsCommitCaptureStaleRevisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (!TestNotNull(TEXT("引擎实例可用于创建测试 World"), GEngine))
	{
		return false;
	}

	const int32 InitialWorldContextCount = GEngine->GetWorldContexts().Num();
	{
		FTestWorldWrapper WorldWrapper;
		if (TestTrue(TEXT("创建陈旧 Revision 场景的最小 Game World"),
			WorldWrapper.CreateTestWorld(EWorldType::Game)))
		{
			UWorld* World = WorldWrapper.GetTestWorld();
			UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
			AActor* ContainerHost = World ? World->SpawnActor<AActor>() : nullptr;
			UCatContainerReplicationComponent* ReplicationComponent = ContainerHost
				? NewObject<UCatContainerReplicationComponent>(ContainerHost)
				: nullptr;

			const bool bHasRuntimeObjects = TestNotNull(TEXT("陈旧 Revision 测试 World 可用"), World)
				&& TestNotNull(TEXT("陈旧 Revision 场景取得真实 Items 子系统"), ItemsService)
				&& TestNotNull(TEXT("陈旧 Revision 场景创建容器宿主"), ContainerHost)
				&& TestNotNull(TEXT("陈旧 Revision 场景创建容器复制组件"), ReplicationComponent);
			if (bHasRuntimeObjects)
			{
				ContainerHost->AddInstanceComponent(ReplicationComponent);
				ReplicationComponent->RegisterComponent();

				const FString StableNetId = TEXT("StaleRevisionStableNetId");
				const FGuid ContainerId = FGuid::NewGuid();
				TestTrue(TEXT("陈旧 Revision 场景通过正式入口注册地面鱼护"),
					ItemsService->RegisterContainer(
						ReplicationComponent,
						ContainerId,
						ECatContainerKind::FishGuard,
						StableNetId,
						2));

				FCatContainerSnapshot InitialSnapshot;
				const bool bHasInitialSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, InitialSnapshot);
				TestTrue(TEXT("种子捕获前可读取容器公开快照"), bHasInitialSnapshot);
				if (bHasInitialSnapshot)
				{
					TestEqual(TEXT("种子捕获前 Revision 为 1"), InitialSnapshot.Revision, int64{1});
					TestEqual(TEXT("种子捕获前容器为空"), InitialSnapshot.Fish.Num(), 0);

					FCatCaptureCommitCommand SeedCommand;
					SeedCommand.Context.RequestId = FGuid::NewGuid();
					SeedCommand.Context.ExpectedRevision = InitialSnapshot.Revision;
					SeedCommand.Context.StableNetId = StableNetId;
					SeedCommand.FishingSessionId = FGuid::NewGuid();
					SeedCommand.FishInstanceId = FGuid::NewGuid();
					SeedCommand.FishDefinitionId = TEXT("StaleRevisionSeedFish");
					SeedCommand.TargetContainerId = ContainerId;
					SeedCommand.WeightKilograms = 1.75;
					SeedCommand.SacrificeContribution = 2;
					const FCatCaptureCommitResult SeedResult = ItemsService->CommitCapture(SeedCommand);
					TestTrue(TEXT("合法种子捕获提交成功"), SeedResult.Command.bCommitted);
					TestEqual(TEXT("种子捕获没有拒绝原因"), SeedResult.Command.Error, ECatDomainCommandError::None);

					FCatContainerSnapshot BeforeSnapshot;
					const bool bHasBeforeSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, BeforeSnapshot);
					TestTrue(TEXT("陈旧请求前可读取包含种子鱼的公开快照"), bHasBeforeSnapshot);
					if (bHasBeforeSnapshot)
					{
						TestEqual(TEXT("失败前容器身份正确"), BeforeSnapshot.ContainerId, ContainerId);
						TestEqual(TEXT("失败前容器为地面鱼护"), BeforeSnapshot.Kind, ECatContainerKind::FishGuard);
						TestEqual(TEXT("失败前 Revision 为 2"), BeforeSnapshot.Revision, int64{2});
						TestEqual(TEXT("失败前容器包含一条种子鱼"), BeforeSnapshot.Fish.Num(), 1);

						FCatCaptureCommitCommand Command;
						Command.Context.RequestId = FGuid::NewGuid();
						Command.Context.ExpectedRevision = InitialSnapshot.Revision;
						Command.Context.StableNetId = StableNetId;
						Command.FishingSessionId = FGuid::NewGuid();
						Command.FishInstanceId = FGuid::NewGuid();
						Command.FishDefinitionId = TEXT("StaleRevisionTestFish");
						Command.TargetContainerId = ContainerId;
						Command.WeightKilograms = 2.5;
						Command.SacrificeContribution = 4;

						const FCatCaptureCommitResult Result = ItemsService->CommitCapture(Command);
						TestFalse(TEXT("陈旧 Revision 不发生捕获提交"), Result.Command.bCommitted);
						TestEqual(TEXT("陈旧 Revision 返回 RevisionConflict"),
							Result.Command.Error,
							ECatDomainCommandError::RevisionConflict);
						TestEqual(TEXT("拒绝结果关联原请求"), Result.Command.RequestId, Command.Context.RequestId);
						TestEqual(TEXT("拒绝结果返回容器当前 Revision"), Result.Command.Revision, BeforeSnapshot.Revision);
						TestFalse(TEXT("拒绝结果不伪造捕获请求事实"), Result.Committed.CaptureRequestId.IsValid());
						TestFalse(TEXT("拒绝结果不伪造钓鱼会话事实"), Result.Committed.FishingSessionId.IsValid());
						TestFalse(TEXT("拒绝结果不伪造鱼实例事实"), Result.Committed.FishInstance.FishInstanceId.IsValid());
						TestTrue(TEXT("拒绝结果的鱼定义保持为空"), Result.Committed.FishInstance.FishDefinitionId.IsNone());
						TestTrue(TEXT("拒绝结果的鱼主人保持为空"), Result.Committed.FishInstance.OwnerStableNetId.IsEmpty());
						TestFalse(TEXT("拒绝结果不伪造来源会话"), Result.Committed.FishInstance.SourceFishingSessionId.IsValid());
						TestEqual(TEXT("拒绝结果不包含献祭贡献"), Result.Committed.FishInstance.SacrificeContribution, 0);
						TestEqual(TEXT("拒绝结果不包含鱼重量"), Result.Committed.FishInstance.WeightKilograms, 0.0);
						TestFalse(TEXT("拒绝结果不伪造目标容器事实"), Result.Committed.ContainerId.IsValid());
						TestEqual(TEXT("拒绝结果不包含提交 Revision"), Result.Committed.ContainerRevision, int64{0});

						FCatContainerSnapshot AfterSnapshot;
						const bool bHasAfterSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, AfterSnapshot);
						TestTrue(TEXT("陈旧请求后仍可读取容器公开快照"), bHasAfterSnapshot);
						if (bHasAfterSnapshot)
						{
							TestEqual(TEXT("拒绝前后容器身份不变"), AfterSnapshot.ContainerId, BeforeSnapshot.ContainerId);
							TestEqual(TEXT("拒绝前后容器类别不变"), AfterSnapshot.Kind, BeforeSnapshot.Kind);
							TestEqual(TEXT("拒绝前后 Revision 不变"), AfterSnapshot.Revision, BeforeSnapshot.Revision);
							TestEqual(TEXT("拒绝前后鱼数量不变"), AfterSnapshot.Fish.Num(), BeforeSnapshot.Fish.Num());
							if (AfterSnapshot.Fish.Num() == 1 && BeforeSnapshot.Fish.Num() == 1)
							{
								const FCatFishInstance& BeforeFish = BeforeSnapshot.Fish[0];
								const FCatFishInstance& AfterFish = AfterSnapshot.Fish[0];
								TestEqual(TEXT("拒绝前后既有鱼实例身份不变"), AfterFish.FishInstanceId, BeforeFish.FishInstanceId);
								TestEqual(TEXT("拒绝前后既有鱼定义不变"), AfterFish.FishDefinitionId, BeforeFish.FishDefinitionId);
								TestEqual(TEXT("拒绝前后既有鱼主人不变"), AfterFish.OwnerStableNetId, BeforeFish.OwnerStableNetId);
								TestEqual(TEXT("拒绝前后既有鱼来源会话不变"), AfterFish.SourceFishingSessionId, BeforeFish.SourceFishingSessionId);
								TestEqual(TEXT("拒绝前后既有鱼献祭贡献不变"), AfterFish.SacrificeContribution, BeforeFish.SacrificeContribution);
								TestEqual(TEXT("拒绝前后既有鱼重量不变"), AfterFish.WeightKilograms, BeforeFish.WeightKilograms);
							}
							TestFalse(TEXT("陈旧请求中的新鱼未进入容器"),
								AfterSnapshot.Fish.ContainsByPredicate([&Command](const FCatFishInstance& Fish)
								{
									return Fish.FishInstanceId == Command.FishInstanceId;
								}));
						}
					}
				}

				ItemsService->UnregisterContainer(ReplicationComponent);
				FCatContainerSnapshot RemovedSnapshot;
				TestFalse(TEXT("陈旧 Revision 场景注销后不再暴露旧容器"),
					ItemsService->TryGetContainerSnapshot(ContainerId, RemovedSnapshot));
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}

	TestEqual(TEXT("陈旧 Revision 测试结束后 WorldContext 数量恢复"),
		GEngine->GetWorldContexts().Num(),
		InitialWorldContextCount);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在独立 Game World 中注册容量为 2 的共享鱼缸，并从正式查询入口保存 Revision=1 的失败前公开快照。
// 2. 构造其余字段和 ExpectedRevision 均合法的捕获命令，使拒绝只归因于目标不是地面鱼护。
// 3. 从 CommitCapture 的公开 Result 核对 PermissionDenied、未提交、原 RequestId、当前 Revision 与空 Committed DTO。
// 4. 逐字段比较失败前后公开快照并确认鱼数组仍为空，证明容器种类校验没有产生任何公开副作用。
// 5. 注销容器并由 FTestWorldWrapper 成对释放 World；作用域外核对 WorldContext 数量恢复，避免身份测试污染其他用例。
bool FCatItemsCommitCaptureWrongContainerKindTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (!TestNotNull(TEXT("引擎实例可用于创建测试 World"), GEngine))
	{
		return false;
	}

	const int32 InitialWorldContextCount = GEngine->GetWorldContexts().Num();
	{
		FTestWorldWrapper WorldWrapper;
		if (TestTrue(TEXT("创建错误 StableNetId 场景的最小 Game World"),
			WorldWrapper.CreateTestWorld(EWorldType::Game)))
		{
			UWorld* World = WorldWrapper.GetTestWorld();
			UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
			AActor* ContainerHost = World ? World->SpawnActor<AActor>() : nullptr;
			UCatContainerReplicationComponent* ReplicationComponent = ContainerHost
				? NewObject<UCatContainerReplicationComponent>(ContainerHost)
				: nullptr;

			const bool bHasRuntimeObjects = TestNotNull(TEXT("错误身份测试 World 可用"), World)
				&& TestNotNull(TEXT("错误身份场景取得真实 Items 子系统"), ItemsService)
				&& TestNotNull(TEXT("错误身份场景创建容器宿主"), ContainerHost)
				&& TestNotNull(TEXT("错误身份场景创建容器复制组件"), ReplicationComponent);
			if (bHasRuntimeObjects)
			{
				ContainerHost->AddInstanceComponent(ReplicationComponent);
				ReplicationComponent->RegisterComponent();

				const FString OwnerStableNetId = TEXT("OwnerStableNetId");
				const FString RequestStableNetId = TEXT("DifferentStableNetId");
				const FGuid ContainerId = FGuid::NewGuid();
				TestTrue(TEXT("错误容器种类场景注册共享鱼缸"),
					ItemsService->RegisterContainer(
						ReplicationComponent,
						ContainerId,
						ECatContainerKind::SharedFishTank,
						OwnerStableNetId,
						2));

				FCatContainerSnapshot BeforeSnapshot;
				const bool bHasBeforeSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, BeforeSnapshot);
				TestTrue(TEXT("错误身份请求前可读取容器公开快照"), bHasBeforeSnapshot);
				if (bHasBeforeSnapshot)
				{
					TestEqual(TEXT("失败前容器身份正确"), BeforeSnapshot.ContainerId, ContainerId);
					TestEqual(TEXT("失败前容器为共享鱼缸"), BeforeSnapshot.Kind, ECatContainerKind::SharedFishTank);
					TestEqual(TEXT("失败前 Revision 为 1"), BeforeSnapshot.Revision, int64{1});
					TestEqual(TEXT("失败前共享鱼缸为空"), BeforeSnapshot.Fish.Num(), 0);

					FCatCaptureCommitCommand Command;
					Command.Context.RequestId = FGuid::NewGuid();
					Command.Context.ExpectedRevision = BeforeSnapshot.Revision;
					Command.Context.StableNetId = RequestStableNetId;
					Command.FishingSessionId = FGuid::NewGuid();
					Command.FishInstanceId = FGuid::NewGuid();
					Command.FishDefinitionId = TEXT("WrongContainerKindTestFish");
					Command.TargetContainerId = ContainerId;
					Command.WeightKilograms = 2.25;
					Command.SacrificeContribution = 5;

					const FCatCaptureCommitResult Result = ItemsService->CommitCapture(Command);
					TestFalse(TEXT("错误容器种类不发生捕获提交"), Result.Command.bCommitted);
					TestEqual(TEXT("错误容器种类返回 PermissionDenied"),
						Result.Command.Error,
						ECatDomainCommandError::PermissionDenied);
					TestEqual(TEXT("权限拒绝结果关联原请求"), Result.Command.RequestId, Command.Context.RequestId);
					TestEqual(TEXT("权限拒绝结果返回当前 Revision"), Result.Command.Revision, BeforeSnapshot.Revision);
					TestFalse(TEXT("权限拒绝结果不伪造捕获请求事实"), Result.Committed.CaptureRequestId.IsValid());
					TestFalse(TEXT("权限拒绝结果不伪造钓鱼会话事实"), Result.Committed.FishingSessionId.IsValid());
					TestFalse(TEXT("权限拒绝结果不伪造鱼实例事实"), Result.Committed.FishInstance.FishInstanceId.IsValid());
					TestTrue(TEXT("权限拒绝结果的鱼定义保持为空"), Result.Committed.FishInstance.FishDefinitionId.IsNone());
					TestTrue(TEXT("权限拒绝结果的鱼主人保持为空"), Result.Committed.FishInstance.OwnerStableNetId.IsEmpty());
					TestFalse(TEXT("权限拒绝结果不伪造来源会话"), Result.Committed.FishInstance.SourceFishingSessionId.IsValid());
					TestEqual(TEXT("权限拒绝结果不包含献祭贡献"), Result.Committed.FishInstance.SacrificeContribution, 0);
					TestEqual(TEXT("权限拒绝结果不包含鱼重量"), Result.Committed.FishInstance.WeightKilograms, 0.0);
					TestFalse(TEXT("权限拒绝结果不伪造目标容器事实"), Result.Committed.ContainerId.IsValid());
					TestEqual(TEXT("权限拒绝结果不包含提交 Revision"), Result.Committed.ContainerRevision, int64{0});

					FCatContainerSnapshot AfterSnapshot;
					const bool bHasAfterSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, AfterSnapshot);
					TestTrue(TEXT("错误身份请求后仍可读取容器公开快照"), bHasAfterSnapshot);
					if (bHasAfterSnapshot)
					{
						TestEqual(TEXT("权限拒绝前后容器身份不变"), AfterSnapshot.ContainerId, BeforeSnapshot.ContainerId);
						TestEqual(TEXT("权限拒绝前后容器类别不变"), AfterSnapshot.Kind, BeforeSnapshot.Kind);
						TestEqual(TEXT("权限拒绝前后 Revision 不变"), AfterSnapshot.Revision, BeforeSnapshot.Revision);
						TestEqual(TEXT("权限拒绝前后鱼数量不变"), AfterSnapshot.Fish.Num(), BeforeSnapshot.Fish.Num());
						TestEqual(TEXT("权限拒绝后共享鱼缸仍为空"), AfterSnapshot.Fish.Num(), 0);
					}
				}

				ItemsService->UnregisterContainer(ReplicationComponent);
				FCatContainerSnapshot RemovedSnapshot;
				TestFalse(TEXT("错误身份场景注销后不再暴露旧容器"),
					ItemsService->TryGetContainerSnapshot(ContainerId, RemovedSnapshot));
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}

	TestEqual(TEXT("错误 StableNetId 测试结束后 WorldContext 数量恢复"),
		GEngine->GetWorldContexts().Num(),
		InitialWorldContextCount);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在独立 Game World 中注册容量为 1 的地面鱼护，再合法提交种子鱼，把容器推进到 Revision=2 且占满唯一槽位。
// 2. 保存包含种子鱼的完整公开快照，然后用正确主人、当前 Revision 和全新的请求、会话、鱼实例提交第二条合法捕获命令。
// 3. 从 CommitCapture 的公开 Result 核对 CapacityExceeded、未提交、原 RequestId、当前 Revision 与空 Committed DTO。
// 4. 逐字段比较失败前后公开快照，并显式确认第二条鱼没有出现，以证明容量拒绝没有覆盖或复制既有实物。
// 5. 注销容器并由 FTestWorldWrapper 成对释放 World；作用域外核对 WorldContext 数量恢复，避免满容器场景污染后续测试。
bool FCatItemsCommitCaptureFullContainerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (!TestNotNull(TEXT("引擎实例可用于创建测试 World"), GEngine))
	{
		return false;
	}

	const int32 InitialWorldContextCount = GEngine->GetWorldContexts().Num();
	{
		FTestWorldWrapper WorldWrapper;
		if (TestTrue(TEXT("创建满容器场景的最小 Game World"),
			WorldWrapper.CreateTestWorld(EWorldType::Game)))
		{
			UWorld* World = WorldWrapper.GetTestWorld();
			UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
			AActor* ContainerHost = World ? World->SpawnActor<AActor>() : nullptr;
			UCatContainerReplicationComponent* ReplicationComponent = ContainerHost
				? NewObject<UCatContainerReplicationComponent>(ContainerHost)
				: nullptr;

			const bool bHasRuntimeObjects = TestNotNull(TEXT("满容器测试 World 可用"), World)
				&& TestNotNull(TEXT("满容器场景取得真实 Items 子系统"), ItemsService)
				&& TestNotNull(TEXT("满容器场景创建容器宿主"), ContainerHost)
				&& TestNotNull(TEXT("满容器场景创建容器复制组件"), ReplicationComponent);
			if (bHasRuntimeObjects)
			{
				ContainerHost->AddInstanceComponent(ReplicationComponent);
				ReplicationComponent->RegisterComponent();

				const FString StableNetId = TEXT("FullContainerOwnerStableNetId");
				const FGuid ContainerId = FGuid::NewGuid();
				TestTrue(TEXT("满容器场景注册容量为 1 的地面鱼护"),
					ItemsService->RegisterContainer(
						ReplicationComponent,
						ContainerId,
						ECatContainerKind::FishGuard,
						StableNetId,
						1));

				FCatContainerSnapshot InitialSnapshot;
				const bool bHasInitialSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, InitialSnapshot);
				TestTrue(TEXT("种子捕获前可读取容量为 1 的容器快照"), bHasInitialSnapshot);
				if (bHasInitialSnapshot)
				{
					TestEqual(TEXT("种子捕获前 Revision 为 1"), InitialSnapshot.Revision, int64{1});
					TestEqual(TEXT("种子捕获前地面鱼护为空"), InitialSnapshot.Fish.Num(), 0);

					FCatCaptureCommitCommand SeedCommand;
					SeedCommand.Context.RequestId = FGuid::NewGuid();
					SeedCommand.Context.ExpectedRevision = InitialSnapshot.Revision;
					SeedCommand.Context.StableNetId = StableNetId;
					SeedCommand.FishingSessionId = FGuid::NewGuid();
					SeedCommand.FishInstanceId = FGuid::NewGuid();
					SeedCommand.FishDefinitionId = TEXT("FullContainerSeedFish");
					SeedCommand.TargetContainerId = ContainerId;
					SeedCommand.WeightKilograms = 1.5;
					SeedCommand.SacrificeContribution = 2;
					const FCatCaptureCommitResult SeedResult = ItemsService->CommitCapture(SeedCommand);
					TestTrue(TEXT("容量为 1 的鱼护接受首条种子捕获"), SeedResult.Command.bCommitted);
					TestEqual(TEXT("种子捕获没有拒绝原因"), SeedResult.Command.Error, ECatDomainCommandError::None);

					FCatContainerSnapshot BeforeSnapshot;
					const bool bHasBeforeSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, BeforeSnapshot);
					TestTrue(TEXT("第二条捕获前可读取已满容器公开快照"), bHasBeforeSnapshot);
					if (bHasBeforeSnapshot)
					{
						TestEqual(TEXT("失败前容器身份正确"), BeforeSnapshot.ContainerId, ContainerId);
						TestEqual(TEXT("失败前容器为地面鱼护"), BeforeSnapshot.Kind, ECatContainerKind::FishGuard);
						TestEqual(TEXT("失败前 Revision 为 2"), BeforeSnapshot.Revision, int64{2});
						TestEqual(TEXT("失败前容器包含唯一种子鱼"), BeforeSnapshot.Fish.Num(), 1);

						FCatCaptureCommitCommand Command;
						Command.Context.RequestId = FGuid::NewGuid();
						Command.Context.ExpectedRevision = BeforeSnapshot.Revision;
						Command.Context.StableNetId = StableNetId;
						Command.FishingSessionId = FGuid::NewGuid();
						Command.FishInstanceId = FGuid::NewGuid();
						Command.FishDefinitionId = TEXT("FullContainerRejectedFish");
						Command.TargetContainerId = ContainerId;
						Command.WeightKilograms = 3.25;
						Command.SacrificeContribution = 6;

						const FCatCaptureCommitResult Result = ItemsService->CommitCapture(Command);
						TestFalse(TEXT("已满地面鱼护不发生第二次捕获提交"), Result.Command.bCommitted);
						TestEqual(TEXT("已满地面鱼护返回 CapacityExceeded"),
							Result.Command.Error,
							ECatDomainCommandError::CapacityExceeded);
						TestEqual(TEXT("容量拒绝结果关联第二条请求"), Result.Command.RequestId, Command.Context.RequestId);
						TestEqual(TEXT("容量拒绝结果返回当前 Revision"), Result.Command.Revision, BeforeSnapshot.Revision);
						TestFalse(TEXT("容量拒绝结果不伪造捕获请求事实"), Result.Committed.CaptureRequestId.IsValid());
						TestFalse(TEXT("容量拒绝结果不伪造钓鱼会话事实"), Result.Committed.FishingSessionId.IsValid());
						TestFalse(TEXT("容量拒绝结果不伪造鱼实例事实"), Result.Committed.FishInstance.FishInstanceId.IsValid());
						TestTrue(TEXT("容量拒绝结果的鱼定义保持为空"), Result.Committed.FishInstance.FishDefinitionId.IsNone());
						TestTrue(TEXT("容量拒绝结果的鱼主人保持为空"), Result.Committed.FishInstance.OwnerStableNetId.IsEmpty());
						TestFalse(TEXT("容量拒绝结果不伪造来源会话"), Result.Committed.FishInstance.SourceFishingSessionId.IsValid());
						TestEqual(TEXT("容量拒绝结果不包含献祭贡献"), Result.Committed.FishInstance.SacrificeContribution, 0);
						TestEqual(TEXT("容量拒绝结果不包含鱼重量"), Result.Committed.FishInstance.WeightKilograms, 0.0);
						TestFalse(TEXT("容量拒绝结果不伪造目标容器事实"), Result.Committed.ContainerId.IsValid());
						TestEqual(TEXT("容量拒绝结果不包含提交 Revision"), Result.Committed.ContainerRevision, int64{0});

						FCatContainerSnapshot AfterSnapshot;
						const bool bHasAfterSnapshot = ItemsService->TryGetContainerSnapshot(ContainerId, AfterSnapshot);
						TestTrue(TEXT("容量拒绝后仍可读取容器公开快照"), bHasAfterSnapshot);
						if (bHasAfterSnapshot)
						{
							TestEqual(TEXT("容量拒绝前后容器身份不变"), AfterSnapshot.ContainerId, BeforeSnapshot.ContainerId);
							TestEqual(TEXT("容量拒绝前后容器类别不变"), AfterSnapshot.Kind, BeforeSnapshot.Kind);
							TestEqual(TEXT("容量拒绝前后 Revision 不变"), AfterSnapshot.Revision, BeforeSnapshot.Revision);
							TestEqual(TEXT("容量拒绝前后鱼数量不变"), AfterSnapshot.Fish.Num(), BeforeSnapshot.Fish.Num());
							if (AfterSnapshot.Fish.Num() == 1 && BeforeSnapshot.Fish.Num() == 1)
							{
								const FCatFishInstance& BeforeFish = BeforeSnapshot.Fish[0];
								const FCatFishInstance& AfterFish = AfterSnapshot.Fish[0];
								TestEqual(TEXT("容量拒绝前后既有鱼实例身份不变"), AfterFish.FishInstanceId, BeforeFish.FishInstanceId);
								TestEqual(TEXT("容量拒绝前后既有鱼定义不变"), AfterFish.FishDefinitionId, BeforeFish.FishDefinitionId);
								TestEqual(TEXT("容量拒绝前后既有鱼主人不变"), AfterFish.OwnerStableNetId, BeforeFish.OwnerStableNetId);
								TestEqual(TEXT("容量拒绝前后既有鱼来源会话不变"), AfterFish.SourceFishingSessionId, BeforeFish.SourceFishingSessionId);
								TestEqual(TEXT("容量拒绝前后既有鱼献祭贡献不变"), AfterFish.SacrificeContribution, BeforeFish.SacrificeContribution);
								TestEqual(TEXT("容量拒绝前后既有鱼重量不变"), AfterFish.WeightKilograms, BeforeFish.WeightKilograms);
							}
							TestFalse(TEXT("容量拒绝的第二条鱼没有进入容器"),
								AfterSnapshot.Fish.ContainsByPredicate([&Command](const FCatFishInstance& Fish)
								{
									return Fish.FishInstanceId == Command.FishInstanceId;
								}));
						}
					}
				}

				ItemsService->UnregisterContainer(ReplicationComponent);
				FCatContainerSnapshot RemovedSnapshot;
				TestFalse(TEXT("满容器场景注销后不再暴露旧容器"),
					ItemsService->TryGetContainerSnapshot(ContainerId, RemovedSnapshot));
			}
		}
		WorldWrapper.ForwardErrorMessages(this);
	}

	TestEqual(TEXT("满容器测试结束后 WorldContext 数量恢复"),
		GEngine->GetWorldContexts().Num(),
		InitialWorldContextCount);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
