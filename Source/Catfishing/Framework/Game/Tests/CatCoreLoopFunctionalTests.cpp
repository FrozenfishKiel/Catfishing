#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Items/CatFishTankActor.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "Run/CatRunSettings.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCoreLoopFunctionalTest,
	"Catfishing.Functional.CoreLoop.RealTickedWorldRunsDayCatchCampSacrificeAndSettlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatCoreLoopFunctionalTest
{
	/** 本用例把一天压缩成 90 秒：够完成两次咬钩约 13 秒的捕获加营地操作，又不用真等 20 分钟。 */
	constexpr float DayLengthSecondsOverride = 90.0f;

	/** 单人局把当日额度压到 1 条：一条鱼献祭就算达标，让成功结算在一天内可达。 */
	constexpr int32 QuotaTargetOverride = 1;

	/** 毕业日压到第 1 天：当晚全员确认翻天时额度已齐，直接走成功结算，而不是进入第 2 天。 */
	constexpr int32 FinalDayIndexOverride = 1;

	/** 真实 tick 的步长；30fps 是 PIE 的常见帧率，比它更细只会拖慢用例，不会改变任何判定。 */
	constexpr float TickStepSeconds = 1.0f / 30.0f;

	/**
	 * Run 节奏覆盖守卫：把全局 Run 配置临时改成"90 秒白天 / 额度 1 / 第 1 天毕业"，析构时逐项写回。
	 * 项目 ini 的正式节奏是 20 分钟白天、第 10 天毕业——功能测试要走完整局，不压缩节奏就没法在自动化里跑。
	 */
	struct FCoreLoopRunSettingsOverride
	{
		/** 被临时覆盖的全局 Run 配置默认对象；为空表示没取到，本守卫不改写也不恢复任何字段。 */
		UCatRunSettings* Settings = nullptr;

		/** 覆盖前的白天秒数，析构时写回。 */
		float SavedDayLengthSeconds = 0.0f;

		/** 覆盖前的固定额度，析构时写回。 */
		int32 SavedQuotaTarget = 0;

		/** 覆盖前的毕业日序号，析构时写回。 */
		int32 SavedFinalDayIndex = 0;

		FCoreLoopRunSettingsOverride()
		{
			Settings = GetMutableDefault<UCatRunSettings>();
			if (Settings)
			{
				SavedDayLengthSeconds = Settings->DayLengthSeconds;
				SavedQuotaTarget = Settings->QuotaTarget;
				SavedFinalDayIndex = Settings->FinalDayIndex;
				Settings->DayLengthSeconds = DayLengthSecondsOverride;
				Settings->QuotaTarget = QuotaTargetOverride;
				Settings->FinalDayIndex = FinalDayIndexOverride;
			}
		}

		~FCoreLoopRunSettingsOverride()
		{
			if (Settings)
			{
				Settings->DayLengthSeconds = SavedDayLengthSeconds;
				Settings->QuotaTarget = SavedQuotaTarget;
				Settings->FinalDayIndex = SavedFinalDayIndex;
			}
		}
	};

	/**
	 * 鱼目录覆盖守卫：把全局鱼目录临时换成一条固定重量的弱鱼，析构时整表写回。
	 * 换目录不是为了绕开抽鱼代码——抽取仍走真实的目录接口——而是为了让搏斗结果可预测：
	 * 真实鱼表按均匀抽取会抽到力量高于 starter 竿强度（25）的鱼，向外游时拖竿会按判定表①断竿，
	 * 用例就随抽签结果时红时绿。固定成一条 2 公斤、力量 10 的鱼后，猫力 50 ≥ 鱼力×2 恒成立，
	 * 判定表③绝对碾压是唯一出口，用例每次都走同一条路。
	 */
	struct FCoreLoopFishCatalogOverride
	{
		/** 被临时覆盖的全局鱼目录默认对象；为空表示没取到，本守卫不改写也不恢复任何字段。 */
		UCatFishCatalogSettings* Settings = nullptr;

		/** 覆盖期间保活的临时鱼定义；析构时从根集合摘除交还 GC。 */
		UCatFishDefinition* Definition = nullptr;

		/** 覆盖前的目录 Schema 版本，析构时写回。 */
		int32 SavedContentSchemaVersion = 0;

		/** 覆盖前的目录数据版本，析构时写回。 */
		int64 SavedDataRevision = 0;

		/** 覆盖前的飞书来源戳，析构时写回。 */
		FCatDataCatalogSourceStamp SavedSourceStamp;

		/** 覆盖前的定义清单，析构时写回。 */
		TArray<TSoftObjectPtr<UCatFishDefinition>> SavedDefinitions;

		FCoreLoopFishCatalogOverride()
		{
			Settings = GetMutableDefault<UCatFishCatalogSettings>();
			if (!Settings)
			{
				return;
			}
			SavedContentSchemaVersion = Settings->ContentSchemaVersion;
			SavedDataRevision = Settings->DataRevision;
			SavedSourceStamp = Settings->SourceStamp;
			SavedDefinitions = Settings->Definitions;

			Definition = NewObject<UCatFishDefinition>(GetTransientPackage(), TEXT("CatCoreLoopFunctionalFish"));
			Definition->AddToRoot();
			Definition->bEnableRuntimeDefinition = true;
			Definition->FishDefinitionId = TEXT("CoreLoopWeakFish");
			Definition->BodyClass = ECatFishBodyClass::Standard;
			Definition->SacrificeContribution = 1;
			Definition->RegionIds = {TEXT("River")};
			Definition->ChumAffinities = {ECatChumAffinity::Fishy};
			// 最小重量 == 最大重量：抽取时重量不再随机，鱼力按重量缩放后恒等于 10，判定结果因此可预测。
			Definition->MinimumWeightKilograms = 2.0;
			Definition->MaximumWeightKilograms = 2.0;
			Definition->MinimumFightParticipants = 1;
			Definition->FishStrength = 10.0;
			Definition->FishFightStamina = 15.0;
			Definition->FoodSafety = ECatFishFoodSafety::Safe;
			Definition->bTankDisplayEligible = true;

			// GameMode StartPlay 会用同一个校验器把鱼与装备当一个内容包整体校验，戳与版本缺一不可，
			// 否则整局会以 DataCatalogInvalid 拒绝启动，本用例连白天都进不去。
			Settings->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
			Settings->DataRevision = 1;
			Settings->SourceStamp.SourceKind = TEXT("Automation");
			Settings->SourceStamp.SourceNodeToken = TEXT("CatCoreLoopFunctionalTest");
			Settings->SourceStamp.SourceRevision = 1;
			Settings->SourceStamp.SourceSliceName = TEXT("CoreLoopWeakFish");
			Settings->Definitions.Reset();
			Settings->Definitions.Add(Definition);
		}

		~FCoreLoopFishCatalogOverride()
		{
			if (Settings)
			{
				Settings->ContentSchemaVersion = SavedContentSchemaVersion;
				Settings->DataRevision = SavedDataRevision;
				Settings->SourceStamp = SavedSourceStamp;
				Settings->Definitions = SavedDefinitions;
			}
			if (Definition)
			{
				Definition->RemoveFromRoot();
			}
		}
	};

	/** 一名已通过真实准入并占有 Character 的玩家；任一为空表示装配失败。 */
	struct FPlayerFixture
	{
		/** 走过真实 PreLogin/PostLogin 的项目 PlayerController。 */
		TObjectPtr<ACatfishingPlayerController> Controller = nullptr;

		/** PostLogin 由引擎 RestartPlayer 在 PlayerStart 生成并占有的项目 Character。 */
		TObjectPtr<ACatCharacter> Character = nullptr;

		/** 该玩家的服务器私有身份字符串。 */
		FString StableNetId;
	};

	// 玩家装配流程：与捕获切片同骨架，但用在**已 BeginPlay**的 World 上——PostLogin 会触发引擎的
	// RestartPlayer，Character 由 GameMode 按 DefaultPawnClass 在 PlayerStart 自动生成并占有，
	// 而不是测试手动 Spawn；这正是生产路径。
	static FPlayerFixture AdmitPlayer(FAutomationTestBase& Test, UWorld& World, const FString& StableNetId)
	{
		FPlayerFixture Fixture;
		ACatfishingGameModeBase* GameMode = World.GetAuthGameMode<ACatfishingGameModeBase>();
		Test.TestNotNull(TEXT("已开局 World 持有项目 GameMode"), GameMode);
		if (!GameMode)
		{
			return Fixture;
		}
		const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
			StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
		const FUniqueNetIdRepl UniqueId(StableUniqueId);
		FString PreLoginError;
		GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
		Test.TestTrue(TEXT("玩家身份 PreLogin 通过"), PreLoginError.IsEmpty());

		ACatfishingPlayerController* Controller = World.SpawnActor<ACatfishingPlayerController>();
		ACatfishingPlayerState* PlayerState = World.SpawnActor<ACatfishingPlayerState>();
		UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
		if (!Controller || !PlayerState || !TestPlayer || !PreLoginError.IsEmpty())
		{
			return Fixture;
		}
		TestPlayer->CurrentNetSpeed = 10000;
		Controller->SetPlayer(TestPlayer);
		PlayerState->SetUniqueId(UniqueId);
		Controller->SetPlayerState(PlayerState);
		World.AddController(Controller);
		GameMode->PostLogin(Controller);

		ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
		Test.TestNotNull(TEXT("PostLogin 后引擎已按生产路径生成并占有 Character"), Character);
		if (!Character)
		{
			return Fixture;
		}
		// 测试 World 没有地面，重力会把猫在几秒内拖出水域和营地半径，后面的每一步都会被距离校验误拒。
		// 改成飞行模式钉住位置：本用例验证的是命令链与局流程，不验证行走物理。
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Fixture.Controller = Controller;
		Fixture.Character = Character;
		Fixture.StableNetId = StableNetId;
		return Fixture;
	}

	// 鱼缸挂接流程：SharedFishTank 是营地的私有 EditInstanceOnly 属性（正式流程由关卡作者在编辑器里指定），
	// 测试里沿用 Camp 用例的既有做法，按属性名反射写入，不给生产类型加测试后门。
	static bool AttachSharedFishTank(ACatCampHubActor* Camp, ACatFishTankActor* Tank)
	{
		FObjectProperty* SharedTankProperty = FindFProperty<FObjectProperty>(ACatCampHubActor::StaticClass(), TEXT("SharedFishTank"));
		if (!Camp || !Tank || !SharedTankProperty)
		{
			return false;
		}
		SharedTankProperty->SetObjectPropertyValue_InContainer(Camp, Tank);
		return true;
	}

	// 条件推进流程：按固定步长真实 tick World（计时器、StateTree 组件、窝料衰减、落水置湿都在走），
	// 直到条件成立或超出模拟时长上限；返回是否等到。上限按模拟秒数而不是真实秒数计。
	static bool TickUntil(FTestWorldWrapper& Wrapper, const float MaxSimulatedSeconds, TFunctionRef<bool()> Condition)
	{
		const int32 MaxSteps = FMath::CeilToInt32(MaxSimulatedSeconds / TickStepSeconds);
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			if (Condition())
			{
				return true;
			}
			Wrapper.TickTestWorld(TickStepSeconds);
		}
		return Condition();
	}

	// 会话查找流程：全 World 扫非终态的钓鱼会话；本用例只有一名钓手，最多存在一个。
	static ACatFishingSession* FindActiveFishingSession(UWorld& World)
	{
		for (TActorIterator<ACatFishingSession> It(&World); It; ++It)
		{
			if (!It->IsTerminal())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

// 测试流程（这是全项目唯一在真实 tick 的 World 里从开局跑到局终的用例）：
// 1. 覆盖 Run 节奏（90 秒白天 / 额度 1 / 第 1 天毕业）与鱼目录（一条固定 2kg、力量 10 的弱鱼），
//    在 World BeginPlay 前摆好 PlayerStart、River 水域、营地与共享鱼缸——BeginPlay 走的是真实
//    StartPlay：数据目录校验、ST_RunFlow 启动、白天计时器全部按生产路径建立。
// 2. 玩家经真实 PreLogin/PostLogin 入局，Character 由引擎 RestartPlayer 生成。
// 3. 白天：领免费饵 → 公款买窝料 → 站在水里投窝（咬钩间隔从 120 秒缩到约 13 秒）→ 连钓两条鱼
//    （真咬期上报拖＝完美中鱼，向外游+拖对弱鱼命中判定表③碾压进近岸，抄网入护）。
// 4. 营地：把一条鱼转进共享鱼缸，再做一次营地休息。
// 5. 入夜：白天计时器到点自动进普通夜（不是测试代码切相位）；献祭鱼护里剩下的鱼，额度 1/1。
// 6. 翻天确认：唯一玩家 ready → 额度已齐 + 第 1 天即毕业日 → 走成功结算夜 → 结算完成 → Ending → Ended。
// 全程只通过玩家可用的入口（Controller RPC 与营地命令）驱动，不调用任何 Seed/Bypass。
bool FCatCoreLoopFunctionalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatCoreLoopFunctionalTest;

	FCoreLoopRunSettingsOverride RunOverride;
	FCoreLoopFishCatalogOverride CatalogOverride;
	if (!TestNotNull(TEXT("Run 配置可覆盖"), RunOverride.Settings)
		|| !TestNotNull(TEXT("鱼目录可覆盖"), CatalogOverride.Settings))
	{
		return false;
	}

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建核心循环功能测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("功能测试 World 可创建"), World))
	{
		return false;
	}

	// 先注册项目 GameMode 再摆场景：BeginPlayInTestWorld 内部的 SetGameMode 对已存在的 GameMode 是空操作，
	// 所以这里注册的才是真正生效的那一个。
	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	if (!TestTrue(TEXT("功能测试 World 可注册项目 GameMode"), World->SetGameMode(GameModeUrl)))
	{
		return false;
	}

	// 场景四件套都在 BeginPlay 前摆好，让它们的 BeginPlay 走真实注册路径（鱼缸在 BeginPlay 注册共享容器）。
	APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	ACatWaterRegion* WaterRegion = World->SpawnActor<ACatWaterRegion>(FVector(0.0, 2000.0, 0.0), FRotator::ZeroRotator);
	ACatFishTankActor* Tank = World->SpawnActor<ACatFishTankActor>(FVector(-800.0, -800.0, 0.0), FRotator::ZeroRotator);
	ACatCampHubActor* Camp = World->SpawnActor<ACatCampHubActor>(FVector(-800.0, -800.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("可生成 PlayerStart"), PlayerStart) || !TestNotNull(TEXT("可生成水域"), WaterRegion)
		|| !TestNotNull(TEXT("可生成共享鱼缸"), Tank) || !TestNotNull(TEXT("可生成营地"), Camp))
	{
		return false;
	}
	WaterRegion->RegionId = TEXT("River");
	WaterRegion->bEnablePrototypeBounds = true;
	WaterRegion->LocalCenterOffset = FVector::ZeroVector;
	WaterRegion->HalfExtent = FVector(3000.0, 1500.0, 400.0);
	WaterRegion->RegionRevision = 1;
	TestTrue(TEXT("水域配置 runtime ready"), WaterRegion->IsRuntimeConfigured());
	TestTrue(TEXT("营地可挂接共享鱼缸"), AttachSharedFishTank(Camp, Tank));

	// 真实开局：StartPlay 会校验数据目录并启动 ST_RunFlow，白天计时器从这一刻开始按 tick 走。
	if (!TestTrue(TEXT("World 可真实 BeginPlay"), WorldWrapper.BeginPlayInTestWorld()))
	{
		return false;
	}
	ACatfishingGameState* GameState = World->GetGameState<ACatfishingGameState>();
	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	UCatChumSpotSubsystem* ChumSpots = World->GetSubsystem<UCatChumSpotSubsystem>();
	if (!TestNotNull(TEXT("可取得项目 GameState"), GameState) || !TestNotNull(TEXT("可取得 ItemsService"), ItemsService)
		|| !TestNotNull(TEXT("可取得窝点子系统"), ChumSpots))
	{
		return false;
	}
	TestEqual(TEXT("真实 StartPlay 后一局处于第 1 天白天"), GameState->GetRunPublicState().Phase.Phase, ECatRunPhase::DayActive);
	TestEqual(TEXT("开局天序号是 1"), GameState->GetRunPublicState().Phase.DayIndex, 1);
	TestTrue(TEXT("白天允许钓鱼"), GameState->GetRunPublicState().Phase.bFishingAllowed);
	TestNotEqual(TEXT("开局没有以启动失败收场"), GameState->GetRunPublicState().EndReason, ECatRunEndReason::StartupFailed);
	if (GameState->GetRunPublicState().Phase.Phase != ECatRunPhase::DayActive)
	{
		return false;
	}

	const FPlayerFixture Player = AdmitPlayer(*this, *World, TEXT("player:functional-core-loop"));
	if (!Player.Controller || !Player.Character)
	{
		return false;
	}
	const FGuid GuardId = Player.Character->GetPersonalFishGuardId();
	UCatEquipmentComponent* Equipment = Player.Character->GetEquipmentComponent();
	TestNotNull(TEXT("玩家 Character 持有装备组件"), Equipment);
	if (!Equipment)
	{
		return false;
	}
	TestFalse(TEXT("占有后 starter 三件套已自动装配"), Equipment->GetSnapshot().RodDefinitionId.IsNone());

	// 站进水域：抛竿落点按漂射程取在猫正前方，投窝可达判定要求人和落点同在一片水里。
	Player.Character->SetActorLocation(FVector(0.0, 1500.0, 150.0));
	Player.Character->SetActorRotation(FRotator(0.0, 0.0, 0.0));

	// —— 白天·商店与投窝：领免费饵、公款买第一条窝料、把窝投在自己脚下 ——
	const UCatShopEconomySettings* ShopSettings = GetDefault<UCatShopEconomySettings>();
	TestFalse(TEXT("项目配置里有免费普通饵条目"), ShopSettings->FreeOrdinaryBaitEntryId.IsNone());
	Player.Controller->ServerClaimFreeShopEntry(ShopSettings->FreeOrdinaryBaitEntryId, FGuid::NewGuid(),
		GameState->GetShopEconomySnapshot().WalletRevision);
	Player.Controller->ServerSubmitShopPurchase(TEXT("ShopBugChumOrder"), FGuid::NewGuid(),
		GameState->GetShopEconomySnapshot().WalletRevision);
	WorldWrapper.TickTestWorld(TickStepSeconds);
	FName ChumDefinitionId = NAME_None;
	for (const FCatRunConsumableStack& Stack : Equipment->GetSnapshot().Consumables)
	{
		if (Stack.DefinitionId == TEXT("BugChum"))
		{
			ChumDefinitionId = Stack.DefinitionId;
		}
	}
	TestFalse(TEXT("公款购买的窝料落进了猫的随身耗材栈"), ChumDefinitionId.IsNone());
	if (!ChumDefinitionId.IsNone())
	{
		Player.Controller->ServerContributeChum(Player.Character->GetActorLocation(), FGuid::NewGuid(),
			Equipment->GetSnapshot().Revision, ChumSpots->GetAggregationRevision(), ChumDefinitionId);
		WorldWrapper.TickTestWorld(TickStepSeconds);
		TestTrue(TEXT("投窝后脚下有了窝点"), ChumSpots->QueryChumSpot(Player.Character->GetActorLocation()).bHasSpot);
	}

	// —— 白天·连钓两条鱼：每条都走 开钓→等窝料算出的咬钩间隔→真咬期拖=完美中鱼→碾压进近岸→抄网入护 ——
	UCatRunImprintService* ImprintService = World->GetSubsystem<UCatRunImprintService>();
	if (!TestNotNull(TEXT("可取得印记服务"), ImprintService))
	{
		return false;
	}
	TArray<FGuid> ScoopRequestIds;
	TArray<FGuid> CaughtFishInstanceIds;
	int32 CaughtFishCount = 0;
	for (int32 CatchIndex = 0; CatchIndex < 2; ++CatchIndex)
	{
		Player.Controller->ServerStartFishingSession(FGuid::NewGuid());
		WorldWrapper.TickTestWorld(TickStepSeconds);
		ACatFishingSession* Session = FindActiveFishingSession(*World);
		if (!TestNotNull(TEXT("开钓后存在活跃钓鱼会话"), Session))
		{
			break;
		}
		// 投过窝的咬钩间隔约 13 秒；给 30 秒模拟上限兜住衰减带来的漂移。
		TestTrue(TEXT("等到真咬期（咬钩间隔按窝料池算出）"), TickUntil(WorldWrapper, 30.0f,
			[&] { return Session->GetSnapshot().Phase == ECatFishingPhase::TrueBiteWindow; }));
		Player.Controller->ServerSetFishingFightIntent(ECatFishingFightIntent::Pull);
		TestTrue(TEXT("提竿后进入搏斗并对弱鱼直接碾压到近岸"), TickUntil(WorldWrapper, 5.0f,
			[&] { return Session->GetSnapshot().Phase == ECatFishingPhase::NearShore; }));
		TestTrue(TEXT("真咬期 1 秒内提竿判定为完美中鱼"), Session->GetSnapshot().bPerfectHook);

		FCatScoopCommand Scoop;
		Scoop.Context.RequestId = FGuid::NewGuid();
		Scoop.Context.ExpectedRevision = Session->GetSnapshot().Revision;
		Scoop.TargetGuardContainerId = GuardId;
		Scoop.ScoopWorldLocation = Player.Character->GetActorLocation();
		Player.Controller->ServerRequestScoop(Session->GetSnapshot().FishingSessionId, Scoop);
		TestTrue(TEXT("抄网后会话进入 Resolved"), TickUntil(WorldWrapper, 2.0f,
			[&] { return Session->GetSnapshot().Phase == ECatFishingPhase::Resolved; }));
		Player.Controller->ServerSetFishingFightIntent(ECatFishingFightIntent::None);

		FCatContainerSnapshot Guard;
		TestTrue(TEXT("抄网后能读到个人鱼护"), ItemsService->TryGetContainerSnapshot(GuardId, Guard));
		CaughtFishCount = Guard.Fish.Num();
		ScoopRequestIds.Add(Scoop.Context.RequestId);
		for (const FCatFishInstance& Fish : Guard.Fish)
		{
			CaughtFishInstanceIds.AddUnique(Fish.FishInstanceId);
		}
		// 终态会话保留一小段复制窗口；等它彻底离场再开下一竿，避免"同钓手已有活跃会话"的误拒。
		TestTrue(TEXT("终态会话在留存窗后离场"), TickUntil(WorldWrapper, 10.0f,
			[&] { return FindActiveFishingSession(*World) == nullptr; }));
	}
	TestEqual(TEXT("白天连钓两条鱼都进了个人鱼护"), CaughtFishCount, 2);
	if (CaughtFishCount != 2 || ScoopRequestIds.Num() != 2 || CaughtFishInstanceIds.Num() != 2)
	{
		return false;
	}

	// —— 补上客户端档案的回执：结算归档要等所有 Grant 拿到 durable ACK 才放行。
	// 生产里这一步由 LocalPlayer 的档案子系统在落盘成功后自动发起；测试 World 的 Controller 挂的是最小
	// UPlayer、没有档案子系统，所以由测试顶替"档案已落盘"这个事实，走的仍是同一条真实回执入口
	// （ServerAcknowledgeProfileGrant）。Grant 号通过两轨记录的幂等重放取得：重放键就是抄网的 RequestId。
	for (int32 CatchIndex = 0; CatchIndex < 2; ++CatchIndex)
	{
		FCatCaptureCommittedResult ReplayCapture;
		ReplayCapture.CaptureRequestId = ScoopRequestIds[CatchIndex];
		ReplayCapture.FishInstance.FishInstanceId = CaughtFishInstanceIds[CatchIndex];
		ReplayCapture.FishInstance.FishDefinitionId = TEXT("CoreLoopWeakFish");
		ReplayCapture.FishInstance.WeightKilograms = 2.0;
		FCatCaptureConditionSnapshot ReplayCondition;
		ReplayCondition.RegionId = TEXT("River");
		const FGuid CaughtGrantId = ImprintService->RecordCommittedCapture(ReplayCapture, Player.StableNetId, ReplayCondition);
		const FGuid ScoopedGrantId = ImprintService->RecordCommittedScoop(ReplayCapture, Player.StableNetId);
		TestTrue(TEXT("钓起轨 Grant 可按抄网 RequestId 重放取得"), CaughtGrantId.IsValid());
		TestTrue(TEXT("抄获轨 Grant 可按抄网 RequestId 重放取得"), ScoopedGrantId.IsValid());
		Player.Controller->ServerAcknowledgeProfileGrant(CaughtGrantId);
		Player.Controller->ServerAcknowledgeProfileGrant(ScoopedGrantId);
	}
	WorldWrapper.TickTestWorld(TickStepSeconds);
	TestEqual(TEXT("四条 Grant 回执后没有待 ACK 记录"), ImprintService->GetPendingGrantAckCount(), 0);

	// —— 营地：走到营地半径内，把一条鱼转进共享鱼缸，再休息一次 ——
	Player.Character->SetActorLocation(Camp->GetActorLocation() + FVector(100.0, 0.0, 150.0));
	FCatContainerSnapshot GuardAtCamp;
	FCatContainerSnapshot TankBefore;
	TestTrue(TEXT("营地前能读到鱼护"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAtCamp));
	TestTrue(TEXT("营地前能读到共享鱼缸"), ItemsService->TryGetContainerSnapshot(Tank->GetTankContainerId(), TankBefore));
	Player.Controller->ServerTransferFishToTank(Camp, FGuid::NewGuid(), GuardAtCamp.Fish[0].FishInstanceId,
		GuardAtCamp.Revision, TankBefore.Revision);
	WorldWrapper.TickTestWorld(TickStepSeconds);
	FCatContainerSnapshot TankAfter;
	FCatContainerSnapshot GuardAfterTransfer;
	TestTrue(TEXT("转移后能读到共享鱼缸"), ItemsService->TryGetContainerSnapshot(Tank->GetTankContainerId(), TankAfter));
	TestTrue(TEXT("转移后能读到鱼护"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAfterTransfer));
	TestEqual(TEXT("共享鱼缸里多了一条鱼"), TankAfter.Fish.Num(), TankBefore.Fish.Num() + 1);
	TestEqual(TEXT("鱼护里剩一条留着晚上献祭"), GuardAfterTransfer.Fish.Num(), 1);
	const FCatDomainCommandResult RestResult = Camp->RequestRest(Player.Controller, FGuid::NewGuid());
	TestTrue(TEXT("营地半径内休息命令提交成功"), RestResult.bCommitted);

	// —— 入夜：不碰任何相位接口，等白天计时器自己到点，由 ST_RunFlow 把局推进普通夜 ——
	TestTrue(TEXT("白天计时器到点后自动进入普通夜"), TickUntil(WorldWrapper, DayLengthSecondsOverride + 10.0f,
		[&] { return GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::NormalNight; }));
	TestTrue(TEXT("普通夜的额度写口是打开的"), GameState->GetRunPublicState().Phase.bQuotaOpen);
	TestFalse(TEXT("夜晚不允许钓鱼"), GameState->GetRunPublicState().Phase.bFishingAllowed);

	// —— 献祭：把鱼护里剩下的那条上供，额度 1/1 达标 ——
	FCatContainerSnapshot GuardAtNight;
	TestTrue(TEXT("夜里能读到鱼护"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAtNight));
	FCatSacrificeCommand Sacrifice;
	Sacrifice.Context.RequestId = FGuid::NewGuid();
	Sacrifice.Context.ExpectedRevision = GuardAtNight.Revision;
	Sacrifice.FishInstanceId = GuardAtNight.Fish[0].FishInstanceId;
	Sacrifice.ContainerId = GuardId;
	Sacrifice.ExpectedRunRevision = GameState->GetRunPublicState().Revision;
	Player.Controller->ServerRequestSacrifice(Sacrifice);
	TestTrue(TEXT("献祭后额度推进到 1/1"), TickUntil(WorldWrapper, 2.0f,
		[&] { return GameState->GetRunPublicState().QuotaProgress >= QuotaTargetOverride; }));
	TestEqual(TEXT("献祭推进了世界进度"), GameState->GetRunPublicState().WorldProgress, 1);
	FCatContainerSnapshot GuardAfterSacrifice;
	TestTrue(TEXT("献祭后能读到鱼护"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAfterSacrifice));
	TestEqual(TEXT("献祭后鱼护清空（鱼被不可逆消费）"), GuardAfterSacrifice.Fish.Num(), 0);

	// —— 翻天确认：唯一玩家 ready；额度已齐 + 第 1 天即毕业日 → 成功结算夜 ——
	Player.Controller->ServerSetNextDayReady(FGuid::NewGuid(), GameState->GetRunPublicState().Revision, true);
	TestTrue(TEXT("全员 ready 且额度达标的毕业日走成功结算夜"), TickUntil(WorldWrapper, 5.0f,
		[&] { return GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::SuccessSettlementNight; }));
	TestEqual(TEXT("成功结算夜的终局原因是 Success"), GameState->GetRunPublicState().EndReason, ECatRunEndReason::Success);
	TestEqual(TEXT("没有进入第 2 天（毕业日当晚直接结算）"), GameState->GetRunPublicState().Phase.DayIndex, 1);

	// —— 结算完成：Ending → Ended，一局按生产路径走到自然终点 ——
	Player.Controller->ServerRequestSettlementCompletion(FGuid::NewGuid(), GameState->GetRunPublicState().Revision);
	TestTrue(TEXT("结算完成后一局推进到 Ended"), TickUntil(WorldWrapper, 5.0f,
		[&] { return GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::Ended; }));

	WorldWrapper.EndPlayInTestWorld();
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
