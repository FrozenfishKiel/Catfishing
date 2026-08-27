#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatSacrificeCoordinator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSacrificeCoordinatorTeardownGateTest,
	"Catfishing.Unit.Run.SacrificeCoordinator.TeardownClosesNewSacrificeCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSacrificeCoordinatorItemsCommittedRecoveryTest,
	"Catfishing.Unit.Run.SacrificeCoordinator.ItemsCommittedRecoveryOnlyRetriesRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatSacrificeCoordinatorTest
{
	/** 本测试的稳定玩家身份；它同时用于 PlayerState UniqueId、鱼捕获者和献祭协议键。 */
	static const FString StableNetIdValue(TEXT("CatSacrificeCoordinatorStableNetId"));

	/** 已注册测试容器的公开上下文；测试只通过 ItemsService 写口读写它，不绕过容器内部真相。 */
	struct FRegisteredContainer
	{
		/** 承载复制组件的 authority Actor；它给容器提供真实 World 生命周期。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** ItemsService 发布快照用的正式复制组件；测试通过它验证注册路径没有被私造。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本局容器稳定 ID；献祭命令用它绑定 Items 聚合。 */
		FGuid ContainerId;

		/** 鱼实例的服务器私有捕获者；PlayerState 与 Items 授权都使用同一身份字符串。 */
		FString OwnerStableNetId;
	};

	// 容器注册流程：生成真实 Actor 与复制组件，再交给 ItemsService 注册；失败时返回的上下文保持可被测试显式断言。
	static FRegisteredContainer RegisterContainer(UWorld* World, UCatItemsService* ItemsService,
		const ECatContainerKind Kind, const FString& OwnerStableNetId, const int32 Capacity)
	{
		FRegisteredContainer Result;
		Result.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Result.Component = Result.Owner ? NewObject<UCatContainerReplicationComponent>(Result.Owner) : nullptr;
		Result.ContainerId = FGuid::NewGuid();
		Result.OwnerStableNetId = OwnerStableNetId;
		if (Result.Owner && Result.Component && ItemsService)
		{
			Result.Owner->AddInstanceComponent(Result.Component);
			Result.Component->RegisterComponent();
			ItemsService->RegisterContainer(Result.Component, Result.ContainerId, Kind, OwnerStableNetId, Capacity);
		}
		return Result;
	}

	// 捕获命令流程：构造一条能进入正式 Items 捕获提交的命令；贡献值固定为 3，便于随后核对 Run 只加一次。
	static FCatCaptureCommitCommand MakeCaptureCommand(const FRegisteredContainer& TargetContainer,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = TargetContainer.OwnerStableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = TEXT("SacrificeRecoveryFish");
		Command.TargetContainerId = TargetContainer.ContainerId;
		Command.WeightKilograms = 2.5;
		Command.SacrificeContribution = 3;
		return Command;
	}

	// 种鱼流程：通过 CommitCapture 创建真实 FishInstance；测试不直接改容器数组，避免绕过 Items 幂等和 Revision 规则。
	static FCatCaptureCommitResult SeedFish(UCatItemsService* ItemsService, const FRegisteredContainer& TargetContainer,
		const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		return ItemsService ? ItemsService->CommitCapture(
			MakeCaptureCommand(TargetContainer, FGuid::NewGuid(), FishInstanceId, ExpectedRevision))
			: FCatCaptureCommitResult();
	}

	// 快照读取流程：走 ItemsService 公开只读接口；读取失败时返回默认快照，让调用方用断言暴露缺口。
	static FCatContainerSnapshot GetSnapshot(UCatItemsService* ItemsService, const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		if (ItemsService)
		{
			ItemsService->TryGetContainerSnapshot(ContainerId, Snapshot);
		}
		return Snapshot;
	}

	// 查询流程：只按公开 Snapshot 判断目标鱼是否仍存在；不读取 Items 私有预留表或终态缓存。
	static bool SnapshotContainsFish(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	// 献祭命令流程：把 Items Revision 与 Run Revision 分别写入合同，覆盖跨聚合最容易混淆的两个版本号。
	static FCatSacrificeCommand MakeSacrificeCommand(const FRegisteredContainer& Container,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedItemsRevision,
		const int64 ExpectedRunRevision)
	{
		FCatSacrificeCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedItemsRevision;
		Command.Context.StableNetId = TEXT("ClientSuppliedIdentityMustBeIgnored");
		Command.FishInstanceId = FishInstanceId;
		Command.ContainerId = Container.ContainerId;
		Command.ExpectedRunRevision = ExpectedRunRevision;
		return Command;
	}

	/** 自动化 Run apply hook 的生命周期守卫；构造时安装，析构时清空，避免失败用例污染后续测试。 */
	struct FRunApplyOverrideScope
	{
		/** 构造流程：把调用方提供的替代器注册到 SacrificeCoordinator 的测试 seam。 */
		explicit FRunApplyOverrideScope(UCatSacrificeCoordinator::FRunApplyOverrideForAutomation Hook)
		{
			UCatSacrificeCoordinator::SetRunApplyOverrideForAutomation(MoveTemp(Hook));
		}

		/** 析构流程：无论测试成功或失败都移除替代器，后续用例重新走真实 GameMode 写口。 */
		~FRunApplyOverrideScope()
		{
			UCatSacrificeCoordinator::SetRunApplyOverrideForAutomation({});
		}
	};

	/** A3 恢复用例的最小真实世界夹具；只种入 DayActive、玩家身份、地面鱼护和一条鱼。 */
	struct FItemsCommittedRecoveryFixture
	{
		/** 自动化测试世界；GameMode、Subsystem、Actor 都在这里创建并随夹具销毁。 */
		FTestWorldWrapper WorldWrapper;

		/** 当前测试 World 指针；CreateWorld 成功后可用，不拥有生命周期。 */
		UWorld* World = nullptr;

		/** Lake GameMode 权威实例；夹具只写入 DayActive 公开状态和命令 gate。 */
		ACatfishingGameModeBase* GameMode = nullptr;

		/** 真实玩家 Controller；献祭协调器从它的 PlayerState 解析服务器身份。 */
		APlayerController* Controller = nullptr;

		/** 真实 PlayerState；唯一用途是承载稳定 UniqueId。 */
		APlayerState* PlayerState = nullptr;

		/** Items 世界服务；容器注册、种鱼、预留和不可逆提交都走它的正式接口。 */
		UCatItemsService* ItemsService = nullptr;

		/** 一局献祭协调器；测试只通过 RequestSacrifice 入口触发跨聚合协议。 */
		UCatSacrificeCoordinator* Coordinator = nullptr;

		/** 测试地面鱼护；它是被献祭鱼所在的真实 Items 容器。 */
		FRegisteredContainer Guard;

		/** 被献祭鱼实例 ID；种鱼、献祭和快照断言都使用同一个不可变标识。 */
		FGuid FishInstanceId;

		/** 世界创建流程：启动 Lake GameMode，再取得 Items 与 Sacrifice 两个真实 WorldSubsystem。 */
		bool CreateWorld(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("Create Sacrifice recovery Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
			{
				return false;
			}
			World = WorldWrapper.GetTestWorld();
			if (!Test.TestNotNull(TEXT("Read Sacrifice recovery World"), World))
			{
				return false;
			}
			World->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
			WorldWrapper.BeginPlayInTestWorld();
			GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
			ItemsService = World->GetSubsystem<UCatItemsService>();
			Coordinator = World->GetSubsystem<UCatSacrificeCoordinator>();
			ConfigureDayActiveRun();
			return Test.TestNotNull(TEXT("Spawn Lake GameMode for Sacrifice recovery"), GameMode)
				&& Test.TestNotNull(TEXT("Create ItemsService for Sacrifice recovery"), ItemsService)
				&& Test.TestNotNull(TEXT("Create SacrificeCoordinator for recovery"), Coordinator);
		}

		/** 玩家创建流程：生成 Controller 与 PlayerState，并把稳定 UniqueId 写入 PlayerState 供协调器冻结身份。 */
		bool SpawnPlayer(FAutomationTestBase& Test)
		{
			Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
			PlayerState = World ? World->SpawnActor<APlayerState>() : nullptr;
			if (!Test.TestNotNull(TEXT("Spawn Sacrifice recovery Controller"), Controller)
				|| !Test.TestNotNull(TEXT("Spawn Sacrifice recovery PlayerState"), PlayerState))
			{
				return false;
			}
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetIdValue, FName(TEXT("CAT_TEST")));
			PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			Controller->PlayerState = PlayerState;
			return Test.TestTrue(TEXT("Sacrifice recovery PlayerState has stable UniqueId"), PlayerState->GetUniqueId().IsValid());
		}

		/** Run 状态种入流程：直接写入测试用 DayActive 快照；不启动 StateTree，目标值设高以避免达标事件依赖。 */
		void ConfigureDayActiveRun()
		{
			if (!GameMode)
			{
				return;
			}
			GameMode->RunPublicState = FCatRunPublicState();
			GameMode->RunPublicState.Phase.RunId = FGuid::NewGuid();
			GameMode->RunPublicState.Phase.DayIndex = 1;
			GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
			GameMode->RunPublicState.Phase.ServerTimeAnchorSeconds = World ? World->GetTimeSeconds() : 0.0;
			GameMode->RunPublicState.Phase.DeadlineServerTimeSeconds =
				GameMode->RunPublicState.Phase.ServerTimeAnchorSeconds + 120.0;
			GameMode->RunPublicState.Phase.bHasDeadline = true;
			GameMode->RunPublicState.Phase.bFishingAllowed = true;
			GameMode->RunPublicState.Phase.bQuotaOpen = true;
			GameMode->RunPublicState.QuotaProgress = 0;
			GameMode->RunPublicState.QuotaTarget = 10;
			GameMode->RunPublicState.Revision = 1;
			GameMode->RunPublicState.EndReason = ECatRunEndReason::None;
			GameMode->RunPublicState.bTeardownComplete = false;
			GameMode->bRunCommandsOpen = true;
		}

		/** 鱼护种入流程：注册地面鱼护并通过 CommitCapture 放入一条真实鱼，返回提交后的 Items Revision。 */
		int64 SeedFishGuard(FAutomationTestBase& Test)
		{
			Guard = RegisterContainer(World, ItemsService, ECatContainerKind::FishGuard, StableNetIdValue, 3);
			if (!Test.TestNotNull(TEXT("Sacrifice recovery Guard actor exists"), Guard.Owner.Get())
				|| !Test.TestNotNull(TEXT("Sacrifice recovery Guard component exists"), Guard.Component.Get()))
			{
				return 0;
			}
			FishInstanceId = FGuid::NewGuid();
			const FCatCaptureCommitResult SeedResult = SeedFish(ItemsService, Guard, FishInstanceId, 1);
			Test.TestTrue(TEXT("Seed fish into personal guard for recovery"), SeedResult.Command.bCommitted);
			const FCatContainerSnapshot Snapshot = GetSnapshot(ItemsService, Guard.ContainerId);
			Test.TestEqual(TEXT("Seeded guard has one fish"), Snapshot.Fish.Num(), 1);
			Test.TestTrue(TEXT("Seeded guard contains recovery fish"), SnapshotContainsFish(Snapshot, FishInstanceId));
			return Snapshot.Revision;
		}
	};
}

// 测试流程：从真实 Game World 取得 SacrificeCoordinator，先执行空协议 teardown，再提交一条结构完整但无 Controller 的命令；关闭后的错误必须优先暴露 CommandsClosed。
bool FCatSacrificeCoordinatorTeardownGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 SacrificeCoordinator 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatSacrificeCoordinator* Coordinator = World->GetSubsystem<UCatSacrificeCoordinator>();
	TestNotNull(TEXT("可取得 SacrificeCoordinator"), Coordinator);
	if (!Coordinator)
	{
		return false;
	}

	TestTrue(TEXT("无活跃协议时 teardown 可以收口"), Coordinator->PrepareForRunTeardown());

	FCatSacrificeCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = 1;
	Command.FishInstanceId = FGuid::NewGuid();
	Command.ContainerId = FGuid::NewGuid();
	Command.ExpectedRunRevision = 1;
	const FCatSacrificeResult Result = Coordinator->RequestSacrifice(nullptr, Command);
	TestFalse(TEXT("teardown 后不接受新献祭"), Result.bCompleted);
	TestEqual(TEXT("teardown 后返回 CommandsClosed"), Result.Error, ECatDomainCommandError::CommandsClosed);
	TestEqual(TEXT("拒绝结果仍关联原 RequestId"), Result.RequestId, Command.Context.RequestId);
	return !HasAnyErrors();
}

// 测试流程：先让真实 Items 完成不可逆提交，再注入一次 Run apply 失败；同 RequestId 重试必须只补 Run，不还鱼、不重复扣鱼、不重复加额度。
bool FCatSacrificeCoordinatorItemsCommittedRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSacrificeCoordinatorTest::FItemsCommittedRecoveryFixture Fixture;
	if (!Fixture.CreateWorld(*this) || !Fixture.SpawnPlayer(*this))
	{
		return false;
	}
	const int64 SeedItemsRevision = Fixture.SeedFishGuard(*this);
	if (SeedItemsRevision <= 0)
	{
		return false;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const int64 StartingRunRevision = Fixture.GameMode->GetRunPublicState().Revision;
	const int32 StartingQuotaProgress = Fixture.GameMode->GetRunPublicState().QuotaProgress;
	const FCatSacrificeCommand Command = CatSacrificeCoordinatorTest::MakeSacrificeCommand(
		Fixture.Guard, RequestId, Fixture.FishInstanceId, SeedItemsRevision, StartingRunRevision);
	int32 RunApplyAttempts = 0;
	CatSacrificeCoordinatorTest::FRunApplyOverrideScope OverrideScope(
		[&Fixture, &RunApplyAttempts](UCatSacrificeCoordinator& Coordinator,
			const FCatQuotaContributionCommand& QuotaCommand) -> TOptional<FCatRunCommandResult>
		{
			(void)Coordinator;
			++RunApplyAttempts;
			if (RunApplyAttempts != 1)
			{
				return TOptional<FCatRunCommandResult>();
			}

			FCatRunCommandResult Result;
			Result.bCommitted = false;
			Result.RequestId = QuotaCommand.Context.RequestId;
			Result.Error = ECatRunCommandError::StateTreeUnavailable;
			Result.Revision = Fixture.GameMode ? Fixture.GameMode->GetRunPublicState().Revision : 0;
			Result.Phase = Fixture.GameMode ? Fixture.GameMode->GetRunPublicState().Phase.Phase : ECatRunPhase::NotStarted;
			return TOptional<FCatRunCommandResult>(Result);
		});

	const FCatSacrificeResult FirstResult = Fixture.Coordinator->RequestSacrifice(Fixture.Controller, Command);
	TestFalse(TEXT("首次 Run apply 注入失败不完成献祭"), FirstResult.bCompleted);
	TestEqual(TEXT("首次失败停留在 ItemsCommitted"), FirstResult.Stage, ECatSacrificeStage::ItemsCommitted);
	TestEqual(TEXT("首次失败暴露依赖不可用"), FirstResult.Error, ECatDomainCommandError::DependencyUnavailable);
	TestEqual(TEXT("首次结果保持 RequestId"), FirstResult.RequestId, RequestId);
	TestEqual(TEXT("首次结果冻结献祭贡献"), FirstResult.AppliedContribution, 3);
	TestTrue(TEXT("首次失败已经推进 Items Revision"), FirstResult.ItemsRevision > SeedItemsRevision);
	TestEqual(TEXT("首次失败没有推进 Run Revision"), Fixture.GameMode->GetRunPublicState().Revision, StartingRunRevision);
	TestEqual(TEXT("首次失败没有增加 Run 额度"), Fixture.GameMode->GetRunPublicState().QuotaProgress, StartingQuotaProgress);
	const FCatContainerSnapshot AfterFirstSnapshot =
		CatSacrificeCoordinatorTest::GetSnapshot(Fixture.ItemsService, Fixture.Guard.ContainerId);
	TestFalse(TEXT("ItemsCommitted 后鱼已不可逆移除"), CatSacrificeCoordinatorTest::SnapshotContainsFish(
		AfterFirstSnapshot, Fixture.FishInstanceId));
	TestEqual(TEXT("首次失败结果记录 Items 提交 Revision"), FirstResult.ItemsRevision, AfterFirstSnapshot.Revision);
	TestEqual(TEXT("首次请求进入一次 Run apply"), RunApplyAttempts, 1);

	const FCatSacrificeResult RecoveryResult = Fixture.Coordinator->RequestSacrifice(Fixture.Controller, Command);
	TestTrue(TEXT("同 RequestId 重试补齐 Run apply"), RecoveryResult.bCompleted);
	TestEqual(TEXT("补齐后进入 Completed"), RecoveryResult.Stage, ECatSacrificeStage::Completed);
	TestEqual(TEXT("补齐后清空错误"), RecoveryResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("补齐结果保持 RequestId"), RecoveryResult.RequestId, RequestId);
	TestEqual(TEXT("补齐结果沿用同一献祭贡献"), RecoveryResult.AppliedContribution, FirstResult.AppliedContribution);
	TestEqual(TEXT("补齐结果沿用同一 Items Revision"), RecoveryResult.ItemsRevision, FirstResult.ItemsRevision);
	TestEqual(TEXT("补齐只增加一次 Run 额度"), Fixture.GameMode->GetRunPublicState().QuotaProgress,
		StartingQuotaProgress + FirstResult.AppliedContribution);
	TestEqual(TEXT("补齐只推进一次 Run Revision"), Fixture.GameMode->GetRunPublicState().Revision,
		StartingRunRevision + 1);
	TestEqual(TEXT("补齐时第二次进入 Run apply 并落到真实 GameMode 写口"), RunApplyAttempts, 2);

	const int64 CompletedRunRevision = Fixture.GameMode->GetRunPublicState().Revision;
	const int32 CompletedQuotaProgress = Fixture.GameMode->GetRunPublicState().QuotaProgress;
	const FCatSacrificeResult ReplayResult = Fixture.Coordinator->RequestSacrifice(Fixture.Controller, Command);
	TestTrue(TEXT("Completed 重放仍报告完成"), ReplayResult.bCompleted);
	TestEqual(TEXT("Completed 重放不改变阶段"), ReplayResult.Stage, ECatSacrificeStage::Completed);
	TestEqual(TEXT("Completed 重放不再次进入 Run apply"), RunApplyAttempts, 2);
	TestEqual(TEXT("Completed 重放不重复增加 Run 额度"), Fixture.GameMode->GetRunPublicState().QuotaProgress,
		CompletedQuotaProgress);
	TestEqual(TEXT("Completed 重放不重复推进 Run Revision"), Fixture.GameMode->GetRunPublicState().Revision,
		CompletedRunRevision);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
