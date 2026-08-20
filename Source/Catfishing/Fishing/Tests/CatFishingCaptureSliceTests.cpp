#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/StateTreeComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Items/CatItemsService.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCaptureSliceTest,
	"Catfishing.Slice.FishingCapture.NearShoreScoopLandsFishInGuardAndRecordsImprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightRodBreakSliceTest,
	"Catfishing.Slice.FishingFight.OutwardPullAgainstStrongFishBreaksStarterRodAndTerminates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFloatCastSliceTest,
	"Catfishing.Slice.FishingCast.FloatRangeAndAccuracyDecideLandingLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingCaptureSliceTest
{
	/** 本切片的钓手装配结果：真实 Lake GameMode 已准入的 Controller 和它占有的 Character；任一为空表示装配失败。 */
	struct FFisherFixture
	{
		/** 走过 PreLogin/PostLogin 的项目 PlayerController；FishingSession 只从它的 PlayerState 读服务器身份。 */
		TObjectPtr<ACatfishingPlayerController> Controller = nullptr;

		/** 被占有的项目 Character；占有时已注册个人鱼护并写入初始搏斗属性。 */
		TObjectPtr<ACatCharacter> Character = nullptr;

		/** 钓手的服务器私有身份字符串；Collection 两轨 Grant 的收件人必须与它一致。 */
		FString StableNetId;
	};

	// 钓手装配流程：注册项目 Lake GameMode 并只初始化 Actor（不 BeginPlay，避免启动本用例无关的 Run StateTree），种入 DayActive 打开命令门，
	// 再走真实 PreLogin/PostLogin 建立 Active 身份，最后生成 Character 并占有——占有在 authority 上会注册个人鱼护并应
	// 用初始 FishingStrength/FightStamina。
	// 与 Framework/Game/Tests 的 Chum 用例同一套骨架：那边的辅助函数在匿名命名空间里无法复用，这里只保留本切片需要的最小步骤。
	static FFisherFixture SpawnAdmittedFisher(FAutomationTestBase& Test, UWorld& World, const FString& StableNetId)
	{
		FFisherFixture Fixture;
		const FString GameModeOption = FString::Printf(TEXT("?game=%s"), *ACatfishingGameModeBase::StaticClass()->GetPathName());
		FURL GameModeUrl(nullptr, *GameModeOption, TRAVEL_Absolute);
		Test.TestTrue(TEXT("切片 World 可注册项目 Lake GameMode"), World.SetGameMode(GameModeUrl));
		ACatfishingGameModeBase* GameMode = World.GetAuthGameMode<ACatfishingGameModeBase>();
		Test.TestNotNull(TEXT("可取得切片 authority GameMode"), GameMode);
		if (!GameMode)
		{
			return Fixture;
		}
		World.InitializeActorsForPlay(GameModeUrl);
		GameMode->SeedRunPhaseEntryForAutomation(ECatRunPhase::DayActive, 1, 1);

		const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(
			StableNetId, UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
		const FUniqueNetIdRepl UniqueId(StableUniqueId);
		FString PreLoginError;
		GameMode->PreLogin(TEXT(""), TEXT("127.0.0.1"), UniqueId, PreLoginError);
		Test.TestTrue(TEXT("切片钓手身份 PreLogin 通过"), PreLoginError.IsEmpty());

		ACatfishingPlayerController* Controller = World.SpawnActor<ACatfishingPlayerController>();
		ACatfishingPlayerState* PlayerState = World.SpawnActor<ACatfishingPlayerState>();
		UPlayer* TestPlayer = Controller ? NewObject<UPlayer>(Controller) : nullptr;
		Test.TestNotNull(TEXT("切片 Controller 可创建"), Controller);
		Test.TestNotNull(TEXT("切片 PlayerState 可创建"), PlayerState);
		Test.TestNotNull(TEXT("切片 Controller 可绑定最小 Player"), TestPlayer);
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
		Test.TestTrue(TEXT("切片 Controller 通过玩法命令 gate"), GameMode->CanAcceptGameplayCommand(Controller));

		ACatCharacter* Character = World.SpawnActor<ACatCharacter>();
		Test.TestNotNull(TEXT("切片 Character 可创建"), Character);
		if (!Character)
		{
			return Fixture;
		}
		Controller->Possess(Character);
		Fixture.Controller = Controller;
		Fixture.Character = Character;
		Fixture.StableNetId = StableNetId;
		return Fixture;
	}

	// 鱼定义构造流程：用 transient 对象拼一条 runtime-ready 的普通鱼，不配置成像事件，使抢抄链只依赖 Items 与
	// Collection 两轨记录，不依赖印记准入清单。
	static UCatFishDefinition* MakeSliceFishDefinition()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = TEXT("SliceNearShoreFish");
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RegionIds = {TEXT("SliceRiver")};
		Definition->ChumAffinities = {ECatChumAffinity::Fishy};
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 3.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 10.0;
		Definition->FishFightStamina = 20.0;
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		return Definition;
	}

	// EncounterSpec 构造流程：按 Boundary Cast 会冻结的字段拼一份合法规格；水域 AABB 中心 (1000,0,0)、半尺寸
	// (500,500,400)，与钓手站位配合使近岸目标恰好落在 reach 内。
	static FCatFishingEncounterSpec MakeSliceEncounterSpec(const UCatFishDefinition& Definition)
	{
		FCatFishingEncounterSpec Spec;
		Spec.AttemptId.Value = FGuid::NewGuid();
		Spec.FishDefinitionId = Definition.FishDefinitionId;
		Spec.DataSchemaVersion = 1;
		Spec.DataRevision = 1;
		Spec.ContentHashHex = TEXT("slice-content-hash");
		Spec.SelectionSeed = 7;
		Spec.WaterRegion.RegionId = TEXT("SliceRiver");
		Spec.WaterRegion.RegionRevision = 1;
		Spec.WaterRegion.WorldCenter = FVector(1000.0, 0.0, 0.0);
		Spec.WaterRegion.HalfExtent = FVector(500.0, 500.0, 400.0);
		Spec.FishWeightKilograms = 2.5;
		Spec.bGiant = false;
		Spec.FishFightStamina = Definition.FishFightStamina;
		Spec.FightParticipantCount = 1;
		Spec.CombinedFishingStrength = 50.0;
		Spec.CombinedFightStamina = 100.0;
		// 咬钩间隔刻意取 6 秒而不是旧延时边的 3 秒：下面先推 3.5 秒断言仍在 Probe，证明 Probe 等的是会话冻结的值，不是资产里写死的数。
		Spec.BiteIntervalSeconds = 6.0;
		// D₀ 取毛线球漂射程那一档的 5 米：本切片只关心搏斗开局的 D 会不会随 D/L 模型变化，不关心具体是哪只漂抛的。
		Spec.InitialFishDistanceMeters = 5.0;
		return Spec;
	}

	// StateTree 推进流程：测试 World 不跑引擎 tick，手动 tick 组件三帧——第一帧让资产把延时边登记为
	// DelayedTransition，第二帧按 Seconds 扣完剩余时间并触发转移，第三帧让新状态的 EnterPhase 副作用完整落地。
	static void AdvanceFishingStateTree(UStateTreeComponent& Component, const float Seconds)
	{
		Component.TickComponent(0.016f, LEVELTICK_All, nullptr);
		Component.TickComponent(Seconds, LEVELTICK_All, nullptr);
		Component.TickComponent(0.016f, LEVELTICK_All, nullptr);
	}
}

// 测试流程：
// 1. 在真实 Lake GameMode 下准入一名钓手并占有 Character（个人鱼护随占有注册到 Items）。
// 2. 让钓手站在水域 AABB 西侧岸上 60cm 处，用 transient 鱼定义与 EncounterSpec 初始化真实 FishingSession，启动项目
// ini 指向的 ST_FishingSession 资产。
// 3. 手动 tick StateTree 把 Probe→TrueBiteWindow 推过去：Probe 要等满 EncounterSpec 冻结的 6 秒咬钩间隔（3.5 秒时仍在
// Probe）；真咬期立刻上报"拖"＝提竿，
//    World 时间没走所以落在 1 秒完美窗内（bPerfectHook=true），HookSet 事件把树推进 HookedFight 而不用等 10 秒延时边；
// 进搏斗后继续按着"拖"：猫力 50 对鱼力 10/3×2.5≈8.33（完美中鱼后 ≈6.67），猫 ≥ 鱼×2 命中判定表③绝对碾压，D 归零，树进
// NearShore（近岸目标由 Session 按钓手位置算出）。
// 4. 钓手提交 RequestScoop，断言捕获提交成功、会话 Resolved、实物鱼出现在他的个人鱼护快照里、Collection 为钓起轨与抄
// 获轨各留一条待 ACK Grant，且钓起轨以钓手身份重放返回同一 Grant。
// 5. 同 RequestId 重放只返回 AlreadyResolved，鱼护里仍只有一条鱼。
// 本用例不走 Boundary Start/Cast（鱼表抽取、水域查询），那条链由 Integration/Fishing 合同测试与 PIE 实测覆盖；这里锁
// 的是"进 NearShore → 抄 → 入护 → 记轨"这一段。
bool FCatFishingCaptureSliceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建钓鱼捕获切片 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatItemsService* ItemsService = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatRunImprintService* ImprintService = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("切片 World 可创建"), World);
	TestNotNull(TEXT("可取得 ItemsService"), ItemsService);
	TestNotNull(TEXT("可取得 RunImprintService"), ImprintService);
	TestTrue(TEXT("项目 Fishing 配置处于 runtime ready（ini 段缺失时本切片无法进行）"),
		GetDefault<UCatFishingSettings>()->IsRuntimeReady());
	if (!World || !ItemsService || !ImprintService || !GetDefault<UCatFishingSettings>()->IsRuntimeReady())
	{
		return false;
	}

	const CatFishingCaptureSliceTest::FFisherFixture Fisher =
		CatFishingCaptureSliceTest::SpawnAdmittedFisher(*this, *World, TEXT("player:slice-near-shore-fisher"));
	if (!Fisher.Controller || !Fisher.Character)
	{
		return false;
	}
	const FGuid GuardId = Fisher.Character->GetPersonalFishGuardId();
	FCatContainerSnapshot GuardBefore;
	TestTrue(TEXT("占有后个人鱼护已注册到 Items"), ItemsService->TryGetContainerSnapshot(GuardId, GuardBefore));
	TestEqual(TEXT("抢抄前个人鱼护是空的"), GuardBefore.Fish.Num(), 0);

	// 钓手站在水域西侧岸上：水域 X 范围 500~1500，钓手 X=440，离最近水面点 60cm，小于 ini 的 ScoopReachCentimeters=100。
	Fisher.Character->SetActorLocation(FVector(440.0, 0.0, 0.0));

	// 本切片的钓手必须持有 starter 竿（会话初始化要从装备冻结竿强度与 L_max）；碾压出口不磨竿，所以搏斗后耐久应与此刻相同、抄上鱼后再 -1。
	const double RodDurabilityBeforeFight = Fisher.Character->GetEquipmentComponent()->GetSnapshot().RodDurability;
	TestTrue(TEXT("切片钓手占有后已装配 starter 鱼竿"), RodDurabilityBeforeFight > 0.0);

	UCatFishDefinition* FishDefinition = CatFishingCaptureSliceTest::MakeSliceFishDefinition();
	const FCatFishingEncounterSpec EncounterSpec = CatFishingCaptureSliceTest::MakeSliceEncounterSpec(*FishDefinition);
	// 搏斗开局的 D₀ 现在由 EncounterSpec 携带（Cast 时按当前漂的射程与精准度算出的落点距离），不再来自全局配置。
	const double EncounterSpecInitialDistance = EncounterSpec.InitialFishDistanceMeters;
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	TestNotNull(TEXT("可生成 FishingSession Actor"), Session);
	if (!Session)
	{
		return false;
	}
	TestTrue(TEXT("FishingSession 用真实 ST_FishingSession 资产初始化成功"),
		Session->InitializeSession(Fisher.Controller, Fisher.Character, FishDefinition, GuardId, EncounterSpec));
	TestEqual(TEXT("StateTree 启动后首状态是 Probe"), Session->GetSnapshot().Phase, ECatFishingPhase::Probe);
	UStateTreeComponent* StateTree = Session->FindComponentByClass<UStateTreeComponent>();
	TestNotNull(TEXT("FishingSession 持有 StateTree 组件"), StateTree);
	if (!StateTree || Session->GetSnapshot().Phase != ECatFishingPhase::Probe)
	{
		return false;
	}

	// Probe 等的是会话冻结的 6 秒咬钩间隔（BiteIntervalWait 任务按 Tick 累计）；先推 3.5 秒证明旧的 3 秒延时边已经不存在，再补够 6 秒。
	// 其余两条延时边仍与 Commandlet 里的占位常量一致（10/5 秒）；多给一点余量避免浮点边界。
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 3.5f);
	TestEqual(TEXT("3.5 秒后 Probe 仍在等会话冻结的咬钩间隔"), Session->GetSnapshot().Phase, ECatFishingPhase::Probe);
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 3.0f);
	TestEqual(TEXT("等满咬钩间隔后进入 TrueBiteWindow"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
	// 真咬期提竿：测试 World 不推进时间，所以一定落在 1 秒完美窗内；HookSet 事件在下一次 tick 被资产消费。
	const FCatDomainCommandResult HookIntent = Session->SubmitFightIntent(Fisher.Controller, ECatFishingFightIntent::Pull);
	TestTrue(TEXT("钓手在真咬期上报拖（提竿）被接受"), HookIntent.bCommitted);
	TestTrue(TEXT("真咬进入后立即提竿判定为完美中鱼"), Session->GetSnapshot().bPerfectHook);
	// 提竿后先松手（None），让搏斗开局时鱼向外游、没有对抗，便于在进 NearShore 之前观察 HookedFight 的开局快照。
	TestTrue(TEXT("提竿后松手被接受"), Session->SubmitFightIntent(Fisher.Controller, ECatFishingFightIntent::None).bCommitted);
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 0.1f);
	TestEqual(TEXT("提竿事件把树推进 HookedFight（不等 10 秒延时边）"), Session->GetSnapshot().Phase, ECatFishingPhase::HookedFight);
	TestEqual(TEXT("搏斗开局鱼为向外游"), Session->GetSnapshot().FishSwimState, ECatFishSwimState::Outward);
	TestTrue(TEXT("搏斗开局冻结了竿的放线上限"), Session->GetSnapshot().LineLengthMaxMeters > 0.0);
	TestTrue(TEXT("松手时向外游的鱼把 D 拉大（D/L 模型在走）"), Session->GetSnapshot().FishDistanceMeters > EncounterSpecInitialDistance);
	// 现在按住拖：向外游+拖，猫 50 ≥ 鱼 ≈6.67×2 → 判定表③碾压，同帧 D=0 并进 NearShore。
	TestTrue(TEXT("搏斗中上报拖被接受"), Session->SubmitFightIntent(Fisher.Controller, ECatFishingFightIntent::Pull).bCommitted);
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 0.1f);
	TestEqual(TEXT("向外游+拖对弱鱼命中绝对碾压"), Session->GetSnapshot().FightOutcome, ECatFishingFightOutcome::Overpowered);
	TestEqual(TEXT("碾压后 D 归零"), Session->GetSnapshot().FishDistanceMeters, 0.0);
	TestEqual(TEXT("碾压后树进入 NearShore（近岸目标由服务器按钓手位置算出）"),
		Session->GetSnapshot().Phase, ECatFishingPhase::NearShore);
	if (Session->GetSnapshot().Phase != ECatFishingPhase::NearShore)
	{
		return false;
	}

	const int32 PendingGrantsBefore = ImprintService->GetPendingGrantAckCount();
	FCatScoopCommand Scoop;
	Scoop.Context.RequestId = FGuid::NewGuid();
	Scoop.Context.ExpectedRevision = Session->GetSnapshot().Revision;
	Scoop.TargetGuardContainerId = GuardId;
	Scoop.ScoopWorldLocation = Fisher.Character->GetActorLocation();
	const FCatScoopResult ScoopResult = Session->RequestScoop(Fisher.Controller, Scoop);
	TestTrue(TEXT("NearShore 内 reach 范围的钓手抢抄提交成功"), ScoopResult.Command.bCommitted);
	TestEqual(TEXT("抢抄返回 None"), ScoopResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("抢抄后会话 Resolved"), Session->GetSnapshot().Phase, ECatFishingPhase::Resolved);
	TestTrue(TEXT("抢抄后会话进入终态"), Session->IsTerminal());
	TestEqual(TEXT("捕获实物鱼种与会话鱼定义一致"), ScoopResult.Capture.FishInstance.FishDefinitionId,
		FishDefinition->FishDefinitionId);
	TestEqual(TEXT("捕获实物重量来自 EncounterSpec 冻结值"), ScoopResult.Capture.FishInstance.WeightKilograms,
		EncounterSpec.FishWeightKilograms);
	// 飞书 4.4：每钓上一条鱼固定 -1 耐久，扣在钓手的竿上；起点是 starter 竿资产的最大耐久。
	const FCatEquipmentLoadoutSnapshot LoadoutAfterCapture = Fisher.Character->GetEquipmentComponent()->GetSnapshot();
	TestEqual(TEXT("抄上鱼后钓手鱼竿耐久固定 -1"), LoadoutAfterCapture.RodDurability, RodDurabilityBeforeFight - 1.0, 1e-6);
	TestFalse(TEXT("抄上鱼后鱼竿未断"), LoadoutAfterCapture.bRodBroken);

	FCatContainerSnapshot GuardAfter;
	TestTrue(TEXT("抢抄后仍能读到个人鱼护快照"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAfter));
	TestEqual(TEXT("个人鱼护里恰好多了一条鱼"), GuardAfter.Fish.Num(), 1);
	if (GuardAfter.Fish.Num() == 1)
	{
		TestEqual(TEXT("鱼护里的鱼就是本次捕获的 FishInstance"), GuardAfter.Fish[0].FishInstanceId,
			ScoopResult.Capture.FishInstance.FishInstanceId);
	}

	TestEqual(TEXT("Collection 为钓起轨与抄获轨各留一条待 ACK Grant"),
		ImprintService->GetPendingGrantAckCount(), PendingGrantsBefore + 2);
	FCatCaptureConditionSnapshot RecordedCondition;
	RecordedCondition.RegionId = EncounterSpec.WaterRegion.RegionId;
	TestTrue(TEXT("钓起轨以钓手身份、会话水域条件重放返回既有 Grant（证明钓起轨确实记给了钓手）"),
		ImprintService->RecordCommittedCapture(ScoopResult.Capture, Fisher.StableNetId, RecordedCondition).IsValid());
	TestTrue(TEXT("抄获轨以抄手身份重放返回既有 Grant"),
		ImprintService->RecordCommittedScoop(ScoopResult.Capture, Fisher.StableNetId).IsValid());
	TestEqual(TEXT("两轨重放不新增 Grant"), ImprintService->GetPendingGrantAckCount(), PendingGrantsBefore + 2);

	const FCatScoopResult Replay = Session->RequestScoop(Fisher.Controller, Scoop);
	TestFalse(TEXT("同 RequestId 重放不再次提交"), Replay.Command.bCommitted);
	TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved"), Replay.Command.Error, ECatDomainCommandError::AlreadyResolved);
	FCatContainerSnapshot GuardAfterReplay;
	TestTrue(TEXT("重放后仍能读到个人鱼护快照"), ItemsService->TryGetContainerSnapshot(GuardId, GuardAfterReplay));
	TestEqual(TEXT("重放后鱼护里仍只有一条鱼"), GuardAfterReplay.Fish.Num(), 1);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 同样准入一名带 starter 竿（强度 25）的钓手，但鱼定义改成力量 30（最大重量 3kg × K=10，抽到 3kg）；Probe 等满、真
// 咬期不提竿让 10 秒延时边自动进 HookedFight（非完美中鱼）。
// 2. 进搏斗后钓手按住拖：鱼初始向外游，判定表①竿强度 25 ≤ min(猫 50, 鱼 30)=30 → 同帧断竿。
// 3. 断言：会话终局 RodBroken、钓手 Equipment 快照 bRodBroken=true 且耐久归零（断竿走的是
// CommitFightRodDurabilityFromAuthority 把剩余耐久磨光）、树走失败边进 Terminated、会话进入终态。
bool FCatFishingFightRodBreakSliceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	WorldWrapper.ForwardErrorMessages(this);
	if (!TestTrue(TEXT("创建断竿切片 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("断竿切片 World 可创建"), World)
		|| !TestTrue(TEXT("项目 Fishing 配置处于 runtime ready"), GetDefault<UCatFishingSettings>()->IsRuntimeReady()))
	{
		return false;
	}
	const CatFishingCaptureSliceTest::FFisherFixture Fisher =
		CatFishingCaptureSliceTest::SpawnAdmittedFisher(*this, *World, TEXT("player:slice-rod-break-fisher"));
	if (!Fisher.Controller || !Fisher.Character)
	{
		return false;
	}
	Fisher.Character->SetActorLocation(FVector(440.0, 0.0, 0.0));
	const FCatEquipmentLoadoutSnapshot LoadoutBefore = Fisher.Character->GetEquipmentComponent()->GetSnapshot();
	TestTrue(TEXT("断竿切片钓手已装配 starter 鱼竿"), LoadoutBefore.RodDurability > 0.0);
	TestFalse(TEXT("断竿切片开局鱼竿未断"), LoadoutBefore.bRodBroken);

	UCatFishDefinition* FishDefinition = CatFishingCaptureSliceTest::MakeSliceFishDefinition();
	FishDefinition->FishDefinitionId = TEXT("SliceStrongFish");
	FishDefinition->FishStrength = 30.0; // 最大重量 3kg × K=10
	FCatFishingEncounterSpec EncounterSpec = CatFishingCaptureSliceTest::MakeSliceEncounterSpec(*FishDefinition);
	EncounterSpec.FishWeightKilograms = 3.0; // 抽到最大重量，鱼基础力量正好 30
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	if (!TestNotNull(TEXT("可生成断竿切片 FishingSession"), Session))
	{
		return false;
	}
	TestTrue(TEXT("断竿切片会话用真实 ST_FishingSession 初始化成功"),
		Session->InitializeSession(Fisher.Controller, Fisher.Character, FishDefinition, Fisher.Character->GetPersonalFishGuardId(), EncounterSpec));
	UStateTreeComponent* StateTree = Session->FindComponentByClass<UStateTreeComponent>();
	if (!TestNotNull(TEXT("断竿切片会话持有 StateTree 组件"), StateTree))
	{
		return false;
	}
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 6.5f);
	TestEqual(TEXT("等满咬钩间隔后进入 TrueBiteWindow"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
	// 不提竿：10 秒普通响应窗到点自动进 HookedFight，非完美中鱼，鱼力量不削减。
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 10.5f);
	TestEqual(TEXT("真咬窗到点自动进入 HookedFight"), Session->GetSnapshot().Phase, ECatFishingPhase::HookedFight);
	TestFalse(TEXT("没提竿就不是完美中鱼"), Session->GetSnapshot().bPerfectHook);
	if (Session->GetSnapshot().Phase != ECatFishingPhase::HookedFight)
	{
		return false;
	}

	TestTrue(TEXT("搏斗中上报拖被接受"), Session->SubmitFightIntent(Fisher.Controller, ECatFishingFightIntent::Pull).bCommitted);
	CatFishingCaptureSliceTest::AdvanceFishingStateTree(*StateTree, 0.1f);
	TestEqual(TEXT("向外游+拖：竿 25 ≤ min(50,30) 命中判定表①断竿"), Session->GetSnapshot().FightOutcome, ECatFishingFightOutcome::RodBroken);
	const FCatEquipmentLoadoutSnapshot LoadoutAfter = Fisher.Character->GetEquipmentComponent()->GetSnapshot();
	TestTrue(TEXT("断竿后 Equipment 快照 bRodBroken=true"), LoadoutAfter.bRodBroken);
	TestEqual(TEXT("断竿后鱼竿耐久归零"), LoadoutAfter.RodDurability, 0.0, 1e-9);
	TestEqual(TEXT("断竿让树走失败边进入 Terminated"), Session->GetSnapshot().Phase, ECatFishingPhase::Terminated);
	TestTrue(TEXT("断竿后会话进入终态"), Session->IsTerminal());
	return !HasAnyErrors();
}

// 测试流程：拿一只已装配 starter 三件套的猫，直接对抛竿几何断言——落点在猫正前方、离猫的距离等于当前漂的射程
// （允许精准度偏移这一圈误差）、同一次抛竿可复现、不同抛竿会飘到不同地方，而且落点始终留在水面高度上。
// D₀ 就是这个距离，所以换一只射程不同的漂，遛鱼开局要收的线也跟着变；漂之间的射程差异由装备目录用例单独锁。
bool FCatFishingFloatCastSliceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建抛竿几何测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		return false;
	}
	const CatFishingCaptureSliceTest::FFisherFixture Fisher =
		CatFishingCaptureSliceTest::SpawnAdmittedFisher(*this, *World, TEXT("player:slice-float-cast-fisher"));
	if (!Fisher.Controller || !Fisher.Character)
	{
		return false;
	}

	const FVector FisherLocation(0.0, 0.0, 0.0);
	Fisher.Character->SetActorLocation(FisherLocation);
	Fisher.Character->SetActorRotation(FRotator(0.0, 0.0, 0.0));
	const FName FloatDefinitionId = Fisher.Character->GetEquipmentComponent()->GetSnapshot().FloatDefinitionId;
	const UCatEquipmentDefinition* FloatDefinition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(FloatDefinitionId);
	TestNotNull(TEXT("starter 鱼漂已进入运行目录"), FloatDefinition);
	if (!FloatDefinition)
	{
		return false;
	}

	const FGuid RequestId = FGuid::NewGuid();
	FVector CastLocation = FVector::ZeroVector;
	TestTrue(TEXT("装着漂的猫可以算出落点"),
		UCatFishingService::TryResolveFloatCastLocation(*Fisher.Character, RequestId, CastLocation));

	const double CastDistanceMeters = FVector::Dist(FisherLocation, CastLocation) / 100.0;
	TestTrue(TEXT("落点距离不短于射程减去精准度偏移"),
		CastDistanceMeters >= FloatDefinition->FloatCastRangeMeters - FloatDefinition->FloatAccuracyOffsetRadiusMeters - 1e-6);
	TestTrue(TEXT("落点距离不超过射程加上精准度偏移"),
		CastDistanceMeters <= FloatDefinition->FloatCastRangeMeters + FloatDefinition->FloatAccuracyOffsetRadiusMeters + 1e-6);
	TestTrue(TEXT("落点落在猫的正前方"),
		FVector::DotProduct((CastLocation - FisherLocation).GetSafeNormal(), Fisher.Character->GetActorForwardVector()) > 0.0);
	TestEqual(TEXT("落点保持在猫所在的水平高度"), CastLocation.Z, FisherLocation.Z, 1e-6);

	FVector ReplayLocation = FVector::ZeroVector;
	TestTrue(TEXT("同一次抛竿可重算"),
		UCatFishingService::TryResolveFloatCastLocation(*Fisher.Character, RequestId, ReplayLocation));
	TestEqual(TEXT("同一 RequestId 的落点可复现"), ReplayLocation, CastLocation);

	// 精准度的意义是"每一竿都会飘一点"，所以不同 RequestId 必须能落到别处；只要 16 次里出现过一次不同就足够证明偏移生效。
	bool bFoundDifferentLanding = false;
	for (int32 Attempt = 0; Attempt < 16 && !bFoundDifferentLanding; ++Attempt)
	{
		FVector OtherLocation = FVector::ZeroVector;
		bFoundDifferentLanding = UCatFishingService::TryResolveFloatCastLocation(
			*Fisher.Character, FGuid::NewGuid(), OtherLocation) && !OtherLocation.Equals(CastLocation, 1e-3);
	}
	TestTrue(TEXT("换一次抛竿落点会因精准度偏移而不同"), bFoundDifferentLanding);

	// 朝向被摆成纯竖直时没有水平方向可用，此时宁可不抛，也不随便挑一个方向把漂丢出去。
	Fisher.Character->SetActorRotation(FRotator(90.0, 0.0, 0.0));
	FVector VerticalCastLocation(7.0, 7.0, 7.0);
	TestFalse(TEXT("朝向退化成纯竖直时算不出落点"),
		UCatFishingService::TryResolveFloatCastLocation(*Fisher.Character, RequestId, VerticalCastLocation));
	TestEqual(TEXT("算不出落点时输出清零"), VerticalCastLocation, FVector::ZeroVector);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
