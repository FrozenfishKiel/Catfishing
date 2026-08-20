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

	// 转移命令流程：从当前测试容器上下文构造正式 TransferOwnedFish 命令；源/目标 Revision 都由调用方明确传入。
	static FCatFishTransferCommand MakeTransferCommand(const FRegisteredContainer& SourceContainer,
		const FRegisteredContainer& TargetContainer, const FGuid RequestId, const FGuid FishInstanceId,
		const int64 ExpectedSourceRevision, const int64 ExpectedTargetRevision)
	{
		FCatFishTransferCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedSourceRevision;
		Command.Context.StableNetId = SourceContainer.OwnerStableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.SourceContainerId = SourceContainer.ContainerId;
		Command.TargetContainerId = TargetContainer.ContainerId;
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

	// 售卖冻结命令流程：构造商店售卖第一阶段所需的命令；身份由调用方给出，用于覆盖主人授权与共用鱼缸口径。
	static FCatFishSaleHoldCommand MakeSaleHoldCommand(const FRegisteredContainer& Container,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision, const FString& StableNetId)
	{
		FCatFishSaleHoldCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.ContainerId = Container.ContainerId;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsFishSaleHoldTest,
	"Catfishing.Unit.Items.PrepareFishSale.HoldCommitCancelAndReplayContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsFishSaleHoldPermissionTest,
	"Catfishing.Unit.Items.PrepareFishSale.GuardOwnerOnlyAndSharedTankIsTeamReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsNegativeSacrificeContributionTest,
	"Catfishing.Unit.Items.SacrificeContribution.NegativeValueSurvivesCaptureTransferAndCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先通过 CommitCapture 种入个人鱼护，再执行转移到共享鱼缸；重放不能二次移动，目标 Revision 陈旧时两边容器必须保持原样。
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
	const CatItemsServiceTransactionTest::FRegisteredContainer Tank = CatItemsServiceTransactionTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 3);
	TestNotNull(TEXT("个人鱼护组件已创建"), Guard.Component.Get());
	TestNotNull(TEXT("共享鱼缸组件已创建"), Tank.Component.Get());

	const FGuid FishA = FGuid::NewGuid();
	const FGuid FishB = FGuid::NewGuid();
	TestTrue(TEXT("第一条鱼种入个人鱼护"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishA, 1).Command.bCommitted);
	TestTrue(TEXT("第二条鱼种入个人鱼护"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishB, 2).Command.bCommitted);

	const FGuid TransferRequestId = FGuid::NewGuid();
	FCatDomainCommandResult TransferResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, TransferRequestId, FishA, 3, 1));
	TestTrue(TEXT("首次转移提交"), TransferResult.bCommitted);
	TestEqual(TEXT("首次转移返回 None"), TransferResult.Error, ECatDomainCommandError::None);

	FCatContainerSnapshot GuardSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId);
	FCatContainerSnapshot TankSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId);
	TestFalse(TEXT("成功转移后源容器不再含 FishA"), CatItemsServiceTransactionTest::SnapshotContainsFish(GuardSnapshot, FishA));
	TestTrue(TEXT("成功转移后目标容器含 FishA"), CatItemsServiceTransactionTest::SnapshotContainsFish(TankSnapshot, FishA));
	TestEqual(TEXT("成功转移后源 Revision 为 4"), GuardSnapshot.Revision, int64(4));
	TestEqual(TEXT("成功转移后目标 Revision 为 2"), TankSnapshot.Revision, int64(2));

	FCatDomainCommandResult ReplayResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, TransferRequestId, FishA, 3, 1));
	TestFalse(TEXT("转移重放不再次提交"), ReplayResult.bCommitted);
	TestEqual(TEXT("转移重放返回 AlreadyResolved"), ReplayResult.Error, ECatDomainCommandError::AlreadyResolved);

	const FGuid StaleRequestId = FGuid::NewGuid();
	FCatDomainCommandResult StaleResult = ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, StaleRequestId, FishB, 4, 1));
	TestFalse(TEXT("目标 Revision 陈旧时转移不提交"), StaleResult.bCommitted);
	TestEqual(TEXT("目标 Revision 陈旧返回 RevisionConflict"), StaleResult.Error, ECatDomainCommandError::RevisionConflict);
	GuardSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId);
	TankSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId);
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
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, FGuid::NewGuid(), FishId, 3, 1));
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

// 测试流程：走完商店售卖的两阶段协议——先冻结（鱼还在容器里，不算价、不碰钱包），再 drain（鱼不可逆消失）。
// 锁的不变量：冻结成功不删鱼；同一 RequestId 重放只读首次终态；载荷漂移拿不到鱼也改不了状态；
// 换个身份不能解锁别人的冻结；drain 之后不能再回退；单独一条鱼验证取消后它真的恢复可吃，
// 并且取消之后同一个 RequestId 再来一次只能得到 Cancelled，不能得到一份"成功 + 空鱼"的过期终态。
bool FCatItemsFishSaleHoldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 售卖冻结测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 售卖冻结测试 World"), World);
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
	TestNotNull(TEXT("个人鱼护组件已创建"), Guard.Component.Get());

	const FGuid FishSold = FGuid::NewGuid();
	const FGuid FishKept = FGuid::NewGuid();
	const FGuid FishDrift = FGuid::NewGuid();
	TestTrue(TEXT("可种入待售鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishSold,
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision).Command.bCommitted);
	TestTrue(TEXT("可种入取消测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishKept,
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision).Command.bCommitted);
	TestTrue(TEXT("可种入载荷漂移测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishDrift,
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision).Command.bCommitted);

	const int64 RevisionBeforeHold = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FGuid SaleRequestId = FGuid::NewGuid();
	const FCatFishSaleHoldResult Prepared = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, SaleRequestId, FishSold, RevisionBeforeHold, TEXT("PlayerA")));
	TestTrue(TEXT("首次售卖冻结提交"), Prepared.Command.bCommitted);
	TestEqual(TEXT("首次售卖冻结返回 None"), Prepared.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("售卖冻结返回目标鱼供商店定价"), Prepared.Fish.FishInstanceId, FishSold);
	TestEqual(TEXT("售卖冻结推进容器 Revision"), Prepared.Command.Revision, RevisionBeforeHold + 1);
	TestTrue(TEXT("冻结阶段鱼仍在容器里，没有被提前删除"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishSold));

	const FCatFishSaleHoldResult PreparedReplay = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, SaleRequestId, FishSold, RevisionBeforeHold, TEXT("PlayerA")));
	TestFalse(TEXT("售卖冻结重放不再次提交"), PreparedReplay.Command.bCommitted);
	TestEqual(TEXT("售卖冻结重放返回 AlreadyResolved"), PreparedReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("售卖冻结重放仍返回首次那条鱼"), PreparedReplay.Fish.FishInstanceId, FishSold);
	TestEqual(TEXT("售卖冻结重放返回首次 Revision"), PreparedReplay.Command.Revision, Prepared.Command.Revision);

	const int64 RevisionBeforeDrift = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FCatFishSaleHoldResult DriftResult = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, SaleRequestId, FishDrift, RevisionBeforeHold, TEXT("PlayerA")));
	TestFalse(TEXT("同 RequestId 换鱼不提交"), DriftResult.Command.bCommitted);
	TestEqual(TEXT("同 RequestId 换鱼返回 InvalidPayload"), DriftResult.Command.Error, ECatDomainCommandError::InvalidPayload);
	TestFalse(TEXT("载荷漂移的请求拿不到任何鱼事实"), DriftResult.Fish.FishInstanceId.IsValid());
	TestEqual(TEXT("载荷漂移不推进容器 Revision"),
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, RevisionBeforeDrift);

	const FCatFishSaleHoldResult WrongIdentityCancel = ItemsService->CancelPreparedFishSale(TEXT("PlayerB"),
		Guard.ContainerId, SaleRequestId);
	TestFalse(TEXT("换身份不能取消别人的售卖冻结"), WrongIdentityCancel.Command.bCommitted);
	TestEqual(TEXT("换身份取消返回 NotFound"), WrongIdentityCancel.Command.Error, ECatDomainCommandError::NotFound);

	const int64 RevisionBeforeDrain = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FCatFishSaleHoldResult Drained = ItemsService->CommitPreparedFishSale(TEXT("PlayerA"), Guard.ContainerId, SaleRequestId);
	TestTrue(TEXT("售卖 drain 提交"), Drained.Command.bCommitted);
	TestEqual(TEXT("售卖 drain 返回 None"), Drained.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("售卖 drain 返回被移除的鱼"), Drained.Fish.FishInstanceId, FishSold);
	TestEqual(TEXT("售卖 drain 推进容器 Revision"), Drained.Command.Revision, RevisionBeforeDrain + 1);
	TestFalse(TEXT("售卖 drain 后鱼已不可逆离开容器"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishSold));

	const FCatFishSaleHoldResult DrainReplay = ItemsService->CommitPreparedFishSale(TEXT("PlayerA"), Guard.ContainerId, SaleRequestId);
	TestFalse(TEXT("售卖 drain 重放不再次移除"), DrainReplay.Command.bCommitted);
	TestEqual(TEXT("售卖 drain 重放返回 AlreadyResolved"), DrainReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("售卖 drain 重放返回首次 Revision"), DrainReplay.Command.Revision, Drained.Command.Revision);
	TestEqual(TEXT("售卖 drain 重放返回同一条鱼"), DrainReplay.Fish.FishInstanceId, FishSold);

	const FCatFishSaleHoldResult CancelAfterDrain = ItemsService->CancelPreparedFishSale(TEXT("PlayerA"),
		Guard.ContainerId, SaleRequestId);
	TestFalse(TEXT("已 drain 的售卖不能回退"), CancelAfterDrain.Command.bCommitted);
	TestEqual(TEXT("已 drain 的售卖回退返回 AlreadyResolved"), CancelAfterDrain.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("回退失败也不会把已卖出的鱼变回来"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishSold));

	const FGuid KeptSaleRequestId = FGuid::NewGuid();
	// 这一段的重放要拿同一个 ExpectedRevision 再发一次，所以先把冻结用的那个版本号留下来：
	// 换个版本号发出去就变成载荷漂移，测的是另一条分支，验不到"取消之后重放"这件事。
	const int64 RevisionBeforeKeptHold = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	TestTrue(TEXT("可冻结第二条鱼用于验证回退"), ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, KeptSaleRequestId, FishKept,
			RevisionBeforeKeptHold, TEXT("PlayerA"))).Command.bCommitted);
	const FCatFishSaleHoldResult Cancelled = ItemsService->CancelPreparedFishSale(TEXT("PlayerA"),
		Guard.ContainerId, KeptSaleRequestId);
	TestTrue(TEXT("未 drain 的售卖可以回退"), Cancelled.Command.bCommitted);
	TestEqual(TEXT("售卖回退返回 Cancelled"), Cancelled.Command.Error, ECatDomainCommandError::Cancelled);
	TestTrue(TEXT("回退后鱼仍在容器"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishKept));

	// 回归：解冻之后同一个 RequestId 又发一次冻结。终态缓存里还留着当初那句"冻结成功"，但那把锁早已不存在，
	// 重放它就会给出"成功 + 一条全零的鱼"——商店链拿到这样一条鱼会用 0 千克去估价，全靠下游价格表兜底才没出事。
	// 这里锁住的正确答复是：明说这个请求已经被取消，不给冻结事实，也不给鱼。
	const int64 RevisionBeforeCancelledReplay = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FCatFishSaleHoldResult ReplayAfterCancel = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, KeptSaleRequestId, FishKept,
			RevisionBeforeKeptHold, TEXT("PlayerA")));
	TestFalse(TEXT("取消后同 RequestId 重放不再声称冻结成立"), ReplayAfterCancel.Command.bCommitted);
	TestEqual(TEXT("取消后同 RequestId 重放返回 Cancelled"),
		ReplayAfterCancel.Command.Error, ECatDomainCommandError::Cancelled);
	TestFalse(TEXT("取消后重放绝不能带出一条空鱼冒充冻结事实"), ReplayAfterCancel.Fish.FishInstanceId.IsValid());
	TestEqual(TEXT("取消后重放不推进容器 Revision"),
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
		RevisionBeforeCancelledReplay);
	TestTrue(TEXT("取消后重放也没有偷偷把鱼重新锁回去"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId), FishKept));

	TestTrue(TEXT("回退真的解开了锁：这条鱼可以被正常吃掉"), ItemsService->ConsumeFish(
		CatItemsServiceTransactionTest::MakeConsumeCommand(Guard, FGuid::NewGuid(), FishKept,
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			TEXT("PlayerA"))).Command.bCommitted);
	return !HasAnyErrors();
}

// 测试流程：验证售卖冻结的两条权限口径——个人鱼护里的鱼只有它的主人能拿去卖，共用鱼缸是团队储备，任何人都能卖且不要求原钓手在场。
// 顺带锁住陈旧 ExpectedRevision 必须以 RevisionConflict 拒绝，商店不能拿旧快照去冻结。
bool FCatItemsFishSaleHoldPermissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 售卖权限测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 售卖权限测试 World"), World);
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
	TestTrue(TEXT("可种入权限测试鱼"), CatItemsServiceTransactionTest::SeedFish(ItemsService, Guard, FishId,
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision).Command.bCommitted);

	const int64 GuardRevision = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FCatFishSaleHoldResult OtherPlayerHold = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Guard, FGuid::NewGuid(), FishId, GuardRevision, TEXT("PlayerB")));
	TestFalse(TEXT("别人不能卖个人鱼护里的鱼"), OtherPlayerHold.Command.bCommitted);
	TestEqual(TEXT("非主人售卖返回 PermissionDenied"), OtherPlayerHold.Command.Error, ECatDomainCommandError::PermissionDenied);
	TestEqual(TEXT("权限拒绝不推进容器 Revision"),
		CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, GuardRevision);

	TestTrue(TEXT("主人把鱼存进共用鱼缸"), ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, FGuid::NewGuid(), FishId, GuardRevision,
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision)).bCommitted);

	const int64 TankRevision = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision;
	const FCatFishSaleHoldResult StaleTankHold = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Tank, FGuid::NewGuid(), FishId, TankRevision - 1, TEXT("PlayerB")));
	TestFalse(TEXT("陈旧 Revision 不能冻结"), StaleTankHold.Command.bCommitted);
	TestEqual(TEXT("陈旧 Revision 返回 RevisionConflict"), StaleTankHold.Command.Error, ECatDomainCommandError::RevisionConflict);

	const FCatFishSaleHoldResult TankHold = ItemsService->PrepareFishSale(
		CatItemsServiceTransactionTest::MakeSaleHoldCommand(Tank, FGuid::NewGuid(), FishId, TankRevision, TEXT("PlayerB")));
	TestTrue(TEXT("共用鱼缸是团队储备，原钓手不在场别人也能卖"), TankHold.Command.bCommitted);
	TestEqual(TEXT("共用鱼缸售卖返回 None"), TankHold.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("共用鱼缸售卖返回目标鱼"), TankHold.Fish.FishInstanceId, FishId);
	return !HasAnyErrors();
}

// 测试流程：用 SacrificeContribution = -1 的臭臭鱼走完捕获、转移、预留、提交四步，确认这个负的世界进度增量在 Items 侧一路原样传递，
// 没有任何环节把它当成非法值或夹回 0。顺带确认真正的非法值仍然是 0：0 意味着调用方压根没带定义值。
bool FCatItemsNegativeSacrificeContributionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 负供奉值测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 负供奉值测试 World"), World);
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

	const FGuid StinkyFish = FGuid::NewGuid();
	FCatCaptureCommitCommand StinkyCapture = CatItemsServiceTransactionTest::MakeCaptureCommand(Guard, FGuid::NewGuid(),
		StinkyFish, CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision);
	StinkyCapture.FishDefinitionId = TEXT("StinkyFish");
	StinkyCapture.SacrificeContribution = -1;
	const FCatCaptureCommitResult StinkyResult = ItemsService->CommitCapture(StinkyCapture);
	TestTrue(TEXT("负供奉值的鱼可以被捕获"), StinkyResult.Command.bCommitted);
	TestEqual(TEXT("捕获事实冻结 -1 供奉值"), StinkyResult.Committed.FishInstance.SacrificeContribution, -1);

	FCatCaptureCommitCommand ZeroCapture = CatItemsServiceTransactionTest::MakeCaptureCommand(Guard, FGuid::NewGuid(),
		FGuid::NewGuid(), CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision);
	ZeroCapture.SacrificeContribution = 0;
	const FCatCaptureCommitResult ZeroResult = ItemsService->CommitCapture(ZeroCapture);
	TestFalse(TEXT("供奉值为 0 仍然是非法载荷"), ZeroResult.Command.bCommitted);
	TestEqual(TEXT("供奉值为 0 返回 InvalidPayload"), ZeroResult.Command.Error, ECatDomainCommandError::InvalidPayload);

	const int64 GuardRevision = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	TestTrue(TEXT("负供奉值的鱼可以转移进共用鱼缸"), ItemsService->TransferOwnedFish(
		CatItemsServiceTransactionTest::MakeTransferCommand(Guard, Tank, FGuid::NewGuid(), StinkyFish, GuardRevision,
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision)).bCommitted);
	const FCatContainerSnapshot TankSnapshot = CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId);
	const FCatFishInstance* TankFish = TankSnapshot.Fish.FindByPredicate([StinkyFish](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == StinkyFish;
	});
	TestNotNull(TEXT("转移后共用鱼缸持有臭臭鱼"), TankFish);
	if (TankFish)
	{
		TestEqual(TEXT("转移不改变 -1 供奉值"), TankFish->SacrificeContribution, -1);
	}

	FCatSacrificeCommand SacrificeCommand;
	SacrificeCommand.Context.RequestId = FGuid::NewGuid();
	SacrificeCommand.Context.ExpectedRevision = TankSnapshot.Revision;
	SacrificeCommand.Context.StableNetId = TEXT("PlayerA");
	SacrificeCommand.FishInstanceId = StinkyFish;
	SacrificeCommand.ContainerId = Tank.ContainerId;
	SacrificeCommand.ExpectedRunRevision = 1;
	const FCatFishReservationResult Reserved = ItemsService->ReserveFish(SacrificeCommand);
	TestTrue(TEXT("负供奉值的鱼可以被献祭预留"), Reserved.bReserved);
	TestEqual(TEXT("预留返回 -1 供奉值"), Reserved.SacrificeContribution, -1);

	const FCatFishReservationCommitResult Committed = ItemsService->CommitFishReservation(TEXT("PlayerA"),
		SacrificeCommand.Context.RequestId, Tank.ContainerId);
	TestTrue(TEXT("负供奉值的预留可以提交"), Committed.bCommitted);
	TestEqual(TEXT("提交返回 -1 供奉值"), Committed.SacrificeContribution, -1);
	TestFalse(TEXT("提交后臭臭鱼离开共用鱼缸"),
		CatItemsServiceTransactionTest::SnapshotContainsFish(
			CatItemsServiceTransactionTest::GetSnapshot(ItemsService, Tank.ContainerId), StinkyFish));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
