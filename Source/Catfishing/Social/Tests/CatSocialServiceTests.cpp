#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Engine/Player.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "Social/CatSocialService.h"
#include "Components/SceneComponent.h"
#include "Social/CatSocialSettings.h"
#include "TimerManager.h"

namespace CatSocialServiceTest
{
	/**
	 * 本组测试用来在测试 World 里标记"商店在这里"的 Actor Tag；Settings 覆盖和锚点 Actor 必须取同一个值，否则 Social
	 * 找不到商店，售出会以位置不满足被拒。写成函数而不是全局 FName，避免在 FName 系统初始化之前构造静态对象。
	 */
	static FName ShopAnchorTag()
	{
		return FName(TEXT("CatSocialTestShopAnchor"));
	}

	/** 测试玩家上下文；代表真实 Controller、PlayerState 身份和被 Social 读取的项目 Character。 */
	struct FTestPlayer
	{
		/** 玩家稳定身份原文；测试用它和 UniqueId.ToString 对齐断言与容器主人。 */
		FString StableNetId;

		/** 真实测试 Controller；Social 只从它解析 PlayerState 与 Pawn。 */
		TObjectPtr<APlayerController> Controller = nullptr;

		/** 项目 PlayerState；它持有服务器可见 UniqueId。 */
		TObjectPtr<ACatfishingPlayerState> PlayerState = nullptr;

		/** 项目 Character；Social 用它的 Condition 和位置做可交互检查。 */
		TObjectPtr<ACatCharacter> Character = nullptr;
	};

	/** 已注册 Items 容器上下文；测试只通过公开服务入口写鱼，不直接改复制数组。 */
	struct FRegisteredContainer
	{
		/** 容器宿主 Actor；Social 会用它的位置做偷鱼交互距离判断。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** 正式容器复制组件；Items 发布快照时使用它。 */
		TObjectPtr<UCatContainerReplicationComponent> Component = nullptr;

		/** 本局容器稳定 ID；捕获、偷鱼和快照查询都引用它。 */
		FGuid ContainerId;

		/** 容器私有主人身份；个人鱼护授权和 Fish Owner 必须一致。 */
		FString OwnerStableNetId;
	};

	/** Social 与 ShopEconomy 设置守卫；构造时开启偷鱼与放牌前置条件和可观测钱包，析构时恢复默认对象。 */
	struct FSocialSaleSettingsOverride
	{
		/** 被覆盖的 Social 设置默认对象。 */
		UCatSocialSettings* SocialSettings = GetMutableDefault<UCatSocialSettings>();

		/** 被覆盖的 ShopEconomy 设置默认对象。 */
		UCatShopEconomySettings* ShopSettings = GetMutableDefault<UCatShopEconomySettings>();

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

		/** 测试前被抓印记事件；本测试关闭它，避免把售卖关闭断言变成成像断言。 */
		FName SavedTheftCaughtImprintEventId = NAME_None;

		/** 测试前防骚扰牌保护半径；护栏测试要用一个明确半径判断"牌内"和"牌外"。 */
		double SavedProtectionSignRadiusCentimeters = 0.0;

		/** 测试前放牌距离上限；护栏测试要能在角色脚下直接放牌。 */
		double SavedProtectionSignPlacementRangeCentimeters = 0.0;

		/** 测试前商店锚点标签；售鱼测试要用一个明确标签在测试 World 里摆出"商店"。 */
		FName SavedTheftSaleShopAnchorTag = NAME_None;

		/** 测试前到店距离上限；售鱼测试要能分辨"站在店门口"和"站在受害者面前"。 */
		double SavedTheftSaleShopRangeCentimeters = 0.0;

		/** 测试前进食所需的逃离受害者距离；本组测试不走进食终态，但仍要还原，避免影响其他自动化。 */
		double SavedTheftConsumeVictimEscapeDistanceCentimeters = 0.0;

		/** 测试前 ShopEconomy 总 gate。 */
		bool bSavedShopRuntime = false;

		/** 测试前团队钱包初始余额。 */
		int32 SavedStartingBalance = 0;

		/** 测试前最小售鱼金额。 */
		int32 SavedMinimumFishSaleValue = 1;

		/** 测试前免费普通饵配置。 */
		FName SavedFreeOrdinaryBaitEntryId = NAME_None;

		/** 测试前商店目录；本测试会观察钱包入账，析构必须原样还回去。 */
		TArray<FCatShopCatalogEntry> SavedCatalogEntries;

		/** 测试前收鱼价体重轴的裁决位；项目配置里它是 Unset 的 fail-closed，售鱼测试必须显式裁开才走得通。 */
		ECatDomainPolicy SavedFishPurchasePricePolicy = ECatDomainPolicy::Unset;

		/** 测试前收鱼价体重轴档位表；本测试会塞一份夹具表进去，析构必须原样还回去。 */
		TArray<FCatShopFishWeightPrice> SavedFishPurchasePriceAnchors;

		// 保存并覆盖流程：只改默认对象内存，让本测试 WorldSubsystem 初始化时读到可运行 Social 和可观察钱包配置。
		FSocialSaleSettingsOverride()
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
				SavedProtectionSignRadiusCentimeters = SocialSettings->ProtectionSignRadiusCentimeters;
				SavedProtectionSignPlacementRangeCentimeters = SocialSettings->ProtectionSignPlacementRangeCentimeters;
				SavedTheftSaleShopAnchorTag = SocialSettings->TheftSaleShopAnchorTag;
				SavedTheftSaleShopRangeCentimeters = SocialSettings->TheftSaleShopRangeCentimeters;
				SavedTheftConsumeVictimEscapeDistanceCentimeters = SocialSettings->TheftConsumeVictimEscapeDistanceCentimeters;
				SocialSettings->bEnableSocialRuntime = true;
				SocialSettings->TheftPermission = ECatDomainPolicy::Enabled;
				SocialSettings->TheftEatingWindowSeconds = 30.0;
				SocialSettings->TheftInteractionRangeCentimeters = 1000.0;
				SocialSettings->TheftCatchRangeCentimeters = 1000.0;
				SocialSettings->SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::OriginalOwner;
				SocialSettings->TheftCaughtImprintEventId = NAME_None;
				SocialSettings->ProtectionSignRadiusCentimeters = 300.0;
				SocialSettings->ProtectionSignPlacementRangeCentimeters = 200.0;
				// 这两个数字只是本测试用来分辨"店门口"和"离得远"的刻度，不是飞书裁下来的产品数值，不得据此填进项目配置。
				SocialSettings->TheftSaleShopAnchorTag = CatSocialServiceTest::ShopAnchorTag();
				SocialSettings->TheftSaleShopRangeCentimeters = 400.0;
				SocialSettings->TheftConsumeVictimEscapeDistanceCentimeters = 2000.0;
			}
			if (ShopSettings)
			{
				bSavedShopRuntime = ShopSettings->bEnableShopEconomyRuntime;
				SavedStartingBalance = ShopSettings->StartingTeamWalletBalance;
				SavedMinimumFishSaleValue = ShopSettings->MinimumFishSaleValue;
				SavedFreeOrdinaryBaitEntryId = ShopSettings->FreeOrdinaryBaitEntryId;
				SavedCatalogEntries = ShopSettings->CatalogEntries;
				SavedFishPurchasePricePolicy = ShopSettings->FishPurchasePricePolicy;
				SavedFishPurchasePriceAnchors = ShopSettings->FishPurchasePriceAnchors;
				ShopSettings->bEnableShopEconomyRuntime = true;
				ShopSettings->StartingTeamWalletBalance = 0;
				ShopSettings->MinimumFishSaleValue = 1;
				ShopSettings->FreeOrdinaryBaitEntryId = NAME_None;
				ShopSettings->CatalogEntries.Reset();
				// 售价现在完全由服务器按体重轴估，收鱼价没裁过就等于任何鱼都卖不掉，所以售鱼测试必须自己把这条裁开并给一张表。
				// 下面三档只是本测试的夹具刻度：它们只需满足体重严格递增、价格不递减，并且让 MakeCaptureCommand 那条 2.0 千克的鱼落进中间那一档。
				// 飞书至今没给过任何收鱼价数值，所以这张表不代表产品定价，绝不能被抄进 Config 里的任何 .ini。
				ShopSettings->FishPurchasePricePolicy = ECatDomainPolicy::Enabled;
				ShopSettings->FishPurchasePriceAnchors.Reset();
				FCatShopFishWeightPrice& LightAnchor = ShopSettings->FishPurchasePriceAnchors.AddDefaulted_GetRef();
				LightAnchor.MinimumWeightKilograms = 0.5;
				LightAnchor.Price = 3;
				FCatShopFishWeightPrice& MiddleAnchor = ShopSettings->FishPurchasePriceAnchors.AddDefaulted_GetRef();
				MiddleAnchor.MinimumWeightKilograms = 1.5;
				MiddleAnchor.Price = 7;
				FCatShopFishWeightPrice& HeavyAnchor = ShopSettings->FishPurchasePriceAnchors.AddDefaulted_GetRef();
				HeavyAnchor.MinimumWeightKilograms = 5.0;
				HeavyAnchor.Price = 20;
			}
		}

		// 恢复流程：还原全部默认对象字段，防止后续自动化读到本测试的运行 gate 和钱包配置。
		~FSocialSaleSettingsOverride()
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
				SocialSettings->ProtectionSignRadiusCentimeters = SavedProtectionSignRadiusCentimeters;
				SocialSettings->ProtectionSignPlacementRangeCentimeters = SavedProtectionSignPlacementRangeCentimeters;
				SocialSettings->TheftSaleShopAnchorTag = SavedTheftSaleShopAnchorTag;
				SocialSettings->TheftSaleShopRangeCentimeters = SavedTheftSaleShopRangeCentimeters;
				SocialSettings->TheftConsumeVictimEscapeDistanceCentimeters = SavedTheftConsumeVictimEscapeDistanceCentimeters;
			}
			if (ShopSettings)
			{
				ShopSettings->bEnableShopEconomyRuntime = bSavedShopRuntime;
				ShopSettings->StartingTeamWalletBalance = SavedStartingBalance;
				ShopSettings->MinimumFishSaleValue = SavedMinimumFishSaleValue;
				ShopSettings->FreeOrdinaryBaitEntryId = SavedFreeOrdinaryBaitEntryId;
				ShopSettings->CatalogEntries = SavedCatalogEntries;
				ShopSettings->FishPurchasePricePolicy = SavedFishPurchasePricePolicy;
				ShopSettings->FishPurchasePriceAnchors = SavedFishPurchasePriceAnchors;
			}
		}
	};

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

	// 容器注册流程：创建真实 Actor 与复制组件，再让 Items 服务建立正式容器记录。
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

	// 捕获命令流程：构造 Items 的正式捕获提交，ExpectedRevision 由调用方从快照传入。
	static FCatCaptureCommitCommand MakeCaptureCommand(const FRegisteredContainer& TargetContainer,
		const FGuid FishInstanceId, const int64 ExpectedRevision)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.Context.StableNetId = TargetContainer.OwnerStableNetId;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = TEXT("TestFish");
		Command.TargetContainerId = TargetContainer.ContainerId;
		Command.WeightKilograms = 2.0;
		Command.SacrificeContribution = 1;
		return Command;
	}

	// 快照读取流程：通过 Items 公开只读接口取容器事实；失败时保持默认快照，让测试断言暴露问题。
	static FCatContainerSnapshot GetSnapshot(UCatItemsService* ItemsService, const FGuid ContainerId)
	{
		FCatContainerSnapshot Snapshot;
		if (ItemsService)
		{
			ItemsService->TryGetContainerSnapshot(ContainerId, Snapshot);
		}
		return Snapshot;
	}

	// 查询流程：只看公开 Snapshot 中是否还有指定鱼实例，不访问 Items 私有 escrow。
	static bool SnapshotContainsFish(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.ContainsByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	// 商店锚点摆放流程：在给定位置生成一个普通 Actor 并打上商店标签，模拟关卡里摆好的商店；Social 只按标签找它，不关心它是什么类。
	static AActor* SpawnShopAnchor(UWorld* World, const FVector& Location)
	{
		AActor* Anchor = World ? World->SpawnActor<AActor>() : nullptr;
		if (!Anchor)
		{
			return nullptr;
		}
		// 裸 AActor 没有根组件，而 AActor::GetActorLocation 在没有根组件时恒返回原点、SetActorLocation 静默失败。
		// 少了这一步，锚点无论传什么坐标都站在世界原点，"到店距离"这条判定就永远成立，
		// 于是"人在受害者面前也能把赃物卖掉"会被测试当成正确行为放过去。
		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor, TEXT("ShopAnchorRoot"));
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		Anchor->Tags.Add(ShopAnchorTag());
		return Anchor;
	}

	// 公开叼鱼查询流程：站在第三方视角只读服务发布的公开列表，找这名玩家是否正被标成"叼着赃物"。
	// 它刻意只经 PlayerState 匹配，因为客户端能拿到的就只有 PlayerState，测试必须用同样受限的视角断言。
	static const FCatStolenFishCarrySnapshot* FindPublicCarrier(const UCatSocialService* Social,
		const APlayerState* PlayerState)
	{
		if (!Social || !PlayerState)
		{
			return nullptr;
		}
		return Social->GetStolenFishCarriers().FindByPredicate(
			[PlayerState](const FCatStolenFishCarrySnapshot& Carrier)
			{
				return Carrier.CarrierPlayerState.Get() == PlayerState;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceFailClosedTest,
	"Catfishing.Unit.Social.Service.EmptyTeardownAndMissingIdentityCommandsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceSellStolenFishCommitsEconomyAndDrainsEscrowTest,
	"Catfishing.Unit.Social.Service.SellStolenFishCommitsEconomyAndDrainsEscrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceProtectionSignBlocksTheftTest,
	"Catfishing.Unit.Social.Service.ProtectionSignBlocksTheftBeforeItemsEscrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceSharedTankTheftIgnoresOriginalFisherPresenceTest,
	"Catfishing.Unit.Social.Service.SharedTankTheftIgnoresOriginalFisherPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceStolenFishCarryIsPubliclyVisibleTest,
	"Catfishing.Unit.Social.Service.StolenFishCarryIsPubliclyVisibleAndClearsOnTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceHostRuntimePolicyGatesTheftTest,
	"Catfishing.Unit.Social.Service.HostRuntimePolicyGatesTheftAndMischief",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSocialServiceCatchTheftReplayIsIdempotentTest,
	"Catfishing.Unit.Social.Service.CatchTheftReplayReturnsFirstTerminalWithoutSecondReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：取得真实 Social WorldSubsystem，先验证空协议 teardown 可安全收口，再从公开命令入口提交缺身份请求并确认没有伪造成功。
bool FCatSocialServiceFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 SocialService 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatSocialService* Social = World ? World->GetSubsystem<UCatSocialService>() : nullptr;
	TestNotNull(TEXT("SocialService 测试 World 可用"), World);
	TestNotNull(TEXT("真实 SocialService 已创建"), Social);
	if (!Social)
	{
		return false;
	}

	TestTrue(TEXT("没有活跃偷鱼协议时 teardown 可以安全收口"), Social->CloseCommandsAndResolveAll());

	FCatTheftCommand TheftCommand;
	TheftCommand.Context.RequestId = FGuid::NewGuid();
	TheftCommand.Context.ExpectedRevision = 1;
	TheftCommand.FishInstanceId = FGuid::NewGuid();
	TheftCommand.SourceContainerId = FGuid::NewGuid();
	const FCatTheftResult TheftResult = Social->BeginTheft(nullptr, TheftCommand);
	TestFalse(TEXT("缺身份偷鱼不会提交"), TheftResult.Command.bCommitted);
	TestEqual(TEXT("缺身份偷鱼返回 PolicyUndecided"), TheftResult.Command.Error, ECatDomainCommandError::PolicyUndecided);
	TestFalse(TEXT("缺身份偷鱼不分配服务器 ProtocolId"), TheftResult.TheftProtocolId.IsValid());

	const FGuid MischiefRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult Mischief = Social->RequestMischief(nullptr, nullptr, MischiefRequestId);
	TestFalse(TEXT("缺身份恶作剧不会提交"), Mischief.bCommitted);
	TestEqual(TEXT("缺身份恶作剧返回 PolicyUndecided"), Mischief.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("恶作剧拒绝保留 RequestId"), Mischief.RequestId, MischiefRequestId);

	const FGuid ManualHelpRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult ManualHelp = Social->RequestManualHelp(
		nullptr, ManualHelpRequestId, ECatHelpSignalKind::ManualFishing);
	TestFalse(TEXT("缺身份手动求助不会提交"), ManualHelp.bCommitted);
	TestEqual(TEXT("缺身份手动求助返回 PolicyUndecided"), ManualHelp.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("手动求助拒绝保留 RequestId"), ManualHelp.RequestId, ManualHelpRequestId);

	const FCatTheftResult CatchUnknown = Social->CatchTheft(nullptr, FGuid::NewGuid());
	TestFalse(TEXT("缺身份追回不会提交"), CatchUnknown.Command.bCommitted);
	TestEqual(TEXT("缺身份追回返回 NotFound"), CatchUnknown.Command.Error, ECatDomainCommandError::NotFound);

	const FCatTheftResult SellUnknown = Social->SellStolenFish(nullptr, FGuid::NewGuid(), FGuid::NewGuid(), 1);
	TestFalse(TEXT("缺身份售出偷鱼不会提交"), SellUnknown.Command.bCommitted);
	TestEqual(TEXT("缺身份售出偷鱼返回 PolicyUndecided"), SellUnknown.Command.Error, ECatDomainCommandError::PolicyUndecided);
	return !HasAnyErrors();
}

// 测试流程：通过真实 Items 容器种鱼并开启偷鱼窗口；小偷必须先跑到商店锚点附近才卖得掉，站在受害者面前的那一次只能被拒且不写钱包。
// 到店后 SellStolenFish 必须完成 escrow 准备、ShopEconomy 入账和 escrow drain。成交价不由测试给出：断言先从服务器的体重轴问出这条鱼该值多少钱，
// 再核对钱包和账本收到的正是那个数，以此锁住"定价来自服务器"这条事实。随后验证重放不二次入账、载荷漂移拒绝，并且卖出
// 终态不能再追回、公开叼鱼事实同步清零。
bool FCatSocialServiceSellStolenFishCommitsEconomyAndDrainsEscrowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Social 售鱼成功测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("Social 售鱼成功测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	UCatShopEconomyService* Economy = World->GetSubsystem<UCatShopEconomyService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	TestNotNull(TEXT("ShopEconomyService 可用"), Economy);
	if (!Items || !Social || !Economy)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Victim = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("VictimStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("ThiefStableId"), FVector(100.0, 0.0, 0.0));
	TestNotNull(TEXT("受害者 Controller 可用"), Victim.Controller.Get());
	TestNotNull(TEXT("小偷 Controller 可用"), Thief.Controller.Get());
	TestNotNull(TEXT("受害者 Character 可用"), Victim.Character.Get());
	TestNotNull(TEXT("小偷 Character 可用"), Thief.Character.Get());
	if (!Victim.Controller || !Thief.Controller || !Victim.Character || !Thief.Character)
	{
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Victim.StableNetId, 4);
	TestNotNull(TEXT("受害者个人鱼护组件已创建"), Guard.Component.Get());
	if (!Guard.Component)
	{
		return false;
	}

	const FGuid FishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	const FCatCaptureCommitResult Capture = Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, FishId, GuardSnapshot.Revision));
	TestTrue(TEXT("测试鱼种入受害者鱼护"), Capture.Command.bCommitted);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("种鱼后鱼护包含测试鱼"), CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));

	// 期望金额同样从服务器的体重轴问一次，而不是在断言里写死一个数字。
	// 售价现在由服务器按这条鱼的重量算出来，所以本测试要锁的事实是"入账金额等于服务器按同一体重估出来的价"；
	// 写死常量只会在换档位表时变成一条与定价规则无关的巧合断言。重量也从容器快照里读回来，确保它就是这条鱼真实带着的那个值。
	const FCatFishInstance* SeededFish = GuardSnapshot.Fish.FindByPredicate(
		[FishId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishId;
		});
	TestNotNull(TEXT("能从鱼护快照读回测试鱼"), SeededFish);
	const double SeededWeightKilograms = SeededFish ? SeededFish->WeightKilograms : 0.0;
	TestTrue(TEXT("测试鱼带着非零体重"), SeededWeightKilograms > 0.0);
	int32 ExpectedSaleValue = 0;
	TestTrue(TEXT("测试鱼的体重能在体重轴上查到收购价"),
		Economy->TryAppraiseFishSale(SeededWeightKilograms, ExpectedSaleValue));
	TestTrue(TEXT("服务器估出来的收购价为正"), ExpectedSaleValue > 0);

	FCatTheftCommand TheftCommand;
	TheftCommand.Context.RequestId = FGuid::NewGuid();
	TheftCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	TheftCommand.FishInstanceId = FishId;
	TheftCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult Theft = Social->BeginTheft(Thief.Controller, TheftCommand);
	TestTrue(TEXT("偷取成功进入追回窗口"), Theft.Command.bCommitted);
	TestEqual(TEXT("偷取无错误"), Theft.Command.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("偷取分配协议 ID"), Theft.TheftProtocolId.IsValid());
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestFalse(TEXT("偷取后源鱼护暂时不含测试鱼"), CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));
	TestEqual(TEXT("偷取后公开叼鱼列表出现这一条"), Social->GetStolenFishCarriers().Num(), 1);

	// 商店摆在离受害者 5000 厘米的地方，而到店距离只有 400 厘米：这样"卖鱼"就必须真的跑一趟，构成飞书说的那段追回窗口。
	AActor* ShopAnchor = CatSocialServiceTest::SpawnShopAnchor(World, FVector(5000.0, 0.0, 0.0));
	TestNotNull(TEXT("测试商店锚点已摆放"), ShopAnchor);

	const FCatShopWalletSnapshot InitialWallet = Economy->GetWalletSnapshot();
	TestEqual(TEXT("售鱼前钱包余额为测试初始值"), InitialWallet.Balance, 0);
	TestEqual(TEXT("售鱼前钱包 Revision 已初始化"), InitialWallet.Revision, int64(1));
	const FGuid SaleRequestId = FGuid::NewGuid();
	const FCatTheftResult SaleAwayFromShop = Social->SellStolenFish(
		Thief.Controller, Theft.TheftProtocolId, SaleRequestId, InitialWallet.Revision);
	TestFalse(TEXT("小偷还站在受害者面前时卖不掉赃物"), SaleAwayFromShop.Command.bCommitted);
	TestEqual(TEXT("离商店太远的售鱼返回 InvalidPhase"), SaleAwayFromShop.Command.Error, ECatDomainCommandError::InvalidPhase);
	TestFalse(TEXT("离商店太远时不伪造 sold"), SaleAwayFromShop.bSold);
	TestEqual(TEXT("离商店太远时团队钱包不动"), Economy->GetWalletSnapshot().Balance, InitialWallet.Balance);
	TestEqual(TEXT("离商店太远时不写经济账本"), Economy->GetTransactionLedgerSnapshot().Num(), 0);

	// 位置拒绝不是终态：小偷跑到商店以后，同一个 RequestId 必须还能把这笔卖成，否则按早了一下按钮就等于永久卖不掉。
	Thief.Character->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	const FCatTheftResult Sale = Social->SellStolenFish(
		Thief.Controller, Theft.TheftProtocolId, SaleRequestId, InitialWallet.Revision);
	TestTrue(TEXT("售鱼首次提交成功"), Sale.Command.bCommitted);
	TestEqual(TEXT("售鱼首次提交无错误"), Sale.Command.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("售鱼终态标记 sold"), Sale.bSold);
	TestFalse(TEXT("售鱼后追回窗口关闭"), Sale.bRecoveryWindowOpen);
	TestEqual(TEXT("售出终态后公开叼鱼事实清零"), Social->GetStolenFishCarriers().Num(), 0);
	TestEqual(TEXT("售鱼返回钱包入账 Revision"), Sale.EconomyRevision, InitialWallet.Revision + 1);
	TestEqual(TEXT("售鱼后团队钱包按服务器估价入账"), Economy->GetWalletSnapshot().Balance, InitialWallet.Balance + ExpectedSaleValue);
	TestEqual(TEXT("售鱼后团队钱包 Revision 推进"), Economy->GetWalletSnapshot().Revision, InitialWallet.Revision + 1);

	const TArray<FCatShopTransactionRecord> LedgerAfterSale = Economy->GetTransactionLedgerSnapshot();
	TestEqual(TEXT("售鱼只写一条经济账本"), LedgerAfterSale.Num(), 1);
	if (LedgerAfterSale.Num() == 1)
	{
		TestEqual(TEXT("售鱼账本类别正确"), LedgerAfterSale[0].Kind, ECatShopTransactionKind::FishSale);
		TestEqual(TEXT("售鱼账本记录鱼实例"), LedgerAfterSale[0].FishInstanceId, FishId);
		TestEqual(TEXT("售鱼账本记录的钱包增量就是服务器估价"), LedgerAfterSale[0].WalletDelta, ExpectedSaleValue);
		TestEqual(TEXT("售鱼账本把来源记成偷来的 escrow 鱼"), LedgerAfterSale[0].FishSource, ECatShopFishSaleSource::StolenEscrow);
		TestEqual(TEXT("售鱼账本记录钱包 Revision"), LedgerAfterSale[0].WalletRevision, InitialWallet.Revision + 1);
	}
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestFalse(TEXT("售鱼 drain 后源鱼护不再包含测试鱼"), CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));

	const FCatTheftResult SaleReplay = Social->SellStolenFish(
		Thief.Controller, Theft.TheftProtocolId, SaleRequestId, InitialWallet.Revision);
	TestFalse(TEXT("售鱼重放不再次提交"), SaleReplay.Command.bCommitted);
	TestEqual(TEXT("售鱼重放返回 AlreadyResolved"), SaleReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("售鱼重放仍暴露 sold 终态"), SaleReplay.bSold);
	TestEqual(TEXT("售鱼重放保持钱包 Revision"), SaleReplay.EconomyRevision, Sale.EconomyRevision);
	TestEqual(TEXT("售鱼重放不二次入账"), Economy->GetWalletSnapshot().Balance, InitialWallet.Balance + ExpectedSaleValue);
	TestEqual(TEXT("售鱼重放不写第二条账本"), Economy->GetTransactionLedgerSnapshot().Num(), 1);

	// 成交价已经不再是入参，所以这里改用换一个公款版本前提来构造漂移。
	// 断言锁的事实没变：同一个 RequestId 换了业务意图就必须被拒，而不是安静地重放上一次的终态。
	const FCatTheftResult SaleDrift = Social->SellStolenFish(
		Thief.Controller, Theft.TheftProtocolId, SaleRequestId, InitialWallet.Revision + 1);
	TestFalse(TEXT("同 RequestId 售鱼载荷漂移不提交"), SaleDrift.Command.bCommitted);
	TestEqual(TEXT("同 RequestId 售鱼载荷漂移返回 InvalidPayload"), SaleDrift.Command.Error, ECatDomainCommandError::InvalidPayload);
	TestFalse(TEXT("售鱼载荷漂移不伪造 sold"), SaleDrift.bSold);
	TestEqual(TEXT("售鱼载荷漂移不写钱包"), Economy->GetWalletSnapshot().Balance, InitialWallet.Balance + ExpectedSaleValue);

	FCatTheftCommand DriftTheftCommand = TheftCommand;
	DriftTheftCommand.FishInstanceId = FGuid::NewGuid();
	const FCatTheftResult BeginDrift = Social->BeginTheft(Thief.Controller, DriftTheftCommand);
	TestFalse(TEXT("同 RequestId Begin 载荷漂移不提交"), BeginDrift.Command.bCommitted);
	TestEqual(TEXT("同 RequestId Begin 载荷漂移返回 InvalidPayload"), BeginDrift.Command.Error, ECatDomainCommandError::InvalidPayload);

	const FCatTheftResult BeginReplay = Social->BeginTheft(Thief.Controller, TheftCommand);
	TestFalse(TEXT("售鱼后 Begin 重放不再次提交"), BeginReplay.Command.bCommitted);
	TestEqual(TEXT("售鱼后 Begin 重放返回 AlreadyResolved"), BeginReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("售鱼后 Begin 重放不再暴露追回窗口"), BeginReplay.bRecoveryWindowOpen);
	TestTrue(TEXT("售鱼后 Begin 重放暴露 sold 终态"), BeginReplay.bSold);

	const FCatTheftResult CatchAfterSale = Social->CatchTheft(Victim.Controller, Theft.TheftProtocolId);
	TestFalse(TEXT("售鱼后追回不提交"), CatchAfterSale.Command.bCommitted);
	TestEqual(TEXT("售鱼后追回找不到活跃 escrow"), CatchAfterSale.Command.Error, ECatDomainCommandError::NotFound);
	TestFalse(TEXT("售鱼后追回不标记 returned"), CatchAfterSale.bReturned);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestFalse(TEXT("售鱼后测试鱼不会回到受害者鱼护"), CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));
	TestEqual(TEXT("售鱼后钱包保持首次入账"), Economy->GetWalletSnapshot().Balance, InitialWallet.Balance + ExpectedSaleValue);
	return !HasAnyErrors();
}

// 测试流程：受害者在自己脚下立防骚扰牌，小偷站在合法偷取距离内提交 BeginTheft；护栏必须以 PermissionDenied 拒绝，并且
// 拒绝发生在 Items 之前——源鱼护 Revision 不动、鱼还在，说明 escrow 一次都没被调用过。随后把受害者移出牌子半径，同一条
// 鱼、同样的距离必须能偷成功，证明拒绝确实来自牌子而不是别的准入条件。
bool FCatSocialServiceProtectionSignBlocksTheftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建防骚扰牌挡偷窃测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("防骚扰牌挡偷窃测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	if (!Items || !Social)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Victim = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("SignVictimStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("SignThiefStableId"), FVector(100.0, 0.0, 0.0));
	if (!Victim.Controller || !Thief.Controller || !Victim.Character || !Thief.Character)
	{
		AddError(TEXT("防骚扰牌测试玩家未能完整生成"));
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Victim.StableNetId, 4);
	TestNotNull(TEXT("受害者个人鱼护组件已创建"), Guard.Component.Get());
	if (!Guard.Component)
	{
		return false;
	}

	const FGuid FishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	const FCatCaptureCommitResult Capture = Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, FishId, GuardSnapshot.Revision));
	TestTrue(TEXT("测试鱼种入受害者鱼护"), Capture.Command.bCommitted);

	const FCatDomainCommandResult Placed = Social->PlaceProtectionSign(
		Victim.Controller, FGuid::NewGuid(), FVector::ZeroVector);
	TestTrue(TEXT("受害者可以在自己脚下立牌"), Placed.bCommitted);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	const int64 GuardRevisionBeforeBlockedTheft = GuardSnapshot.Revision;
	FCatTheftCommand BlockedCommand;
	BlockedCommand.Context.RequestId = FGuid::NewGuid();
	BlockedCommand.Context.ExpectedRevision = GuardRevisionBeforeBlockedTheft;
	BlockedCommand.FishInstanceId = FishId;
	BlockedCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult Blocked = Social->BeginTheft(Thief.Controller, BlockedCommand);
	TestFalse(TEXT("立牌后偷窃不提交"), Blocked.Command.bCommitted);
	TestEqual(TEXT("立牌后偷窃返回 PermissionDenied"), Blocked.Command.Error, ECatDomainCommandError::PermissionDenied);
	TestFalse(TEXT("被牌子挡住时不分配偷鱼协议 ID"), Blocked.TheftProtocolId.IsValid());
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestEqual(TEXT("被牌子挡住时源鱼护 Revision 不变，Items escrow 零调用"),
		GuardSnapshot.Revision, GuardRevisionBeforeBlockedTheft);
	TestTrue(TEXT("被牌子挡住时鱼仍留在受害者鱼护"),
		CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));

	// 只把受害者本人移出牌子半径，牌子、鱼护宿主和小偷都留在原地，让这次成功只能归因于护栏不再命中。
	Victim.Character->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	FCatTheftCommand AllowedCommand = BlockedCommand;
	AllowedCommand.Context.RequestId = FGuid::NewGuid();
	AllowedCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	const FCatTheftResult Allowed = Social->BeginTheft(Thief.Controller, AllowedCommand);
	TestTrue(TEXT("受害者走出牌子半径后同一次偷窃成功"), Allowed.Command.bCommitted);
	TestEqual(TEXT("走出半径后偷窃无错误"), Allowed.Command.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("走出半径后偷窃分配协议 ID"), Allowed.TheftProtocolId.IsValid());
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestFalse(TEXT("走出半径后鱼进入 escrow，不再留在鱼护"),
		CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));
	return !HasAnyErrors();
}

// 测试流程：把原钓手钓到的两条鱼分别留在他的个人鱼护和营地共用大鱼缸，然后让他离开自己的角色。共享缸装的是团队储备，
// 原钓手在不在场都不该改变它可被偷；个人鱼护偷的是在场玩家的私人东西，主人不在场仍必须拒绝。
bool FCatSocialServiceSharedTankTheftIgnoresOriginalFisherPresenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建共享缸偷鱼测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("共享缸偷鱼测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	if (!Items || !Social)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Fisher = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("TankFisherStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("TankThiefStableId"), FVector(100.0, 0.0, 0.0));
	if (!Fisher.Controller || !Thief.Controller || !Fisher.Character || !Thief.Character)
	{
		AddError(TEXT("共享缸测试玩家未能完整生成"));
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Fisher.StableNetId, 4);
	const CatSocialServiceTest::FRegisteredContainer Tank = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::SharedFishTank, FString(), 4);
	TestNotNull(TEXT("原钓手个人鱼护组件已创建"), Guard.Component.Get());
	TestNotNull(TEXT("营地共用大鱼缸组件已创建"), Tank.Component.Get());
	if (!Guard.Component || !Tank.Component)
	{
		return false;
	}

	const FGuid TankFishId = FGuid::NewGuid();
	const FGuid GuardFishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("第一条鱼种入原钓手鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, TankFishId, GuardSnapshot.Revision)).Command.bCommitted);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("第二条鱼种入原钓手鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, GuardFishId, GuardSnapshot.Revision)).Command.bCommitted);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatContainerSnapshot TankSnapshot = CatSocialServiceTest::GetSnapshot(Items, Tank.ContainerId);
	FCatFishTransferCommand Transfer;
	Transfer.Context.RequestId = FGuid::NewGuid();
	Transfer.Context.StableNetId = Fisher.StableNetId;
	Transfer.Context.ExpectedRevision = GuardSnapshot.Revision;
	Transfer.FishInstanceId = TankFishId;
	Transfer.SourceContainerId = Guard.ContainerId;
	Transfer.TargetContainerId = Tank.ContainerId;
	Transfer.ExpectedTargetRevision = TankSnapshot.Revision;
	TestTrue(TEXT("原钓手把第一条鱼存进共用大鱼缸"), Items->TransferOwnedFish(Transfer).bCommitted);
	TankSnapshot = CatSocialServiceTest::GetSnapshot(Items, Tank.ContainerId);
	TestTrue(TEXT("共用大鱼缸已持有团队储备鱼"),
		CatSocialServiceTest::SnapshotContainsFish(TankSnapshot, TankFishId));

	// 让原钓手脱离角色，模拟他掉线或离开：Social 之后再也解析不到他的可交互 Character。
	Fisher.Controller->UnPossess();

	FCatTheftCommand TankTheftCommand;
	TankTheftCommand.Context.RequestId = FGuid::NewGuid();
	TankTheftCommand.Context.ExpectedRevision = TankSnapshot.Revision;
	TankTheftCommand.FishInstanceId = TankFishId;
	TankTheftCommand.SourceContainerId = Tank.ContainerId;
	const FCatTheftResult TankTheft = Social->BeginTheft(Thief.Controller, TankTheftCommand);
	TestTrue(TEXT("原钓手不在场时共享缸仍可被偷"), TankTheft.Command.bCommitted);
	TestEqual(TEXT("共享缸偷鱼无错误"), TankTheft.Command.Error, ECatDomainCommandError::None);
	TankSnapshot = CatSocialServiceTest::GetSnapshot(Items, Tank.ContainerId);
	TestFalse(TEXT("共享缸偷鱼后团队储备鱼进入 escrow"),
		CatSocialServiceTest::SnapshotContainsFish(TankSnapshot, TankFishId));

	// 同一个不在场的主人，个人鱼护必须仍然拒绝：这条护栏保护的是在场玩家的私人东西，不能跟着共享缸一起放开。
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatTheftCommand GuardTheftCommand;
	GuardTheftCommand.Context.RequestId = FGuid::NewGuid();
	GuardTheftCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	GuardTheftCommand.FishInstanceId = GuardFishId;
	GuardTheftCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult GuardTheft = Social->BeginTheft(Thief.Controller, GuardTheftCommand);
	TestFalse(TEXT("原钓手不在场时个人鱼护不可被偷"), GuardTheft.Command.bCommitted);
	TestEqual(TEXT("个人鱼护主人不在场返回 DependencyUnavailable"),
		GuardTheft.Command.Error, ECatDomainCommandError::DependencyUnavailable);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("个人鱼护拒绝后鱼仍留在原处"),
		CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, GuardFishId));
	return !HasAnyErrors();
}
// 测试流程：受害者鱼护里先放两条鱼。第一条被偷走后，从只有客户端才拿得到的那种视角（PlayerState）去读服务发布的公开叼鱼列表，
// 必须能认出"小偷正叼着一条赃物"，并读到鱼种、赃物标记和版本；旁观者和受害者本人都不会被误标进去。
// 随后依次走两个终态：受害者追回，以及进食窗口自然到期。两次之后公开事实都必须清零，绝不能留下一只永远叼着不存在的鱼的猫。
// 说明：本测试 World 没有装载鱼目录，"TestFish" 查不到定义，所以窗口到期这一支实际收在"原样返还"而不是"吃掉"。
// 这里锁的不变量是"协议离开活跃表之后公开事实必须跟着清零"，对返还、吃掉、售出三条出口都成立；售出那一条由售鱼测试单独锁。
bool FCatSocialServiceStolenFishCarryIsPubliclyVisibleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建公开叼鱼可见性测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("公开叼鱼可见性测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	if (!Items || !Social)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Victim = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("CarryVictimStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("CarryThiefStableId"), FVector(100.0, 0.0, 0.0));
	const CatSocialServiceTest::FTestPlayer Bystander = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("CarryBystanderStableId"), FVector(200.0, 0.0, 0.0));
	if (!Victim.Controller || !Thief.Controller || !Bystander.Controller
		|| !Victim.Character || !Thief.Character || !Bystander.Character)
	{
		AddError(TEXT("公开叼鱼可见性测试玩家未能完整生成"));
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Victim.StableNetId, 4);
	TestNotNull(TEXT("受害者个人鱼护组件已创建"), Guard.Component.Get());
	if (!Guard.Component)
	{
		return false;
	}

	const FGuid FirstFishId = FGuid::NewGuid();
	const FGuid SecondFishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("第一条测试鱼种入受害者鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, FirstFishId, GuardSnapshot.Revision)).Command.bCommitted);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("第二条测试鱼种入受害者鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, SecondFishId, GuardSnapshot.Revision)).Command.bCommitted);

	TestEqual(TEXT("没人偷鱼时公开叼鱼列表是空的"), Social->GetStolenFishCarriers().Num(), 0);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatTheftCommand FirstTheftCommand;
	FirstTheftCommand.Context.RequestId = FGuid::NewGuid();
	FirstTheftCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	FirstTheftCommand.FishInstanceId = FirstFishId;
	FirstTheftCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult FirstTheft = Social->BeginTheft(Thief.Controller, FirstTheftCommand);
	TestTrue(TEXT("第一次偷取进入追回窗口"), FirstTheft.Command.bCommitted);

	TestEqual(TEXT("偷取后公开叼鱼列表只有小偷一条"), Social->GetStolenFishCarriers().Num(), 1);
	const FCatStolenFishCarrySnapshot* ThiefCarry = CatSocialServiceTest::FindPublicCarrier(Social, Thief.PlayerState);
	TestNotNull(TEXT("第三方能从公开状态认出正在叼鱼的小偷"), ThiefCarry);
	TestNull(TEXT("旁观者不会被误标成叼鱼"),
		CatSocialServiceTest::FindPublicCarrier(Social, Bystander.PlayerState));
	TestNull(TEXT("受害者不会被误标成叼鱼"),
		CatSocialServiceTest::FindPublicCarrier(Social, Victim.PlayerState));
	int64 CarryRevisionWhileHolding = 0;
	if (ThiefCarry)
	{
		TestEqual(TEXT("公开状态暴露嘴里那条鱼的鱼种"), ThiefCarry->FishDefinitionId, FName(TEXT("TestFish")));
		TestTrue(TEXT("公开状态明确标出这是赃物"), ThiefCarry->bStolen);
		TestTrue(TEXT("公开状态带有效版本"), ThiefCarry->Revision > 0);
		CarryRevisionWhileHolding = ThiefCarry->Revision;
	}

	const FCatTheftResult Caught = Social->CatchTheft(Victim.Controller, FirstTheft.TheftProtocolId);
	TestTrue(TEXT("受害者在窗口内追回成功"), Caught.bReturned);
	TestEqual(TEXT("追回终态后公开叼鱼事实清零"), Social->GetStolenFishCarriers().Num(), 0);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatTheftCommand SecondTheftCommand;
	SecondTheftCommand.Context.RequestId = FGuid::NewGuid();
	SecondTheftCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	SecondTheftCommand.FishInstanceId = SecondFishId;
	SecondTheftCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult SecondTheft = Social->BeginTheft(Thief.Controller, SecondTheftCommand);
	TestTrue(TEXT("第二次偷取进入追回窗口"), SecondTheft.Command.bCommitted);
	const FCatStolenFishCarrySnapshot* SecondCarry = CatSocialServiceTest::FindPublicCarrier(Social, Thief.PlayerState);
	TestNotNull(TEXT("第二次偷取重新出现在公开列表"), SecondCarry);
	if (SecondCarry)
	{
		TestTrue(TEXT("公开叼鱼版本单调推进"), SecondCarry->Revision > CarryRevisionWhileHolding);
	}

	// 直接推进本 World 的定时器，让进食窗口自然走完；这里不改窗口时长，用的就是本测试覆盖里的 30 秒。
	// 必须推两拍：FTimerManager 对"本帧还没 Tick 过就设定的计时器"先放进 Pending，
	// 直到某次 Tick 收尾才把它转成 Active 并按当时的 InternalTime 重算到期时刻，所以第一拍不可能让它到期；
	// 同一帧内的第二次 Tick 又会被 HasBeenTickedThisFrame 挡回，中间必须递增 GFrameCounter 才跨得过去。
	// 只推一拍的后果不是"晚一点触发"，而是窗口回调一次都不会跑，下面两条断言实际什么都没验到。
	World->GetTimerManager().Tick(0.0f);
	++GFrameCounter;
	World->GetTimerManager().Tick(31.0f);
	TestEqual(TEXT("窗口到期收口后公开叼鱼事实清零"), Social->GetStolenFishCarriers().Num(), 0);
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("窗口到期且吃不成时鱼原样回到受害者鱼护"),
		CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, SecondFishId));
	return !HasAnyErrors();
}

// 测试流程：先确认运行期策略确实是从 Settings 抄来的开局默认值，再验证局主未登记时谁都改不了策略。
// 登记局主之后，非局主仍必须被拒且不动版本；局主关掉偷取后，真实 BeginTheft 必须返回 PermissionDenied 且鱼一条都不动，恶作剧同样被拒。
// 最后确认重复提交同一份权限不推进版本、重新打开后偷取立刻恢复，并且整个过程里策略版本只增不减。
bool FCatSocialServiceHostRuntimePolicyGatesTheftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建局主运行期权限测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("局主运行期权限测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	if (!Items || !Social)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Host = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("PolicyHostStableId"), FVector(50.0, 0.0, 0.0));
	const CatSocialServiceTest::FTestPlayer Victim = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("PolicyVictimStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("PolicyThiefStableId"), FVector(100.0, 0.0, 0.0));
	if (!Host.Controller || !Victim.Controller || !Thief.Controller
		|| !Host.Character || !Victim.Character || !Thief.Character)
	{
		AddError(TEXT("局主运行期权限测试玩家未能完整生成"));
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Victim.StableNetId, 4);
	TestNotNull(TEXT("受害者个人鱼护组件已创建"), Guard.Component.Get());
	if (!Guard.Component)
	{
		return false;
	}

	const FGuid FishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("测试鱼种入受害者鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, FishId, GuardSnapshot.Revision)).Command.bCommitted);

	const int64 InitialPolicyRevision = Social->GetSocialPolicy().Revision;
	TestEqual(TEXT("运行期偷取权限从 Settings 默认值起步"),
		Social->GetSocialPolicy().TheftPermission, ECatDomainPolicy::Enabled);
	TestTrue(TEXT("运行期策略开局带有效版本"), InitialPolicyRevision > 0);

	// 局主还没登记：这时没有任何人有资格改策略，连之后真正的局主也不行。
	const FCatDomainCommandResult BeforeHostKnown = Social->SetSocialPolicy(
		Host.Controller, FGuid::NewGuid(), ECatDomainPolicy::Disabled, ECatDomainPolicy::Disabled);
	TestFalse(TEXT("局主未登记时改策略不提交"), BeforeHostKnown.bCommitted);
	TestEqual(TEXT("局主未登记时改策略返回 PermissionDenied"),
		BeforeHostKnown.Error, ECatDomainCommandError::PermissionDenied);
	TestEqual(TEXT("局主未登记时策略版本不动"), Social->GetSocialPolicy().Revision, InitialPolicyRevision);

	Social->SetHostAuthority(Host.Controller);

	const FCatDomainCommandResult NonHostAttempt = Social->SetSocialPolicy(
		Thief.Controller, FGuid::NewGuid(), ECatDomainPolicy::Disabled, ECatDomainPolicy::Disabled);
	TestFalse(TEXT("非局主改策略不提交"), NonHostAttempt.bCommitted);
	TestEqual(TEXT("非局主改策略返回 PermissionDenied"),
		NonHostAttempt.Error, ECatDomainCommandError::PermissionDenied);
	TestEqual(TEXT("非局主改策略不动策略版本"), Social->GetSocialPolicy().Revision, InitialPolicyRevision);
	TestEqual(TEXT("非局主改策略不动偷取权限"),
		Social->GetSocialPolicy().TheftPermission, ECatDomainPolicy::Enabled);

	const FCatDomainCommandResult HostCloses = Social->SetSocialPolicy(
		Host.Controller, FGuid::NewGuid(), ECatDomainPolicy::Disabled, ECatDomainPolicy::Disabled);
	TestTrue(TEXT("局主关闭偷取与恶作剧成功提交"), HostCloses.bCommitted);
	TestEqual(TEXT("局主关闭后偷取权限为 Disabled"),
		Social->GetSocialPolicy().TheftPermission, ECatDomainPolicy::Disabled);
	const int64 ClosedPolicyRevision = Social->GetSocialPolicy().Revision;
	TestTrue(TEXT("局主改动推进策略版本"), ClosedPolicyRevision > InitialPolicyRevision);
	TestEqual(TEXT("策略命令回执带当前版本"), HostCloses.Revision, ClosedPolicyRevision);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	const int64 GuardRevisionBeforeBlockedTheft = GuardSnapshot.Revision;
	FCatTheftCommand BlockedCommand;
	BlockedCommand.Context.RequestId = FGuid::NewGuid();
	BlockedCommand.Context.ExpectedRevision = GuardRevisionBeforeBlockedTheft;
	BlockedCommand.FishInstanceId = FishId;
	BlockedCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult BlockedTheft = Social->BeginTheft(Thief.Controller, BlockedCommand);
	TestFalse(TEXT("局主关闭偷取后偷鱼不提交"), BlockedTheft.Command.bCommitted);
	TestEqual(TEXT("局主关闭偷取后偷鱼返回 PermissionDenied"),
		BlockedTheft.Command.Error, ECatDomainCommandError::PermissionDenied);
	TestFalse(TEXT("局主关闭偷取后不分配偷鱼协议 ID"), BlockedTheft.TheftProtocolId.IsValid());
	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestEqual(TEXT("局主关闭偷取后源鱼护 Revision 不变"),
		GuardSnapshot.Revision, GuardRevisionBeforeBlockedTheft);
	TestTrue(TEXT("局主关闭偷取后鱼仍留在受害者鱼护"),
		CatSocialServiceTest::SnapshotContainsFish(GuardSnapshot, FishId));
	TestEqual(TEXT("局主关闭偷取后没有任何人被标成叼鱼"), Social->GetStolenFishCarriers().Num(), 0);

	const FCatDomainCommandResult BlockedMischief = Social->RequestMischief(
		Thief.Controller, Victim.Controller, FGuid::NewGuid());
	TestFalse(TEXT("局主关闭恶作剧后恶作剧不提交"), BlockedMischief.bCommitted);
	TestEqual(TEXT("局主关闭恶作剧后恶作剧返回 PermissionDenied"),
		BlockedMischief.Error, ECatDomainCommandError::PermissionDenied);

	// 同一份权限再提交一次：状态没有变化，因此既不算新写入，也不该推进版本。
	const FCatDomainCommandResult HostRepeats = Social->SetSocialPolicy(
		Host.Controller, FGuid::NewGuid(), ECatDomainPolicy::Disabled, ECatDomainPolicy::Disabled);
	TestFalse(TEXT("重复提交同一份权限不算新写入"), HostRepeats.bCommitted);
	TestEqual(TEXT("重复提交同一份权限返回 AlreadyResolved"),
		HostRepeats.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重复提交同一份权限不推进版本"),
		Social->GetSocialPolicy().Revision, ClosedPolicyRevision);

	const FCatDomainCommandResult HostReopens = Social->SetSocialPolicy(
		Host.Controller, FGuid::NewGuid(), ECatDomainPolicy::Enabled, ECatDomainPolicy::Disabled);
	TestTrue(TEXT("局主重新打开偷取成功提交"), HostReopens.bCommitted);
	TestTrue(TEXT("策略版本继续单调推进"), Social->GetSocialPolicy().Revision > ClosedPolicyRevision);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatTheftCommand AllowedCommand;
	AllowedCommand.Context.RequestId = FGuid::NewGuid();
	AllowedCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	AllowedCommand.FishInstanceId = FishId;
	AllowedCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult AllowedTheft = Social->BeginTheft(Thief.Controller, AllowedCommand);
	TestTrue(TEXT("局主重新打开偷取后同一条鱼可以被偷"), AllowedTheft.Command.bCommitted);
	TestEqual(TEXT("重新打开后偷鱼无错误"), AllowedTheft.Command.Error, ECatDomainCommandError::None);
	return !HasAnyErrors();
}

// 测试流程：受害者在窗口内追回一次，然后用完全相同的 Controller 与 ProtocolId 再点一次追回（网络重发、客户端重试都长这样）。
// 锁的不变量有两条。一是重放必须交回第一次那份终态，并且明说"这次没有发生写入"：拿不到 AlreadyResolved 的调用方会以为
// 自己又追回了一条鱼。二是重放绝不能真的再走一遍返还——鱼只有一条，返还两次就是凭空多一条鱼。
// 顺带锁住终态键的隔离：换一个 ProtocolId 必须落到 NotFound，而不是被上一份缓存顺手答复成"追回成功"。
bool FCatSocialServiceCatchTheftReplayIsIdempotentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatSocialServiceTest::FSocialSaleSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建追回重放测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("追回重放测试 World 可用"), World);
	if (!World)
	{
		return false;
	}

	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	UCatSocialService* Social = World->GetSubsystem<UCatSocialService>();
	TestNotNull(TEXT("ItemsService 可用"), Items);
	TestNotNull(TEXT("SocialService 可用"), Social);
	if (!Items || !Social)
	{
		return false;
	}

	const CatSocialServiceTest::FTestPlayer Victim = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("CatchReplayVictimStableId"), FVector::ZeroVector);
	const CatSocialServiceTest::FTestPlayer Thief = CatSocialServiceTest::SpawnPlayer(
		World, TEXT("CatchReplayThiefStableId"), FVector(100.0, 0.0, 0.0));
	if (!Victim.Controller || !Thief.Controller || !Victim.Character || !Thief.Character)
	{
		AddError(TEXT("追回重放测试玩家未能完整生成"));
		return false;
	}

	const CatSocialServiceTest::FRegisteredContainer Guard = CatSocialServiceTest::RegisterContainer(
		World, Items, ECatContainerKind::PersonalGuard, Victim.StableNetId, 4);
	TestNotNull(TEXT("受害者个人鱼护组件已创建"), Guard.Component.Get());
	if (!Guard.Component)
	{
		return false;
	}

	const FGuid FishId = FGuid::NewGuid();
	FCatContainerSnapshot GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestTrue(TEXT("测试鱼种入受害者鱼护"), Items->CommitCapture(
		CatSocialServiceTest::MakeCaptureCommand(Guard, FishId, GuardSnapshot.Revision)).Command.bCommitted);

	GuardSnapshot = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	FCatTheftCommand TheftCommand;
	TheftCommand.Context.RequestId = FGuid::NewGuid();
	TheftCommand.Context.ExpectedRevision = GuardSnapshot.Revision;
	TheftCommand.FishInstanceId = FishId;
	TheftCommand.SourceContainerId = Guard.ContainerId;
	const FCatTheftResult Theft = Social->BeginTheft(Thief.Controller, TheftCommand);
	TestTrue(TEXT("偷取进入追回窗口"), Theft.Command.bCommitted);
	if (!Theft.Command.bCommitted)
	{
		return false;
	}

	const FCatTheftResult Caught = Social->CatchTheft(Victim.Controller, Theft.TheftProtocolId);
	TestTrue(TEXT("受害者在窗口内追回成功"), Caught.bReturned);
	TestTrue(TEXT("首次追回是一次真实写入"), Caught.Command.bCommitted);
	const FCatContainerSnapshot SnapshotAfterCatch = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestEqual(TEXT("追回后鱼护里就这一条鱼"), SnapshotAfterCatch.Fish.Num(), 1);
	TestTrue(TEXT("追回后鱼回到受害者鱼护"),
		CatSocialServiceTest::SnapshotContainsFish(SnapshotAfterCatch, FishId));

	const FCatTheftResult CaughtReplay = Social->CatchTheft(Victim.Controller, Theft.TheftProtocolId);
	TestFalse(TEXT("追回重放不再声称本次发生了写入"), CaughtReplay.Command.bCommitted);
	TestEqual(TEXT("追回重放返回 AlreadyResolved"),
		CaughtReplay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestTrue(TEXT("追回重放仍交回首次那份 returned 终态"), CaughtReplay.bReturned);
	TestEqual(TEXT("追回重放指向同一份协议"), CaughtReplay.TheftProtocolId, Theft.TheftProtocolId);
	TestEqual(TEXT("追回重放指向同一条鱼"), CaughtReplay.FishInstanceId, FishId);
	const FCatContainerSnapshot SnapshotAfterReplay = CatSocialServiceTest::GetSnapshot(Items, Guard.ContainerId);
	TestEqual(TEXT("追回重放没有把鱼复制成第二条"), SnapshotAfterReplay.Fish.Num(), 1);
	TestEqual(TEXT("追回重放没有再动一次容器 Revision"),
		SnapshotAfterReplay.Revision, SnapshotAfterCatch.Revision);

	const FCatTheftResult UnknownProtocolCatch = Social->CatchTheft(Victim.Controller, FGuid::NewGuid());
	TestFalse(TEXT("换一个协议 ID 不会被上一份缓存答复成追回成功"), UnknownProtocolCatch.bReturned);
	TestEqual(TEXT("换一个协议 ID 返回 NotFound"),
		UnknownProtocolCatch.Command.Error, ECatDomainCommandError::NotFound);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
