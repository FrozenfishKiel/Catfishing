#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Engine/Player.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "Social/CatSocialService.h"
#include "Social/CatSocialSettings.h"

namespace CatItemsServiceConcurrencyTest
{
	/** 已注册测试容器的最小上下文；它代表真实 Actor 宿主、复制组件、稳定容器 ID 与注册身份。 */
	struct FRegisteredContainer
	{
		/** 容器所在的 authority Actor；偷鱼距离判定读它的位置。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** Items 服务发布快照的正式复制组件；测试只读它，不绕过服务写数组。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本局容器稳定 ID；所有 Items 命令都以它绑定聚合。 */
		FGuid ContainerId;

		/** 容器服务器私有主人；个人鱼护授权和鱼 OwnerStableNetId 都使用同一个值。 */
		FString OwnerStableNetId;
	};

	/** 偷鱼测试玩家上下文；Social 只从 Controller 解析身份、Pawn 和位置。 */
	struct FTestPlayer
	{
		/** 玩家稳定身份原文；测试用它对齐 Items 身份与 PlayerState UniqueId。 */
		FString StableNetId;

		/** 真实测试 Controller；BeginTheft 的唯一身份入口。 */
		TObjectPtr<APlayerController> Controller = nullptr;

		/** 项目 PlayerState；它持有服务器可见 UniqueId。 */
		TObjectPtr<ACatfishingPlayerState> PlayerState = nullptr;

		/** 项目 Character；Social 用它做可交互检查和距离判定。 */
		TObjectPtr<ACatCharacter> Character = nullptr;
	};

	/** Social 设置守卫；构造时把偷鱼所需的运行 gate、窗口和距离打开，析构时原样还回默认对象。 */
	struct FTheftSettingsOverride
	{
		/** 被覆盖的 Social 设置默认对象。 */
		UCatSocialSettings* SocialSettings = GetMutableDefault<UCatSocialSettings>();

		/** 测试前 Social 总 gate。 */
		bool bSavedSocialRuntime = false;

		/** 测试前偷鱼权限。 */
		ECatDomainPolicy SavedTheftPermission = ECatDomainPolicy::Unset;

		/** 测试前进食窗口秒数。 */
		double SavedEatingWindowSeconds = 0.0;

		/** 测试前偷鱼交互范围。 */
		double SavedTheftRangeCentimeters = 0.0;

		/** 测试前追回交互范围。 */
		double SavedCatchRangeCentimeters = 0.0;

		/** 测试前共享缸追回策略。 */
		ECatSharedTankRecoveryPolicy SavedSharedTankPolicy = ECatSharedTankRecoveryPolicy::Undecided;

		/** 测试前被抓印记事件；本测试关掉它，避免把互斥断言变成成像断言。 */
		FName SavedTheftCaughtImprintEventId = NAME_None;

		// 保存并覆盖流程：只改默认对象内存，让本测试 World 的 Social 子系统读到一套可运行的偷鱼配置。
		FTheftSettingsOverride()
		{
			if (SocialSettings)
			{
				bSavedSocialRuntime = SocialSettings->bEnableSocialRuntime;
				SavedTheftPermission = SocialSettings->TheftPermission;
				SavedEatingWindowSeconds = SocialSettings->TheftEatingWindowSeconds;
				SavedTheftRangeCentimeters = SocialSettings->TheftInteractionRangeCentimeters;
				SavedCatchRangeCentimeters = SocialSettings->TheftCatchRangeCentimeters;
				SavedSharedTankPolicy = SocialSettings->SharedTankRecoveryPolicy;
				SavedTheftCaughtImprintEventId = SocialSettings->TheftCaughtImprintEventId;
				SocialSettings->bEnableSocialRuntime = true;
				SocialSettings->TheftPermission = ECatDomainPolicy::Enabled;
				SocialSettings->TheftEatingWindowSeconds = 30.0;
				SocialSettings->TheftInteractionRangeCentimeters = 1000.0;
				SocialSettings->TheftCatchRangeCentimeters = 1000.0;
				SocialSettings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::OriginalOwner;
				SocialSettings->TheftCaughtImprintEventId = NAME_None;
			}
		}

		// 恢复流程：还原全部被覆盖字段，防止后续自动化读到本测试打开的偷鱼 gate。
		~FTheftSettingsOverride()
		{
			if (SocialSettings)
			{
				SocialSettings->bEnableSocialRuntime = bSavedSocialRuntime;
				SocialSettings->TheftPermission = SavedTheftPermission;
				SocialSettings->TheftEatingWindowSeconds = SavedEatingWindowSeconds;
				SocialSettings->TheftInteractionRangeCentimeters = SavedTheftRangeCentimeters;
				SocialSettings->TheftCatchRangeCentimeters = SavedCatchRangeCentimeters;
				SocialSettings->SharedTankRecoveryPolicy = SavedSharedTankPolicy;
				SocialSettings->TheftCaughtImprintEventId = SavedTheftCaughtImprintEventId;
			}
		}
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
		if (Result.Owner && Result.Component && ItemsService)
		{
			Result.Owner->AddInstanceComponent(Result.Component);
			Result.Component->RegisterComponent();
			ItemsService->RegisterContainer(Result.Component, Result.ContainerId, Kind, OwnerStableNetId, Capacity);
		}
		return Result;
	}

	// 玩家创建流程：生成真实 Controller、PlayerState 和项目 Character，并把 StableNetId 写入 UE UniqueId。
	static FTestPlayer SpawnPlayer(UWorld* World, const FString& StableNetId, const FVector& Location)
	{
		FTestPlayer Result;
		Result.StableNetId = StableNetId;
		Result.Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
		Result.PlayerState = World ? World->SpawnActor<ACatfishingPlayerState>() : nullptr;
		Result.Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		if (World && Result.Controller && Result.PlayerState)
		{
			const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
				StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
			Result.PlayerState->SetUniqueId(FUniqueNetIdRepl(StableUniqueId));
			Result.Controller->SetPlayerState(Result.PlayerState);
			World->AddController(Result.Controller);
		}
		if (Result.Controller && Result.Character)
		{
			Result.Character->SetActorLocation(Location);
			Result.Controller->Possess(Result.Character);
		}
		return Result;
	}

	// 快照读取流程：从服务公开只读接口读取容器；失败时返回默认快照，调用方继续用断言暴露问题。
	static FCatContainerSnapshot GetSnapshot(UCatItemsService* ItemsService, const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		if (ItemsService)
		{
			ItemsService->TryGetContainerSnapshot(ContainerId, Snapshot);
		}
		return Snapshot;
	}

	// 查询流程：只按公开 Snapshot 判断某条鱼是否存在；测试不访问 Items 私有持有或 escrow。
	static bool SnapshotContainsFish(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	// 种鱼流程：通过公开 CommitCapture 往个人鱼护写入一条真实鱼；ExpectedRevision 由调用方从当前快照读入。
	static bool SeedFish(UCatItemsService* ItemsService, const FRegisteredContainer& Guard, const FGuid FishInstanceId)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = GetSnapshot(ItemsService, Guard.ContainerId).Revision;
		Command.Context.StableNetId = Guard.OwnerStableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = TEXT("TestFish");
		Command.TargetContainerId = Guard.ContainerId;
		Command.WeightKilograms = 2.5;
		Command.SacrificeContribution = 3;
		return ItemsService->CommitCapture(Command).Command.bCommitted;
	}

	// 转移命令流程：构造正式 TransferOwnedFish 命令；身份与两个 ExpectedRevision 都由调用方明确传入，用于构造顺序竞争。
	static FCatFishTransferCommand MakeTransferCommand(const FRegisteredContainer& Source,
		const FRegisteredContainer& Target, const FGuid FishInstanceId, const int64 ExpectedSourceRevision,
		const int64 ExpectedTargetRevision, const FString& StableNetId)
	{
		FCatFishTransferCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = ExpectedSourceRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.SourceContainerId = Source.ContainerId;
		Command.TargetContainerId = Target.ContainerId;
		Command.ExpectedTargetRevision = ExpectedTargetRevision;
		return Command;
	}

	// 献祭命令流程：构造 Items 预留所需的 SacrificeCommand；只触达 Items 预留接口，不启动 Run 协调器。
	static FCatSacrificeCommand MakeSacrificeCommand(const FGuid ContainerId, const FGuid RequestId,
		const FGuid FishInstanceId, const int64 ExpectedRevision, const FString& StableNetId)
	{
		FCatSacrificeCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = StableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.ContainerId = ContainerId;
		Command.ExpectedRunRevision = 1;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsCompetingTransfersTest,
	"Catfishing.Unit.Items.Concurrency.CompetingTransfersMoveFishOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsTransferAndReservationRaceTest,
	"Catfishing.Unit.Items.Concurrency.TransferAndSacrificeReservationCannotBothTakeFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsPreparedSaleExclusionTest,
	"Catfishing.Unit.Items.Concurrency.PreparedSaleBlocksConsumeTransferAndSacrifice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatItemsReservationAndTheftEscrowRaceTest,
	"Catfishing.Unit.Items.Concurrency.SacrificeReservationAndTheftEscrowAreMutuallyExclusive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：构造两个"同时"对同一条鱼发起的转移——都基于同一份转移前快照，先让其中一个走完，再让另一个进来。
// 锁住的不变量：别人的鱼护自己动不了；后到的请求不能凭旧版本二次搬鱼；即使换成最新版本重来，那条鱼也已经不在源容器里。
// 结果只能有一条鱼、只落在一个目标容器里。
bool FCatItemsCompetingTransfersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 并发转移测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 并发转移测试 World"), World);
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

	const CatItemsServiceConcurrencyTest::FRegisteredContainer Guard = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 4);
	const CatItemsServiceConcurrencyTest::FRegisteredContainer TankX = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 4);
	const CatItemsServiceConcurrencyTest::FRegisteredContainer TankY = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 4);

	const FGuid ContestedFish = FGuid::NewGuid();
	TestTrue(TEXT("可种入被争夺的鱼"), CatItemsServiceConcurrencyTest::SeedFish(ItemsService, Guard, ContestedFish));

	// 两个请求共用这一份转移前的快照版本，等价于两个客户端在同一帧读到同一状态后各自发起转移。
	const int64 GuardRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const int64 TankXRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, TankX.ContainerId).Revision;
	const int64 TankYRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, TankY.ContainerId).Revision;

	const FCatDomainCommandResult OtherPlayerTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, TankY, ContestedFish, GuardRevision, TankYRevision,
			TEXT("PlayerB")));
	TestFalse(TEXT("别人不能从我的鱼护里搬鱼"), OtherPlayerTransfer.bCommitted);
	TestEqual(TEXT("他人转移返回 PermissionDenied"), OtherPlayerTransfer.Error, ECatDomainCommandError::PermissionDenied);
	TestEqual(TEXT("他人转移被拒后源容器版本不动"),
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, GuardRevision);

	const FCatDomainCommandResult FirstTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, TankX, ContestedFish, GuardRevision, TankXRevision,
			TEXT("PlayerA")));
	TestTrue(TEXT("先到的转移提交"), FirstTransfer.bCommitted);
	TestEqual(TEXT("先到的转移返回 None"), FirstTransfer.Error, ECatDomainCommandError::None);

	const FCatDomainCommandResult StaleSecondTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, TankY, ContestedFish, GuardRevision, TankYRevision,
			TEXT("PlayerA")));
	TestFalse(TEXT("后到的转移不能凭旧版本再搬一次"), StaleSecondTransfer.bCommitted);
	TestEqual(TEXT("后到的转移返回 RevisionConflict"), StaleSecondTransfer.Error, ECatDomainCommandError::RevisionConflict);

	const FCatDomainCommandResult RetriedSecondTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, TankY, ContestedFish,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, TankY.ContainerId).Revision,
			TEXT("PlayerA")));
	TestFalse(TEXT("后到的转移换成最新版本重试也搬不到鱼"), RetriedSecondTransfer.bCommitted);
	TestEqual(TEXT("鱼已离开源容器时转移返回 NotFound"), RetriedSecondTransfer.Error, ECatDomainCommandError::NotFound);

	TestTrue(TEXT("鱼落在先到的目标容器"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, TankX.ContainerId), ContestedFish));
	TestFalse(TEXT("鱼没有同时出现在另一个目标容器"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, TankY.ContainerId), ContestedFish));
	TestFalse(TEXT("鱼没有留在源容器"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId), ContestedFish));
	return !HasAnyErrors();
}

// 测试流程：让转移和献祭预留在同一条鱼上抢，两个方向各走一遍。
// 先预留后转移：转移必须被锁拒绝，鱼一步都不动；取消预留后同一次转移才能成功。
// 先转移后预留：对旧容器的预留即使换成该容器的最新版本也找不到鱼，而鱼在新容器里可以被正常预留。
bool FCatItemsTransferAndReservationRaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 转移预留竞争测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 转移预留竞争测试 World"), World);
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

	const CatItemsServiceConcurrencyTest::FRegisteredContainer Guard = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 4);
	const CatItemsServiceConcurrencyTest::FRegisteredContainer Tank = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 4);

	const FGuid ReservedFirst = FGuid::NewGuid();
	const FGuid TransferredFirst = FGuid::NewGuid();
	TestTrue(TEXT("可种入先被预留的鱼"), CatItemsServiceConcurrencyTest::SeedFish(ItemsService, Guard, ReservedFirst));
	TestTrue(TEXT("可种入先被转移的鱼"), CatItemsServiceConcurrencyTest::SeedFish(ItemsService, Guard, TransferredFirst));

	const FGuid ReservationRequestId = FGuid::NewGuid();
	TestTrue(TEXT("献祭预留先拿到这条鱼"), ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Guard.ContainerId, ReservationRequestId, ReservedFirst,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			TEXT("PlayerA"))).bReserved);

	const int64 GuardRevisionAfterReserve = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	const FCatDomainCommandResult BlockedTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, Tank, ReservedFirst, GuardRevisionAfterReserve,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision, TEXT("PlayerA")));
	TestFalse(TEXT("已被预留的鱼不能同时被转移"), BlockedTransfer.bCommitted);
	TestEqual(TEXT("与预留竞争的转移返回 InvalidPhase"), BlockedTransfer.Error, ECatDomainCommandError::InvalidPhase);
	TestTrue(TEXT("竞争失败的转移不会移走鱼"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId), ReservedFirst));
	TestFalse(TEXT("竞争失败的转移不会给目标容器加鱼"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId), ReservedFirst));

	TestTrue(TEXT("取消预留提交"), ItemsService->CancelFishReservation(TEXT("PlayerA"), ReservationRequestId,
		Guard.ContainerId).bCommitted);
	TestTrue(TEXT("预留让开后同一条鱼可以转移"), ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, Tank, ReservedFirst,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision,
			TEXT("PlayerA"))).bCommitted);

	TestTrue(TEXT("第二条鱼先被转移进共用鱼缸"), ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, Tank, TransferredFirst,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision,
			TEXT("PlayerA"))).bCommitted);

	const FCatFishReservationResult StaleContainerReserve = ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Guard.ContainerId, FGuid::NewGuid(), TransferredFirst,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, TEXT("PlayerA")));
	TestFalse(TEXT("鱼已经走了，对旧容器的预留拿不到它"), StaleContainerReserve.bReserved);
	TestEqual(TEXT("对旧容器的预留返回 NotFound"), StaleContainerReserve.Error, ECatDomainCommandError::NotFound);

	const FCatFishReservationResult NewContainerReserve = ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Tank.ContainerId, FGuid::NewGuid(), TransferredFirst,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision, TEXT("PlayerA")));
	TestTrue(TEXT("鱼在新容器里可以被正常预留"), NewContainerReserve.bReserved);
	TestEqual(TEXT("新容器预留返回 None"), NewContainerReserve.Error, ECatDomainCommandError::None);
	return !HasAnyErrors();
}

// 测试流程：商店把一条鱼冻结进售卖准备态之后，再让转移、进食和献祭预留分别去抢同一条鱼。
// 锁住的不变量：准备态期间这条鱼谁都动不了（这正是"钱和鱼不能同时得到"的前提）；回退准备态后它立刻恢复可动。
bool FCatItemsPreparedSaleExclusionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 售卖互斥测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 售卖互斥测试 World"), World);
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

	const CatItemsServiceConcurrencyTest::FRegisteredContainer Guard = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("PlayerA"), 4);
	const CatItemsServiceConcurrencyTest::FRegisteredContainer Tank = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 4);

	const FGuid HeldFish = FGuid::NewGuid();
	TestTrue(TEXT("可种入待售鱼"), CatItemsServiceConcurrencyTest::SeedFish(ItemsService, Guard, HeldFish));

	FCatFishSaleHoldCommand SaleCommand;
	SaleCommand.Context.RequestId = FGuid::NewGuid();
	SaleCommand.Context.ExpectedRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	SaleCommand.Context.StableNetId = TEXT("PlayerA");
	SaleCommand.FishInstanceId = HeldFish;
	SaleCommand.ContainerId = Guard.ContainerId;
	TestTrue(TEXT("售卖冻结提交"), ItemsService->PrepareFishSale(SaleCommand).Command.bCommitted);

	const int64 HeldRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision;
	FCatFishConsumeCommand ConsumeCommand;
	ConsumeCommand.Context.RequestId = FGuid::NewGuid();
	ConsumeCommand.Context.ExpectedRevision = HeldRevision;
	ConsumeCommand.Context.StableNetId = TEXT("PlayerA");
	ConsumeCommand.FishInstanceId = HeldFish;
	ConsumeCommand.SourceContainerId = Guard.ContainerId;
	const FCatFishConsumeResult BlockedConsume = ItemsService->ConsumeFish(ConsumeCommand);
	TestFalse(TEXT("售卖准备态期间不能把这条鱼吃掉"), BlockedConsume.Command.bCommitted);
	TestEqual(TEXT("与售卖竞争的进食返回 InvalidPhase"), BlockedConsume.Command.Error, ECatDomainCommandError::InvalidPhase);

	const FCatDomainCommandResult BlockedTransfer = ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, Tank, HeldFish, HeldRevision,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision, TEXT("PlayerA")));
	TestFalse(TEXT("售卖准备态期间不能把这条鱼转走"), BlockedTransfer.bCommitted);
	TestEqual(TEXT("与售卖竞争的转移返回 InvalidPhase"), BlockedTransfer.Error, ECatDomainCommandError::InvalidPhase);

	const FCatFishReservationResult BlockedReserve = ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Guard.ContainerId, FGuid::NewGuid(), HeldFish,
			HeldRevision, TEXT("PlayerA")));
	TestFalse(TEXT("售卖准备态期间不能把这条鱼拿去献祭"), BlockedReserve.bReserved);
	TestEqual(TEXT("与售卖竞争的献祭预留返回 InvalidPhase"), BlockedReserve.Error, ECatDomainCommandError::InvalidPhase);

	FCatFishSaleHoldCommand SecondSaleCommand = SaleCommand;
	SecondSaleCommand.Context.RequestId = FGuid::NewGuid();
	SecondSaleCommand.Context.ExpectedRevision = HeldRevision;
	const FCatFishSaleHoldResult SecondHold = ItemsService->PrepareFishSale(SecondSaleCommand);
	TestFalse(TEXT("同一条鱼不能被两笔售卖同时冻结"), SecondHold.Command.bCommitted);
	TestEqual(TEXT("第二笔售卖冻结返回 InvalidPhase"), SecondHold.Command.Error, ECatDomainCommandError::InvalidPhase);

	TestTrue(TEXT("竞争全部被拒后鱼仍原样留在容器"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId), HeldFish));
	TestEqual(TEXT("竞争全部被拒后容器版本没有被推进"),
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, HeldRevision);

	TestTrue(TEXT("回退售卖准备态"), ItemsService->CancelPreparedFishSale(TEXT("PlayerA"), Guard.ContainerId,
		SaleCommand.Context.RequestId).Command.bCommitted);
	const FCatFishReservationResult ReserveAfterCancel = ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Guard.ContainerId, FGuid::NewGuid(), HeldFish,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision, TEXT("PlayerA")));
	TestTrue(TEXT("回退售卖后这条鱼立刻恢复可献祭"), ReserveAfterCancel.bReserved);
	return !HasAnyErrors();
}

// 测试流程：让献祭预留和偷鱼 escrow 抢共用鱼缸里的同一条鱼，两个方向各走一遍。
// 偷鱼没有 Items 公共写口（BeginFishTheft 只对 Social 开放），所以这里走真实产品入口 UCatSocialService::BeginTheft。
// 这条用例留在 Items 而不是挑到 Social/Tests，是因为它锁的不变量属于 Items：
// 全部断言读的都是容器内容、容器版本和预留结果，一条也没验 Social 自己的追回窗口、权限或归还行为。
// Social 在这里只是唯一一扇能推开 escrow 的门。也因此不要为了“去掉这个依赖”而往 Items 的产品头文件里加测试缝或 friend：
// 那样换来的“独立”是假的——真正要保证的是玩家能走到的那条路不会把同一条鱼交给两个持有者。
// 先预留后偷：偷取必须被 Items 的鱼锁挡下，容器版本一动不动；取消预留后同一次偷取才能成立。
// 先偷后预留：鱼已经进了 escrow，容器里找不到它，预留只能返回 NotFound——两条路径不可能同时拿到同一条鱼。
bool FCatItemsReservationAndTheftEscrowRaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatItemsServiceConcurrencyTest::FTheftSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Items 预留偷取竞争测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建 Items 预留偷取竞争测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* SocialService = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	TestNotNull(TEXT("可取得 SocialService"), SocialService);
	if (!ItemsService || !SocialService)
	{
		return false;
	}

	const CatItemsServiceConcurrencyTest::FTestPlayer Thief = CatItemsServiceConcurrencyTest::SpawnPlayer(
		World, TEXT("ThiefStableId"), FVector(100.0, 0.0, 0.0));
	if (!Thief.Controller || !Thief.Character)
	{
		AddError(TEXT("偷取测试玩家未能完整生成"));
		return false;
	}

	const CatItemsServiceConcurrencyTest::FRegisteredContainer Guard = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::PersonalGuard, TEXT("FisherStableId"), 4);
	const CatItemsServiceConcurrencyTest::FRegisteredContainer Tank = CatItemsServiceConcurrencyTest::RegisterContainer(
		World, ItemsService, ECatContainerKind::SharedFishTank, TEXT(""), 4);
	TestNotNull(TEXT("共用鱼缸组件已创建"), Tank.Component.Get());

	const FGuid ContestedFish = FGuid::NewGuid();
	TestTrue(TEXT("可种入被争夺的团队储备鱼"), CatItemsServiceConcurrencyTest::SeedFish(ItemsService, Guard, ContestedFish));
	TestTrue(TEXT("原钓手把鱼存进共用鱼缸"), ItemsService->TransferOwnedFish(
		CatItemsServiceConcurrencyTest::MakeTransferCommand(Guard, Tank, ContestedFish,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Guard.ContainerId).Revision,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision,
			TEXT("FisherStableId"))).bCommitted);

	const FGuid ReservationRequestId = FGuid::NewGuid();
	TestTrue(TEXT("献祭预留先拿到共用鱼缸里的鱼"), ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Tank.ContainerId, ReservationRequestId, ContestedFish,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision,
			TEXT("FisherStableId"))).bReserved);

	const int64 TankRevisionAfterReserve = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision;
	FCatTheftCommand BlockedTheftCommand;
	BlockedTheftCommand.Context.RequestId = FGuid::NewGuid();
	BlockedTheftCommand.Context.ExpectedRevision = TankRevisionAfterReserve;
	BlockedTheftCommand.FishInstanceId = ContestedFish;
	BlockedTheftCommand.SourceContainerId = Tank.ContainerId;
	const FCatTheftResult BlockedTheft = SocialService->BeginTheft(Thief.Controller, BlockedTheftCommand);
	TestFalse(TEXT("已被预留的鱼不能同时被偷走"), BlockedTheft.Command.bCommitted);
	TestEqual(TEXT("与预留竞争的偷取返回 InvalidPhase"), BlockedTheft.Command.Error, ECatDomainCommandError::InvalidPhase);
	TestTrue(TEXT("竞争失败的偷取不会把鱼拿出容器"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId), ContestedFish));
	TestEqual(TEXT("竞争失败的偷取不推进容器版本"),
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision, TankRevisionAfterReserve);

	TestTrue(TEXT("取消预留提交"), ItemsService->CancelFishReservation(TEXT("FisherStableId"), ReservationRequestId,
		Tank.ContainerId).bCommitted);

	FCatTheftCommand AllowedTheftCommand;
	AllowedTheftCommand.Context.RequestId = FGuid::NewGuid();
	AllowedTheftCommand.Context.ExpectedRevision = CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision;
	AllowedTheftCommand.FishInstanceId = ContestedFish;
	AllowedTheftCommand.SourceContainerId = Tank.ContainerId;
	const FCatTheftResult AllowedTheft = SocialService->BeginTheft(Thief.Controller, AllowedTheftCommand);
	TestTrue(TEXT("预留让开后同一条鱼可以被偷"), AllowedTheft.Command.bCommitted);
	TestEqual(TEXT("放行的偷取返回 None"), AllowedTheft.Command.Error, ECatDomainCommandError::None);
	TestFalse(TEXT("偷取成功后鱼已进入 escrow，不在容器里"), CatItemsServiceConcurrencyTest::SnapshotContainsFish(
		CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId), ContestedFish));

	const FCatFishReservationResult ReserveAfterTheft = ItemsService->ReserveFish(
		CatItemsServiceConcurrencyTest::MakeSacrificeCommand(Tank.ContainerId, FGuid::NewGuid(), ContestedFish,
			CatItemsServiceConcurrencyTest::GetSnapshot(ItemsService, Tank.ContainerId).Revision, TEXT("FisherStableId")));
	TestFalse(TEXT("鱼在 escrow 里时预留拿不到它"), ReserveAfterTheft.bReserved);
	TestEqual(TEXT("鱼在 escrow 里时预留返回 NotFound"), ReserveAfterTheft.Error, ECatDomainCommandError::NotFound);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
