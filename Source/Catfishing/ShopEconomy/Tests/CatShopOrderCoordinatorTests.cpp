#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"
#include "UObject/StrongObjectPtr.h"

namespace CatShopOrderCoordinatorTest
{
	/** 测试用免费领取目录项；它模拟正式商店的保底竿领取入口。 */
	static const FName FreePersonalRodEntryId(TEXT("CoordinatorFreePersonalRodClaim"));

	/** 测试用商店鱼竿定义；协调器应把它交付到买家自己的 Equipment 快照。 */
	static const FName PersonalRodDefinitionId(TEXT("CoordinatorPersonalRod"));

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

	/** 商店装备领取测试的配置夹具；它同时覆盖 Shop 目录和 Equipment 运行定义，析构时恢复默认对象。 */
	struct FShopEquipmentGrantSettingsOverride
	{
		/** 被覆盖的商店设置默认对象；构造写入免费保底竿目录，析构恢复。 */
		UCatShopEconomySettings* ShopSettings = nullptr;

		/** 被覆盖的装备设置默认对象；构造写入测试鱼竿定义，析构恢复。 */
		UCatEquipmentSettings* EquipmentSettings = nullptr;

		/** 测试前商店 runtime gate；析构时恢复，避免影响其他 ShopEconomy 用例。 */
		bool bSavedShopRuntime = false;

		/** 测试前团队公款初始余额；本测试使用免费领取，但仍恢复完整经济默认值。 */
		int32 SavedStartingBalance = 0;

		/** 测试前商店目录；构造期间替换成一条免费装备领取入口。 */
		TArray<FCatShopCatalogEntry> SavedCatalogEntries;

		/** 测试前免费普通饵入口；本测试不使用它，但恢复时要保持默认对象完整。 */
		FName SavedFreeOrdinaryBaitEntryId = NAME_None;

		/** 测试前免费保底竿入口；构造期间指向本测试目录项。 */
		FName SavedFreeStarterRodEntryId = NAME_None;

		/** 测试前装备定义清单；析构时原样恢复。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions;

		/** 持有 transient 鱼竿定义；保证 Settings 软引用在测试 World 生命周期内可解析。 */
		TStrongObjectPtr<UCatEquipmentDefinition> RuntimeRodDefinition;

		/** 构造流程：先保存两个默认设置，再写入一条免费 EquipmentGrant 目录和对应鱼竿定义。 */
		FShopEquipmentGrantSettingsOverride()
		{
			ShopSettings = GetMutableDefault<UCatShopEconomySettings>();
			EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();
			if (ShopSettings)
			{
				bSavedShopRuntime = ShopSettings->bEnableShopEconomyRuntime;
				SavedStartingBalance = ShopSettings->StartingTeamWalletBalance;
				SavedCatalogEntries = ShopSettings->CatalogEntries;
				SavedFreeOrdinaryBaitEntryId = ShopSettings->FreeOrdinaryBaitEntryId;
				SavedFreeStarterRodEntryId = ShopSettings->FreeStarterRodEntryId;

				FCatShopCatalogEntry Entry;
				Entry.EntryId = FreePersonalRodEntryId;
				Entry.Kind = ECatShopEntryKind::EquipmentGrant;
				Entry.DefinitionId = PersonalRodDefinitionId;
				Entry.UnitPrice = 0;
				Entry.InitialStock = 0;
				Entry.bUnlimitedStock = true;
				ShopSettings->bEnableShopEconomyRuntime = true;
				ShopSettings->StartingTeamWalletBalance = 0;
				ShopSettings->CatalogEntries = { Entry };
				ShopSettings->FreeOrdinaryBaitEntryId = NAME_None;
				ShopSettings->FreeStarterRodEntryId = FreePersonalRodEntryId;
			}
			if (EquipmentSettings)
			{
				SavedDefinitions = EquipmentSettings->Definitions;
				RuntimeRodDefinition.Reset(NewObject<UCatEquipmentDefinition>(GetTransientPackage()));
				if (UCatEquipmentDefinition* Definition = RuntimeRodDefinition.Get())
				{
					Definition->bEnableRuntimeDefinition = true;
					Definition->EquipmentDefinitionId = PersonalRodDefinitionId;
					Definition->Kind = ECatEquipmentKind::Rod;
					Definition->LoadoutSlotId = TEXT("CoordinatorPersonalRodSlot");
					Definition->RequiredUnlockId = NAME_None;
					Definition->MaximumRodDurability = 150.0;
					Definition->FishingStrength = 1.0;
					Definition->MaximumLineLengthCentimeters = 2000.0;
					Definition->BaseDurabilityWearPerSecond = 0.1;
					Definition->HighTensionWearMultiplier = 1.0;
					EquipmentSettings->Definitions = { Definition };
				}
			}
		}

		/** 析构流程：恢复 Shop 与 Equipment 默认设置；测试定义不写资产，随强引用释放。 */
		~FShopEquipmentGrantSettingsOverride()
		{
			if (ShopSettings)
			{
				ShopSettings->bEnableShopEconomyRuntime = bSavedShopRuntime;
				ShopSettings->StartingTeamWalletBalance = SavedStartingBalance;
				ShopSettings->CatalogEntries = SavedCatalogEntries;
				ShopSettings->FreeOrdinaryBaitEntryId = SavedFreeOrdinaryBaitEntryId;
				ShopSettings->FreeStarterRodEntryId = SavedFreeStarterRodEntryId;
			}
			if (EquipmentSettings)
			{
				EquipmentSettings->Definitions = SavedDefinitions;
			}
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

	/** 装备组件创建流程：用 authority Pawn 承载买家的 EquipmentComponent，模拟 PlayerController 从本人 Pawn 取得收货组件。 */
	static UCatEquipmentComponent* AddRecipientEquipment(APawn* Pawn)
	{
		UCatEquipmentComponent* Component = Pawn ? NewObject<UCatEquipmentComponent>(Pawn) : nullptr;
		if (Pawn && Component)
		{
			Pawn->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatShopOrderCoordinatorEquipmentClaimGrantTest,
	"Catfishing.Unit.ShopEconomy.OrderCoordinator.EquipmentClaimGrantsPersonalRod",
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

// 测试流程：
// 1. 用临时 Shop 目录建立一条免费 EquipmentGrant 保底竿入口，并用临时 Equipment 目录提供鱼竿定义。
// 2. 让协调器执行免费领取订单，订单应完成经济账本和 Equipment 授予两段提交。
// 3. 检查买家自己的 Equipment 快照里已经出现该鱼竿，账本回执使用订单 RequestId 和 Equipment Revision。
// 4. 用同一 RequestId 重放，确认不会再次推进买家装备 Revision。
bool FCatShopOrderCoordinatorEquipmentClaimGrantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatShopOrderCoordinatorTest::FShopEquipmentGrantSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建商店装备领取测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatShopEconomyService* ShopService = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	UCatShopOrderCoordinator* Coordinator = World ? World->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	APawn* RecipientPawn = World ? World->SpawnActor<APawn>() : nullptr;
	UCatEquipmentComponent* RecipientEquipment =
		CatShopOrderCoordinatorTest::AddRecipientEquipment(RecipientPawn);
	TestNotNull(TEXT("商店装备领取 ShopEconomyService 可创建"), ShopService);
	TestNotNull(TEXT("商店装备领取 Coordinator 可创建"), Coordinator);
	TestNotNull(TEXT("商店装备领取 Pawn 可创建"), RecipientPawn);
	TestNotNull(TEXT("商店装备领取 EquipmentComponent 可创建"), RecipientEquipment);
	if (!ShopService || !Coordinator || !RecipientEquipment)
	{
		return false;
	}

	FCatShopPurchaseCommand Command;
	Command.Context.RequestId = FGuid::NewGuid();
	Command.Context.ExpectedRevision = ShopService->GetWalletSnapshot().Revision;
	Command.Context.StableNetId = TEXT("PlayerA");
	Command.EntryId = CatShopOrderCoordinatorTest::FreePersonalRodEntryId;

	const FCatShopOrderResult Claim = Coordinator->SubmitFreeClaim(Command, RecipientEquipment);
	TestTrue(TEXT("免费鱼竿领取经济段提交"), Claim.Transaction.Command.bCommitted);
	TestEqual(TEXT("免费鱼竿领取经济段无错误"), Claim.Transaction.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("免费鱼竿领取交付确认无错误"), Claim.Delivery.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("免费鱼竿领取后账本已交付"), Claim.Transaction.Transaction.DeliveryState,
		ECatShopDeliveryState::Delivered);
	TestEqual(TEXT("免费鱼竿领取回执使用订单 RequestId"),
		Claim.Transaction.Transaction.DeliveryReceiptId, Command.Context.RequestId);
	TestEqual(TEXT("免费鱼竿进入买家个人鱼竿槽"),
		RecipientEquipment->GetSnapshot().RodDefinitionId,
		CatShopOrderCoordinatorTest::PersonalRodDefinitionId);
	TestEqual(TEXT("免费鱼竿按定义写入耐久"), RecipientEquipment->GetSnapshot().RodDurability, 150.0);
	TestEqual(TEXT("账本记录 Equipment 交付后的个人装备版本"),
		Claim.Transaction.Transaction.DeliveryRevision, RecipientEquipment->GetSnapshot().Revision);

	const int64 EquipmentRevisionAfterClaim = RecipientEquipment->GetSnapshot().Revision;
	const FCatShopOrderResult Replay = Coordinator->SubmitFreeClaim(Command, RecipientEquipment);
	TestFalse(TEXT("免费鱼竿领取重放不重复提交经济段"), Replay.Transaction.Command.bCommitted);
	TestEqual(TEXT("免费鱼竿领取重放经济段 AlreadyResolved"), Replay.Transaction.Command.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("免费鱼竿领取重放交付段 AlreadyResolved"), Replay.Delivery.Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("免费鱼竿领取重放不推进个人装备版本"),
		RecipientEquipment->GetSnapshot().Revision, EquipmentRevisionAfterClaim);

	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
