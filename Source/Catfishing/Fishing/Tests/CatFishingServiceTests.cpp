#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Environment/CatWaterRegion.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Core/CatFishingBoundaryContracts.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Integration/Fishing/CatFishingBoundarySubsystem.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceFailClosedTest,
	"Catfishing.Fishing.Service.InvalidIdentityAndUnknownSessionFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingServiceBrokenRodAttemptClosureTest,
	"Catfishing.Fishing.Service.BrokenRodCastRejectedAndClosesBoundaryAttempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingServiceTest
{
	/** 本用例的水域与鱼种共用的地点 ID；鱼表按它筛候选，水域 Actor 按它回答"落点算不算水"，两边必须写同一个名字。 */
	static const TCHAR* RegionId = TEXT("River");

	/**
	 * 鱼表覆盖守卫：把全局鱼目录临时换成一条本用例自带的弱鱼，析构时逐项写回。
	 * 本用例必须让 Boundary 的 Cast 真的抽到鱼并冻结出 EncounterSpec，否则根本走不到"会话建立失败"那一段；
	 * 而 GameMode StartPlay 会把鱼表和装备表当一个内容包整体校验，来源戳与版本缺一不可，缺了整局会直接以
	 * DataCatalogInvalid 拒绝开局。
	 */
	struct FRegressionFishCatalogOverride
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

		FRegressionFishCatalogOverride()
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

			Definition = NewObject<UCatFishDefinition>(GetTransientPackage(), TEXT("CatFishingServiceRegressionFish"));
			Definition->AddToRoot();
			Definition->bEnableRuntimeDefinition = true;
			Definition->FishDefinitionId = TEXT("ServiceRegressionWeakFish");
			Definition->BodyClass = ECatFishBodyClass::Standard;
			Definition->SacrificeContribution = 1;
			Definition->RegionIds = {RegionId};
			Definition->ChumAffinities = {ECatChumAffinity::Fishy};
			// 最小重量 == 最大重量：抽取时重量不再随机，本用例只关心这一竿有没有抽出鱼，不关心抽到多重。
			Definition->MinimumWeightKilograms = 2.0;
			Definition->MaximumWeightKilograms = 2.0;
			Definition->MinimumFightParticipants = 1;
			Definition->FishStrength = 10.0;
			Definition->FishFightStamina = 15.0;
			Definition->FoodSafety = ECatFishFoodSafety::Safe;

			Settings->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
			Settings->DataRevision = 1;
			Settings->SourceStamp.SourceKind = TEXT("Automation");
			Settings->SourceStamp.SourceNodeToken = TEXT("CatFishingServiceRegressionTest");
			Settings->SourceStamp.SourceRevision = 1;
			Settings->SourceStamp.SourceSliceName = TEXT("ServiceRegressionWeakFish");
			Settings->Definitions.Reset();
			Settings->Definitions.Add(Definition);
		}

		~FRegressionFishCatalogOverride()
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

	/** 一名走过真实准入并占有 Character 的钓手；任一为空表示装配失败，调用方应直接放弃本轮断言。 */
	struct FFisherFixture
	{
		/** 走过真实 PreLogin/PostLogin 的项目 PlayerController；Boundary 只从它的 PlayerState 读服务器身份。 */
		TObjectPtr<ACatfishingPlayerController> Controller = nullptr;

		/** PostLogin 由引擎 RestartPlayer 在 PlayerStart 生成并占有的项目 Character；占有时自动装配 starter 三件套。 */
		TObjectPtr<ACatCharacter> Character = nullptr;
	};

	// 钓手装配流程：走真实 PreLogin/PostLogin 让 GameMode 按生产路径准入并生成 Character（starter 竿/饵/漂随占有装上），
	// 最后把移动模式改成飞行——测试 World 没有地面，重力会在几秒内把猫拖出水域，后面的落点判定会被误拒。
	// 与 Framework/Game 的核心循环用例同骨架；那边的辅助函数在自己的 .cpp 命名空间里，跨文件用不上，这里只留本用例需要的步骤。
	static FFisherFixture AdmitFisher(FAutomationTestBase& Test, UWorld& World, const FString& StableNetId)
	{
		FFisherFixture Fixture;
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
		Test.TestTrue(TEXT("钓手身份 PreLogin 通过"), PreLoginError.IsEmpty());

		ACatfishingPlayerController* Controller = World.SpawnActor<ACatfishingPlayerController>();
		ACatfishingPlayerState* PlayerState = World.SpawnActor<ACatfishingPlayerState>();
		UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
		Test.TestNotNull(TEXT("回归用例 Controller 可创建"), Controller);
		Test.TestNotNull(TEXT("回归用例 PlayerState 可创建"), PlayerState);
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
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Fixture.Controller = Controller;
		Fixture.Character = Character;
		return Fixture;
	}

	// Attempt 开放性探针：拿一条全新 RequestId 的普通饵 operation 去敲同一个 AttemptId。
	// Boundary 没有"还剩几个开放 attempt"的只读入口，但 Journal 的行为本身就是答案：attempt 还开着时新 operation 会被
	// 正常受理（Committed / None），被收口之后一律回 AttemptClosed。普通饵不写任何库存也不推进 Fishing 阶段，
	// 所以这个探针除了留下一条 Journal 记录之外没有副作用，可以安全地在同一局里对不同 attempt 各敲一次。
	static ECatFishingBoundaryError ProbeAttemptOpenState(UCatFishingBoundarySubsystem& Boundary,
		const FCatFishingStartContext& Context)
	{
		FCatFishingBiteAcceptedRequest Probe;
		Probe.RequestId = FGuid::NewGuid();
		Probe.AttemptId = Context.AttemptId;
		Probe.PrincipalId = Context.PrincipalId;
		Probe.ExpectedRevision = Context.RunRevision;
		Probe.BaitDefinitionId = TEXT("ServiceRegressionProbeBait");
		Probe.BiteToken = FGuid::NewGuid();
		return Boundary.BaitAccepted(Probe).Header.Error;
	}
}

// 测试流程：取得真实 Fishing WorldSubsystem 后从三个公开入口提交缺身份/未知会话命令；结果必须明确拒绝且不会创建可观察会话。
bool FCatFishingServiceFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 FishingService 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr;
	TestNotNull(TEXT("FishingService 测试 World 可用"), World);
	TestNotNull(TEXT("真实 FishingService 已创建"), Fishing);
	if (!Fishing)
	{
		return false;
	}

	const FGuid StartRequestId = FGuid::NewGuid();
	const FCatFishingStartResult StartResult = Fishing->StartFishingSession(nullptr, StartRequestId);
	TestFalse(TEXT("缺少 Controller 身份时不会启动会话"), StartResult.bStarted);
	TestEqual(TEXT("缺少身份返回 InvalidIdentity"), StartResult.Error, ECatDomainCommandError::InvalidIdentity);
	TestEqual(TEXT("开始结果保留原 RequestId"), StartResult.RequestId, StartRequestId);
	TestFalse(TEXT("失败开始不伪造 SessionId"), StartResult.FishingSessionId.IsValid());

	const FGuid AssistRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult AssistResult = Fishing->SubmitFightAssist(
		FGuid::NewGuid(), nullptr, AssistRequestId, 1);
	TestFalse(TEXT("未知会话协作不提交"), AssistResult.bCommitted);
	TestEqual(TEXT("未知会话协作返回 NotFound"), AssistResult.Error, ECatDomainCommandError::NotFound);
	TestEqual(TEXT("协作拒绝保留 RequestId"), AssistResult.RequestId, AssistRequestId);

	FCatScoopCommand ScoopCommand;
	ScoopCommand.Context.RequestId = FGuid::NewGuid();
	ScoopCommand.Context.ExpectedRevision = 1;
	ScoopCommand.TargetGuardContainerId = FGuid::NewGuid();
	const FCatScoopResult ScoopResult = Fishing->RequestScoop(FGuid::NewGuid(), nullptr, ScoopCommand);
	TestFalse(TEXT("未知会话抢抄不提交"), ScoopResult.Command.bCommitted);
	TestEqual(TEXT("未知会话抢抄返回 NotFound"), ScoopResult.Command.Error, ECatDomainCommandError::NotFound);
	TestEqual(TEXT("抢抄拒绝保留 RequestId"), ScoopResult.Command.RequestId, ScoopCommand.Context.RequestId);

	Fishing->CloseCommandsAndTerminateAll();
	const FCatFishingStartResult ClosedResult = Fishing->StartFishingSession(nullptr, FGuid::NewGuid());
	TestFalse(TEXT("关门后缺身份请求仍不启动会话"), ClosedResult.bStarted);
	return !HasAnyErrors();
}

// 测试流程（回归：断竿后再抛一次，Boundary 里不能留下无主 attempt）：
// 1. 覆盖鱼表、摆好 PlayerStart 与 River 水域，再走真实 BeginPlay 开局——白天允许钓鱼、环境事实就绪，Cast 才有条件冻结
//    出真正的 EncounterSpec。
// 2. 准入一名钓手，让他站进水里并朝 +X，使漂的落点落在水域 AABB 内。
// 3. 正对照：竿完好时先钓成一竿，证明这套夹具确实能走完 Start→Cast→建会话整条链，后面那一竿的失败点因此只可能在 Cast
//    之后；顺带确认成功那一竿的 attempt 仍然开着，探针本身能区分开/关两种状态。
// 4. 终止会话让出单活跃槽位，再走真实的搏斗耐久入口把剩余耐久一次磨光，把竿真的弄断。
// 5. 回归主体：Boundary 的 Start/Cast 全程不看鱼竿，所以这一竿必然先把 Cast 提交并冻结规格，再死在会话冻结鱼竿那一步。
//    断言命令被拒、没有伪造会话，并且这个 AttemptId 已经被收口——修复前它会一直开着，每失败一次就多攒一个。
bool FCatFishingServiceBrokenRodAttemptClosureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatFishingServiceTest;

	FRegressionFishCatalogOverride CatalogOverride;
	if (!TestNotNull(TEXT("鱼目录可覆盖"), CatalogOverride.Settings))
	{
		return false;
	}

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建断竿回归测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("断竿回归测试 World 可创建"), World))
	{
		return false;
	}

	// 先注册项目 GameMode 再摆场景，最后才 BeginPlay：水域和出生点都要在开局前就位，才能走各自的真实注册路径。
	const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
	FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
	if (!TestTrue(TEXT("测试 World 可注册项目 GameMode"), World->SetGameMode(GameModeUrl)))
	{
		return false;
	}
	APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	ACatWaterRegion* WaterRegion = World->SpawnActor<ACatWaterRegion>(FVector(0.0, 2000.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("可生成 PlayerStart"), PlayerStart) || !TestNotNull(TEXT("可生成水域"), WaterRegion))
	{
		return false;
	}
	WaterRegion->RegionId = CatFishingServiceTest::RegionId;
	WaterRegion->bEnablePrototypeBounds = true;
	WaterRegion->LocalCenterOffset = FVector::ZeroVector;
	WaterRegion->HalfExtent = FVector(3000.0, 1500.0, 400.0);
	WaterRegion->RegionRevision = 1;
	TestTrue(TEXT("水域配置 runtime ready"), WaterRegion->IsRuntimeConfigured());
	if (!TestTrue(TEXT("World 可真实 BeginPlay"), WorldWrapper.BeginPlayInTestWorld()))
	{
		return false;
	}

	UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
	UCatFishingBoundarySubsystem* Boundary = World->GetSubsystem<UCatFishingBoundarySubsystem>();
	if (!TestNotNull(TEXT("可取得 FishingService"), Fishing) || !TestNotNull(TEXT("可取得 Fishing Boundary"), Boundary))
	{
		return false;
	}
	const FFisherFixture Fisher = AdmitFisher(*this, *World, TEXT("player:service-broken-rod-regression"));
	if (!Fisher.Controller || !Fisher.Character)
	{
		return false;
	}
	UCatEquipmentComponent* Equipment = Fisher.Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("钓手 Character 持有装备组件"), Equipment))
	{
		return false;
	}
	// 站进水里、朝 +X 抛：漂的落点必须落在水域 AABB 内，Cast 才会查到水域并继续往下冻结规格。
	Fisher.Character->SetActorLocation(FVector(0.0, 1500.0, 150.0));
	Fisher.Character->SetActorRotation(FRotator::ZeroRotator);
	// 挪完位置先走一帧，让改过的站位和开局后的环境事实都落到各系统读得到的状态上，再开始下命令。
	WorldWrapper.TickTestWorld(1.0f / 30.0f);

	const FGuid HealthyRequestId = FGuid::NewGuid();
	const FCatFishingStartResult HealthyStart = Fishing->StartFishingSession(Fisher.Controller, HealthyRequestId);
	if (!TestTrue(TEXT("竿完好时开钓成功（本用例的正对照）"), HealthyStart.bStarted))
	{
		return false;
	}
	// 同 RequestId 再进一次 Boundary Start 走的是重放分支：不产生第二个 Attempt，只把首次的上下文原样交回来，
	// 这是测试拿到 AttemptId 的唯一只读办法。
	const FCatFishingBoundaryStartResult HealthyAttempt = Boundary->Start(Fisher.Controller, HealthyRequestId);
	TestTrue(TEXT("可按原 RequestId 重放取回成功那一竿的 Attempt"), HealthyAttempt.Header.bReplay);
	TestEqual(TEXT("开钓成功的 attempt 仍然开着，后半程协议还能继续走"),
		ProbeAttemptOpenState(*Boundary, HealthyAttempt.Context),
		ECatFishingBoundaryError::None);

	Fishing->TerminateSessionsForCharacter(Fisher.Character);
	const FCatDomainCommandResult BreakRod = Equipment->CommitFightRodDurabilityFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Equipment->GetSnapshot().RodDurability);
	TestTrue(TEXT("夹具经真实搏斗耐久入口把竿磨断"), BreakRod.bCommitted);
	if (!TestTrue(TEXT("断竿后 Equipment 快照 bRodBroken=true"), Equipment->GetSnapshot().bRodBroken))
	{
		return false;
	}

	const FGuid BrokenRequestId = FGuid::NewGuid();
	const FCatFishingStartResult BrokenStart = Fishing->StartFishingSession(Fisher.Controller, BrokenRequestId);
	TestFalse(TEXT("竿已断时不会建立会话"), BrokenStart.bStarted);
	TestEqual(TEXT("竿已断时开钓命令被拒"), BrokenStart.Error, ECatDomainCommandError::PolicyUndecided);
	TestFalse(TEXT("被拒的一竿不伪造 SessionId"), BrokenStart.FishingSessionId.IsValid());

	const FCatFishingBoundaryStartResult BrokenAttempt = Boundary->Start(Fisher.Controller, BrokenRequestId);
	TestTrue(TEXT("可按原 RequestId 重放取回失败那一竿的 Attempt"), BrokenAttempt.Header.bReplay);
	TestTrue(TEXT("失败那一竿确实分配过 Attempt"), BrokenAttempt.Context.AttemptId.Value.IsValid());
	TestEqual(TEXT("会话没建成时 attempt 已收口，Boundary 里不留无主 attempt"),
		ProbeAttemptOpenState(*Boundary, BrokenAttempt.Context),
		ECatFishingBoundaryError::AttemptClosed);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
