#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "GameFramework/Actor.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"

namespace CatItemsServiceTransactionTest
{
	/** 已注册测试容器的最小上下文；它代表真实 Actor 宿主、复制组件、稳定容器 ID 与注册身份。 */
	struct FRegisteredContainer
	{
		/** 容器所在的 authority Actor；测试通过它承载复制组件并提供宿主生命周期。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** Items 服务发布快照的正式复制组件；测试只读它，不绕过服务写数组。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本局容器稳定 ID；所有 Items 命令都以它绑定聚合。 */
		FGuid ContainerId;

		/** 容器服务器私有主人；个人鱼护授权和鱼 OwnerStableNetId 都使用同一个值。 */
		FString OwnerStableNetId;
	};

	// 容器注册流程：创建 Actor 与正式复制组件，再通过 Items 服务注册容器；调用方用返回上下文驱动真实公共写口。
	static FRegisteredContainer RegisterContainer(UWorld* World, UCatItemsService* ItemsService,
		const ECatContainerKind Kind, const FString& OwnerStableNetId, const int32 Capacity)
	{
		FRegisteredContainer Result;
		Result.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Result.Component = Result.Owner ? NewObject<UCatContainerReplicationComponent>(Result.Owner) : nullptr;
		Result.ContainerId = FGuid::NewGuid();
		Result.OwnerStableNetId = OwnerStableNetId;
		if (Result.Owner && Result.Component)
		{
			Result.Owner->AddInstanceComponent(Result.Component);
			Result.Component->RegisterComponent();
			ItemsService->RegisterContainer(Result.Component, Result.ContainerId, Kind, OwnerStableNetId, Capacity);
		}
		return Result;
	}

	// 捕获命令流程：为指定个人鱼护构造合法捕获提交；ExpectedRevision 从调用方传入以覆盖首次提交和并发拒绝。
	static FCatCaptureCommitCommand MakeCaptureCommand(const FRegisteredContainer& TargetContainer,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = TargetContainer.OwnerStableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = TEXT("TestFish");
		Command.TargetContainerId = TargetContainer.ContainerId;
		Command.WeightKilograms = 2.5;
		Command.SacrificeContribution = 3;
		return Command;
	}

	// 种鱼流程：通过 CommitCapture 写入一条真实 FishInstance；失败时让测试继续显式断言，避免私造容器内部数组。
	static FCatCaptureCommitResult SeedFish(UCatItemsService* ItemsService, const FRegisteredContainer& TargetContainer,
		const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		return ItemsService->CommitCapture(MakeCaptureCommand(TargetContainer, FGuid::NewGuid(), FishInstanceId,
			ExpectedRevision));
	}

	// 快照读取流程：从服务公开只读接口读取容器；若失败返回默认快照，调用方继续用 TestTrue/TestEqual 暴露问题。
	static FCatContainerSnapshot GetSnapshot(UCatItemsService* ItemsService, const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		ItemsService->TryGetContainerSnapshot(ContainerId, Snapshot);
		return Snapshot;
	}

	// 查询流程：只按公开 Snapshot 判断某条鱼是否存在；测试不读取 Items 私有预留或终态缓存。
	static bool SnapshotContainsFish(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	// 转移命令流程：从当前测试容器上下文构造正式 TransferOwnedFish 命令；源/目标槽位和 Revision 都由调用方明确传入。
	static FCatFishTransferCommand MakeTransferCommand(const FRegisteredContainer& SourceContainer,
		const FRegisteredContainer& TargetContainer, const FGuid RequestId, const FGuid FishInstanceId,
		const int32 SourceContainerSlotIndex, const int32 TargetContainerSlotIndex,
		const int64 ExpectedSourceRevision, const int64 ExpectedTargetRevision)
	{
		FCatFishTransferCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedSourceRevision;
		Command.Context.StableNetId = SourceContainer.OwnerStableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.SourceContainerId = SourceContainer.ContainerId;
		Command.SourceContainerSlotIndex = SourceContainerSlotIndex;
		Command.TargetContainerId = TargetContainer.ContainerId;
		Command.TargetContainerSlotIndex = TargetContainerSlotIndex;
		Command.ExpectedTargetRevision = ExpectedTargetRevision;
		return Command;
	}

	// 进食命令流程：创建直接吃鱼命令；身份和 ExpectedRevision 由调用方控制以覆盖主人授权与并发拒绝。
	static FCatFishConsumeCommand MakeConsumeCommand(const FRegisteredContainer& SourceContainer,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision, const FString& StableNetId)
	{
		FCatFishConsumeCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.SourceContainerId = SourceContainer.ContainerId;
		return Command;
	}

	// 献祭命令流程：构造 Items 预留所需的 SacrificeCommand；只触达 Items 预留/提交接口，不启动 Run 协调器。
	static FCatSacrificeCommand MakeSacrificeCommand(const FRegisteredContainer& Container,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		FCatSacrificeCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = Container.OwnerStableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.ContainerId = Container.ContainerId;
		Command.ExpectedRunRevision = 1;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsTransferOwnedFishTest,
	"Catfishing.Unit.Items.TransferOwnedFish.SuccessReplayAndStaleTargetAreAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsConsumeFishTest,
	"Catfishing.Unit.Items.ConsumeFish.SuccessReplayAndOwnerPermissionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsReservationCommitTest,
	"Catfishing.Unit.Items.ReserveFish.CommitIsIrreversibleAndReplayStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsReservationCancelAndCloseTest,
	"Catfishing.Unit.Items.ReserveFish.CancelReleasesFishAndCloseRejectsNewWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先通过现有 Capture 入口种入旧个人容器，再转移到正式地面鱼护；重放不能二次移动，目标 Revision 陈旧时两边容器必须保持原样。
bool FCatItemsTransferOwnedFishTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 转移测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 转移测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	if (!ItemsService)
	{
		return false;
	}

	const CatItemsServiceTransactionTest::FRegisteredContainer Guard = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 3);
	const CatItemsServiceTransactionTest::FRegisteredContainer FishGuard = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::FishGuard, TEXT(""), 3);
	TestNotNull(TEXT("个人鱼护组件已创建"), Guard.Component.Get());
	TestNotNull(TEXT("地面鱼护组件已创建"), FishGuard.Component.Get());

	const FGuid FishA = FGuid::NewGuid();
	const FGuid FishB = FGuid::NewGuid();
	TestTrue(TEXT("第一条鱼种入个人鱼护"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishA, 1).Command.bCommitted);
	TestTrue(TEXT("第二条鱼种入个人鱼护"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishB, 2).Command.bCommitted);

	const FGuid TransferRequestId = FGuid::NewGuid();
	FCatDomainCommandResult TransferResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, FishGuard, TransferRequestId, FishA, 0, 0, 3, 1));
	TestTrue(TEXT("首次转移提交"), TransferResult.bCommitted);
	TestEqual(TEXT("首次转移返回 None"), TransferResult.Error, ECatDomainCommandError::None);

	FCatContainerSnapshot GuardSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId);
	FCatContainerSnapshot TankSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, FishGuard.ContainerId);
	TestFalse(TEXT("成功转移后源容器不再含 FishA"), CatItemsServiceTransactionTest::SnapshotContainsFish(GuardSnapshot, FishA));
	TestTrue(TEXT("成功转移后目标容器含 FishA"), CatItemsServiceTransactionTest::SnapshotContainsFish(TankSnapshot, FishA));
	TestEqual(TEXT("成功转移后源 Revision 为 4"), GuardSnapshot.Revision, int64(4));
	TestEqual(TEXT("成功转移后目标 Revision 为 2"), TankSnapshot.Revision, int64(2));

	FCatDomainCommandResult ReplayResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, FishGuard, TransferRequestId, FishA, 0, 0, 3, 1));
	TestFalse(TEXT("转移重放不再次提交"), ReplayResult.bCommitted);
	TestEqual(TEXT("转移重放返回 AlreadyResolved"), ReplayResult.Error, ECatDomainCommandError::AlreadyResolved);

	const FGuid StaleRequestId = FGuid::NewGuid();
	FCatDomainCommandResult StaleResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, FishGuard, StaleRequestId, FishB, 1, 0, 4, 1));
	TestFalse(TEXT("目标 Revision 陈旧时转移不提交"), StaleResult.bCommitted);
	TestEqual(TEXT("目标 Revision 陈旧返回 RevisionConflict"), StaleResult.Error, ECatDomainCommandError::RevisionConflict);
	GuardSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId);
	TankSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, FishGuard.ContainerId);
	TestTrue(TEXT("陈旧目标 Revision 不会从源移除 FishB"), CatItemsServiceTransactionTest::SnapshotContainsFish(GuardSnapshot, FishB));
	TestFalse(TEXT("陈旧目标 Revision 不会向目标添加 FishB"), CatItemsServiceTransactionTest::SnapshotContainsFish(TankSnapshot, FishB));
	return !HasAnyErrors();
}

// 测试流程：验证直接吃鱼成功、重放和个人鱼护主人权限；同一鱼只能由主人以匹配 Revision 消费，共享/身体效果不在本测试中伪造。
bool FCatItemsConsumeFishTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 进食测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 进食测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	if (!ItemsService)
	{
		return false;
	}

	const CatItemsServiceTransactionTest::FRegisteredContainer Guard = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 3);
	const FGuid OwnedFish = FGuid::NewGuid();
	const FGuid ProtectedFish = FGuid::NewGuid();
	TestTrue(TEXT("可种入待吃鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, OwnedFish, 1).Command.bCommitted);
	TestTrue(TEXT("可种入权限测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, ProtectedFish, 2).Command.bCommitted);

	const FGuid ConsumeRequestId = FGuid::NewGuid();
	FCatFishConsumeResult ConsumeResult = ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, ConsumeRequestId, OwnedFish, 3, TEXT("PlayerA")));
	TestTrue(TEXT("主人直接吃鱼提交"), ConsumeResult.Command.bCommitted);
	TestEqual(TEXT("主人直接吃鱼返回 None"), ConsumeResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("吃鱼结果返回被消费实例"), ConsumeResult.Fish.FishInstanceId, OwnedFish);
	TestFalse(TEXT("成功吃鱼后容器移除该鱼"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), OwnedFish));

	FCatFishConsumeResult ReplayResult = ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, ConsumeRequestId, OwnedFish, 3, TEXT("PlayerA")));
	TestFalse(TEXT("吃鱼重放不再次提交"), ReplayResult.Command.bCommitted);
	TestEqual(TEXT("吃鱼重放返回 AlreadyResolved"), ReplayResult.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("吃鱼重放仍返回首次鱼实例"), ReplayResult.Fish.FishInstanceId, OwnedFish);

	FCatFishConsumeResult WrongOwnerResult = ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, FGuid::NewGuid(), ProtectedFish, 4, TEXT("PlayerB")));
	TestFalse(TEXT("非主人不能吃个人鱼护里的鱼"), WrongOwnerResult.Command.bCommitted);
	TestEqual(TEXT("非主人吃鱼返回 PermissionDenied"), WrongOwnerResult.Command.Error, ECatDomainCommandError::PermissionDenied);
	TestTrue(TEXT("权限拒绝不移除鱼"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), ProtectedFish));
	return !HasAnyErrors();
}

// 测试流程：预留一条鱼后先验证转移被锁拒绝，再提交预留；重复提交返回同一贡献与 Revision，Cancel 已提交预留不能恢复鱼。
bool FCatItemsReservationCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 预留提交测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 预留提交测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	if (!ItemsService)
	{
		return false;
	}

	const CatItemsServiceTransactionTest::FRegisteredContainer Guard = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 3);
	const CatItemsServiceTransactionTest::FRegisteredContainer Tank = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 3);
	const FGuid FishId = FGuid::NewGuid();
	TestTrue(TEXT("可种入待献祭鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishId, 1).Command.bCommitted);

	const FGuid ReservationRequestId = FGuid::NewGuid();
	FCatFishReservationResult ReserveResult = ItemsService->ReserveFish(
		CatItemsServiceTransactionTest::MakeSacrificeCommand(Guard, ReservationRequestId, FishId, 2));
	TestTrue(TEXT("首次预留成功"), ReserveResult.bReserved);
	TestEqual(TEXT("首次预留返回 None"), ReserveResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("预留推进容器 Revision"), ReserveResult.ContainerRevision, int64(3));
	TestEqual(TEXT("预留返回冻结贡献"), ReserveResult.SacrificeContribution, 3);

	FCatDomainCommandResult LockedTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, FGuid::NewGuid(), FishId, 0, 0, 3, 1));
	TestFalse(TEXT("已预留鱼不能转移"), LockedTransfer.bCommitted);
	TestEqual(TEXT("已预留鱼转移返回 InvalidPhase"), LockedTransfer.Error, ECatDomainCommandError::InvalidPhase);

	FCatFishReservationCommitResult CommitResult = ItemsService->CommitFishReservation(TEXT("PlayerA"),
		ReservationRequestId, Guard.ContainerId);
	TestTrue(TEXT("预留提交成功"), CommitResult.bCommitted);
	TestEqual(TEXT("预留提交返回 None"), CommitResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("预留提交推进 Revision"), CommitResult.ContainerRevision, int64(4));
	TestEqual(TEXT("预留提交返回冻结贡献"), CommitResult.SacrificeContribution, 3);
	TestFalse(TEXT("预留提交后鱼已不可逆移除"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishId));

	FCatFishReservationCommitResult ReplayCommit = ItemsService->CommitFishReservation(TEXT("PlayerA"),
		ReservationRequestId, Guard.ContainerId);
	TestTrue(TEXT("预留提交重放仍报告已提交"), ReplayCommit.bCommitted);
	TestEqual(TEXT("预留提交重放返回 None"), ReplayCommit.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("预留提交重放返回同一 Revision"), ReplayCommit.ContainerRevision, int64(4));
	TestEqual(TEXT("预留提交重放返回同一贡献"), ReplayCommit.SacrificeContribution, 3);

	FCatDomainCommandResult CancelCommitted = ItemsService->CancelFishReservation(TEXT("PlayerA"),
		ReservationRequestId, Guard.ContainerId);
	TestFalse(TEXT("已提交预留不能取消恢复"), CancelCommitted.bCommitted);
	TestEqual(TEXT("已提交预留取消返回 AlreadyResolved"), CancelCommitted.Error, ECatDomainCommandError::AlreadyResolved);
	return !HasAnyErrors();
}

// 测试流程：预留后取消，确认鱼仍在且可重新消费；另起一条预留后执行关门，证明开放预留被释放、新写口被 CommandsClosed 拒绝。
bool FCatItemsReservationCancelAndCloseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 预留取消测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 预留取消测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	if (!ItemsService)
	{
		return false;
	}

	const CatItemsServiceTransactionTest::FRegisteredContainer Guard = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 4);
	const FGuid FishToCancel = FGuid::NewGuid();
	const FGuid FishToClose = FGuid::NewGuid();
	const FGuid FishAfterClose = FGuid::NewGuid();
	TestTrue(TEXT("可种入取消测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishToCancel, 1).Command.bCommitted);
	TestTrue(TEXT("可种入关门测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishToClose, 2).Command.bCommitted);
	TestTrue(TEXT("可种入关门后写口测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishAfterClose, 3).Command.bCommitted);

	const FGuid CancelReservationId = FGuid::NewGuid();
	TestTrue(TEXT("可预留待取消鱼"), ItemsService->ReserveFish(
		CatItemsServiceTransactionTest::MakeSacrificeCommand(Guard, CancelReservationId, FishToCancel, 4)).bReserved);
	FCatDomainCommandResult CancelResult = ItemsService->CancelFishReservation(TEXT("PlayerA"), CancelReservationId,
		Guard.ContainerId);
	TestTrue(TEXT("取消开放预留提交"), CancelResult.bCommitted);
	TestEqual(TEXT("取消开放预留返回 Cancelled"), CancelResult.Error, ECatDomainCommandError::Cancelled);
	TestTrue(TEXT("取消预留后鱼仍在容器"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishToCancel));

	FCatFishConsumeResult ConsumeAfterCancel = ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, FGuid::NewGuid(), FishToCancel,
			CancelResult.Revision, TEXT("PlayerA")));
	TestTrue(TEXT("取消预留后鱼可被正常消费"), ConsumeAfterCancel.Command.bCommitted);

	const FCatContainerSnapshot BeforeCloseSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId);
	const FGuid CloseReservationId = FGuid::NewGuid();
	TestTrue(TEXT("可预留待关门释放鱼"), ItemsService->ReserveFish(
		CatItemsServiceTransactionTest::MakeSacrificeCommand(Guard, CloseReservationId, FishToClose,
			BeforeCloseSnapshot.Revision)).bReserved);

	ItemsService->CloseCommandsAndCancelReservations();

	FCatFishReservationCommitResult CommitAfterClose = ItemsService->CommitFishReservation(TEXT("PlayerA"),
		CloseReservationId, Guard.ContainerId);
	TestFalse(TEXT("关门释放开放预留后不能提交"), CommitAfterClose.bCommitted);
	TestEqual(TEXT("关门释放开放预留后提交返回 NotFound"), CommitAfterClose.Error, ECatDomainCommandError::NotFound);
	TestTrue(TEXT("关门释放开放预留不会移除鱼"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishToClose));

	FCatFishConsumeResult ConsumeAfterClose = ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, FGuid::NewGuid(), FishAfterClose,
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, TEXT("PlayerA")));
	TestFalse(TEXT("关门后新吃鱼命令不提交"), ConsumeAfterClose.Command.bCommitted);
	TestEqual(TEXT("关门后新吃鱼返回 CommandsClosed"), ConsumeAfterClose.Command.Error, ECatDomainCommandError::CommandsClosed);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
