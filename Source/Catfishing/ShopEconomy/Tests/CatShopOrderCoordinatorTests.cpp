#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "GameFramework/Actor.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"

namespace CatShopOrderCoordinatorTest
{
	/** 测试售鱼时临时写入的商店配置；它代表可运行的公款 gate、最小售鱼金额和一张确定的体重价格表。 */
	struct FShopSaleSettingsOverride
	{
		/** 被覆盖的默认设置对象；构造写入测试配置，析构恢复原值。 */
		UCatShopEconomySettings* Settings = nullptr;

		/** 测试前 runtime gate 原值；析构时恢复，避免影响其他 ShopEconomy 用例。 */
		bool bSavedRuntime = false;

		/** 测试前团队公款初始余额；售鱼用例需要从 0 开始观察增量。 */
		int32 SavedStartingBalance = 0;

		/** 测试前最小售鱼金额；恢复后不把测试价格策略泄露给默认对象。 */
		int32 SavedMinimumFishSaleValue = 1;

		/** 测试前收鱼价策略；售鱼协调器必须只在显式 Enabled 时估价。 */
		ECatDomainPolicy SavedFishPurchasePricePolicy = ECatDomainPolicy::Unset;

		/** 测试前收鱼价档位表；构造期间替换成单档价格，析构原样恢复。 */
		TArray<FCatShopFishWeightPrice> SavedFishPurchasePriceAnchors;

		/** 测试前商店目录；本文件不测购买，所以构造期间清空目录以减少无关事实。 */
		TArray<FCatShopCatalogEntry> SavedCatalogEntries;

		/** 构造流程：保存默认对象上所有会影响售鱼的字段，再写入本测试唯一需要的价格轴和公款 gate。 */
		FShopSaleSettingsOverride()
		{
			Settings = GetMutableDefault<UCatShopEconomySettings>();
			if (!Settings)
			{
				return;
			}

			bSavedRuntime = Settings->bEnableShopEconomyRuntime;
			SavedStartingBalance = Settings->StartingTeamWalletBalance;
			SavedMinimumFishSaleValue = Settings->MinimumFishSaleValue;
			SavedFishPurchasePricePolicy = Settings->FishPurchasePricePolicy;
			SavedFishPurchasePriceAnchors = Settings->FishPurchasePriceAnchors;
			SavedCatalogEntries = Settings->CatalogEntries;

			Settings->bEnableShopEconomyRuntime = true;
			Settings->StartingTeamWalletBalance = 0;
			Settings->MinimumFishSaleValue = 1;
			Settings->CatalogEntries.Reset();
			Settings->FishPurchasePricePolicy = ECatDomainPolicy::Enabled;
			Settings->FishPurchasePriceAnchors.Reset();
			FCatShopFishWeightPrice Anchor;
			Anchor.MinimumWeightKilograms = 0.5;
			Anchor.Price = 5;
			Settings->FishPurchasePriceAnchors.Add(Anchor);
		}

		/** 析构流程：恢复默认对象，确保后续 WorldSubsystem 初始化不会读到本测试的价格轴或空目录。 */
		~FShopSaleSettingsOverride()
		{
			if (!Settings)
			{
				return;
			}

			Settings->bEnableShopEconomyRuntime = bSavedRuntime;
			Settings->StartingTeamWalletBalance = SavedStartingBalance;
			Settings->MinimumFishSaleValue = SavedMinimumFishSaleValue;
			Settings->FishPurchasePricePolicy = SavedFishPurchasePricePolicy;
			Settings->FishPurchasePriceAnchors = SavedFishPurchasePriceAnchors;
			Settings->CatalogEntries = SavedCatalogEntries;
		}
	};

	/** 已注册容器的测试上下文；它代表真实 Actor 宿主、复制组件、容器 ID 和服务器私有主人身份。 */
	struct FRegisteredContainer
	{
		/** 承载容器复制组件的 authority Actor；Items 注册时从组件 Owner 找回空间宿主。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** Items 发布快照的正式复制组件；测试只通过服务公开接口读取容器内容。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本局容器稳定 ID；售鱼命令和 Items 终态缓存都以它作为聚合作用域。 */
		FGuid ContainerId;

		/** 个人鱼护主人身份；捕获鱼 OwnerStableNetId 与售鱼命令身份必须匹配。 */
		FString OwnerStableNetId;
	};

	/** 容器注册流程：创建真实 Actor 与复制组件，再让 ItemsService 建立空容器快照。 */
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

	/** 捕获命令构造流程：把测试鱼写入指定个人鱼护，重量固定落入本文件的单档收购价。 */
	static FCatCaptureCommitCommand MakeCaptureCommand(const FRegisteredContainer& TargetContainer,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = TargetContainer.OwnerStableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = TEXT("ShopSaleFish");
		Command.TargetContainerId = TargetContainer.ContainerId;
		Command.WeightKilograms = 2.5;
		Command.SacrificeContribution = 1;
		return Command;
	}

	/** 种鱼流程：只通过 Items 捕获提交写入鱼实例；失败时返回原始结果让测试断言暴露。 */
	static FCatCaptureCommitResult SeedFish(UCatItemsService* ItemsService, const FRegisteredContainer& TargetContainer,
		const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		return ItemsService->CommitCapture(MakeCaptureCommand(TargetContainer, FGuid::NewGuid(), FishInstanceId,
			ExpectedRevision));
	}

	/** 快照读取流程：通过 Items 公开只读接口复制容器状态；读取失败时返回空快照，后续断言负责报错。 */
	static FCatContainerSnapshot GetSnapshot(UCatItemsService* ItemsService, const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		if (ItemsService)
		{
			ItemsService->TryGetContainerSnapshot(ContainerId, Snapshot);
		}
		return Snapshot;
	}

	/** 查询流程：按公开快照判断鱼是否还存在；测试不访问 Items 私有数组或终态缓存。 */
	static bool SnapshotContainsFish(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	/** 售鱼命令构造流程：填入服务器身份、两个 ExpectedRevision、来源和目标鱼，不提交价格或重量。 */
	static FCatShopFishSaleOrderCommand MakeSaleOrderCommand(const FRegisteredContainer& Container,
		const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedContainerRevision,
		const int64 ExpectedWalletRevision)
	{
		FCatShopFishSaleOrderCommand Command;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedWalletRevision;
		Command.Context.StableNetId = Container.OwnerStableNetId;
		Command.FishInstanceId = FishInstanceId;
		Command.ContainerId = Container.ContainerId;
		Command.ExpectedContainerRevision = ExpectedContainerRevision;
		Command.SourceKind = ECatShopFishSaleSource::PersonalGuard;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorFishSaleSuccessReplayTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.FishSaleConsumesFishCreditsWalletAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorFishSalePrecheckFailureTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.FishSalePrecheckFailureDoesNotConsumeFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：注册个人鱼护并通过 Items 捕获提交种鱼；协调器售鱼后必须删除鱼、增加团队公款并写 FishSale 账本，
// 同一 RequestId 重放只能读取 Items/Shop 终态，不能再次加钱或再次删除。
bool FCatShopOrderCoordinatorFishSaleSuccessReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderCoordinatorTest::FShopSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建售鱼协调器成功测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatShopEconomyService* ShopService = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	TestNotNull(TEXT("ItemsService 可创建"), ItemsService);
	TestNotNull(TEXT("ShopEconomyService 可创建"), ShopService);
	TestNotNull(TEXT("ShopOrderCoordinator 可创建"), Coordinator);
	if (!ItemsService || !ShopService || !Coordinator)
	{
		return false;
	}

	const CatShopOrderCoordinatorTest::FRegisteredContainer Guard =
		CatShopOrderCoordinatorTest::RegisterContainer(World, ItemsService, ECatContainerKind::PersonalGuard,
			TEXT("PlayerA"), 3);
	TestNotNull(TEXT("个人鱼护复制组件已创建"), Guard.Component.Get());
	const FGuid FishInstanceId = FGuid::NewGuid();
	const FCatCaptureCommitResult Capture = CatShopOrderCoordinatorTest::SeedFish(ItemsService, Guard,
		FishInstanceId, 1);
	TestTrue(TEXT("售鱼前可捕获写入个人鱼护"), Capture.Command.bCommitted);

	const FGuid SaleRequestId = FGuid::NewGuid();
	const FCatShopFishSaleOrderCommand SaleCommand =
		CatShopOrderCoordinatorTest::MakeSaleOrderCommand(Guard, SaleRequestId, FishInstanceId,
			Capture.Committed.ContainerRevision, ShopService->GetWalletSnapshot().Revision);
	const FCatShopOrderResult Sale = Coordinator->SubmitFishSale(SaleCommand);
	TestTrue(TEXT("首次售鱼 Items 段提交"), Sale.Delivery.bCommitted);
	TestEqual(TEXT("首次售鱼 Items 段无错误"), Sale.Delivery.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("首次售鱼公款段提交"), Sale.Transaction.Command.bCommitted);
	TestEqual(TEXT("首次售鱼公款段无错误"), Sale.Transaction.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("售鱼增加团队公款"), ShopService->GetWalletSnapshot().Balance, 5);
	TestEqual(TEXT("售鱼账本类别为 FishSale"), Sale.Transaction.Transaction.Kind,
		ECatShopTransactionKind::FishSale);
	TestEqual(TEXT("售鱼账本记录鱼实例"), Sale.Transaction.Transaction.FishInstanceId, FishInstanceId);
	TestFalse(TEXT("售鱼后鱼从个人鱼护移除"),
		CatShopOrderCoordinatorTest::SnapshotContainsFish(
			CatShopOrderCoordinatorTest::GetSnapshot(ItemsService, Guard.ContainerId), FishInstanceId));

	const FCatShopOrderResult Replay = Coordinator->SubmitFishSale(SaleCommand);
	TestFalse(TEXT("售鱼重放 Items 不再次提交"), Replay.Delivery.bCommitted);
	TestEqual(TEXT("售鱼重放 Items 返回 AlreadyResolved"), Replay.Delivery.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("售鱼重放公款不再次提交"), Replay.Transaction.Command.bCommitted);
	TestEqual(TEXT("售鱼重放公款返回 AlreadyResolved"), Replay.Transaction.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("售鱼重放不二次加钱"), ShopService->GetWalletSnapshot().Balance, 5);
	TestEqual(TEXT("售鱼重放不新增账本"), ShopService->GetTransactionLedgerSnapshot().Num(), 1);
	TestFalse(TEXT("售鱼重放后鱼仍不存在"),
		CatShopOrderCoordinatorTest::SnapshotContainsFish(
			CatShopOrderCoordinatorTest::GetSnapshot(ItemsService, Guard.ContainerId), FishInstanceId));

	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

// 测试流程：使用陈旧的公款 ExpectedRevision 触发 Shop 预检拒绝；协调器必须在 Items 消费前停止，鱼仍留在容器，公款和账本不变。
bool FCatShopOrderCoordinatorFishSalePrecheckFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderCoordinatorTest::FShopSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建售鱼协调器预检失败测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatShopEconomyService* ShopService = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	TestNotNull(TEXT("ItemsService 可创建"), ItemsService);
	TestNotNull(TEXT("ShopEconomyService 可创建"), ShopService);
	TestNotNull(TEXT("ShopOrderCoordinator 可创建"), Coordinator);
	if (!ItemsService || !ShopService || !Coordinator)
	{
		return false;
	}

	const CatShopOrderCoordinatorTest::FRegisteredContainer Guard =
		CatShopOrderCoordinatorTest::RegisterContainer(World, ItemsService, ECatContainerKind::PersonalGuard,
			TEXT("PlayerA"), 3);
	const FGuid FishInstanceId = FGuid::NewGuid();
	const FCatCaptureCommitResult Capture = CatShopOrderCoordinatorTest::SeedFish(ItemsService, Guard,
		FishInstanceId, 1);
	TestTrue(TEXT("预检失败用例可捕获写入个人鱼护"), Capture.Command.bCommitted);

	const int64 StaleWalletRevision = ShopService->GetWalletSnapshot().Revision + 1;
	const FCatShopFishSaleOrderCommand SaleCommand =
		CatShopOrderCoordinatorTest::MakeSaleOrderCommand(Guard, FGuid::NewGuid(), FishInstanceId,
			Capture.Committed.ContainerRevision, StaleWalletRevision);
	const FCatShopOrderResult Rejected = Coordinator->SubmitFishSale(SaleCommand);
	TestFalse(TEXT("公款 Revision 陈旧时不提交 Items 段"), Rejected.Delivery.bCommitted);
	TestEqual(TEXT("公款 Revision 陈旧时 Delivery 返回 RevisionConflict"), Rejected.Delivery.Error,
		ECatDomainCommandError::RevisionConflict);
	TestFalse(TEXT("公款 Revision 陈旧时不提交公款段"), Rejected.Transaction.Command.bCommitted);
	TestEqual(TEXT("公款 Revision 陈旧时 Transaction 返回 RevisionConflict"), Rejected.Transaction.Command.Error,
		ECatDomainCommandError::RevisionConflict);
	TestTrue(TEXT("售鱼预检失败后鱼仍在容器"),
		CatShopOrderCoordinatorTest::SnapshotContainsFish(
			CatShopOrderCoordinatorTest::GetSnapshot(ItemsService, Guard.ContainerId), FishInstanceId));
	TestEqual(TEXT("售鱼预检失败不加钱"), ShopService->GetWalletSnapshot().Balance, 0);
	TestEqual(TEXT("售鱼预检失败不写账本"), ShopService->GetTransactionLedgerSnapshot().Num(), 0);

	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
