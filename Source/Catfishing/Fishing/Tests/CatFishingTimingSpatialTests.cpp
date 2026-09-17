#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "Tests/AutomationCommon.h"
#include "Fishing/CatFishingSession.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Growth/CatGrowthComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Integration/CatFishingResolutionSubsystem.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Fishing/CatFishingService.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Items/CatEquipmentItemAbilities.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Data/CatFishCatalogSettings.h"
#include "Social/CatRoomOwnerService.h"
#include "OnlineSubsystemTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "UI/CatFishingViewTypes.h"

namespace CatR3Tests
{
	void Advance(FTestWorldWrapper& Wrapper, double Seconds)
	{
		// 真实帧推进，避免 World 对单帧大 Delta 的钳制把 3 秒误测成不足一秒。
		for (int32 I = 0, Count = FMath::RoundToInt(Seconds * 100.0); I < Count; ++I)
			Wrapper.TickTestWorld(0.01f);
	}
	struct FFixture
	{
		FTestWorldWrapper Wrapper;
		ACatCharacter* Cat = nullptr;
		ACatfishingPlayerController* Controller = nullptr;
		ACatfishingPlayerState* Player = nullptr;
		UCatEquipmentComponent* Equipment = nullptr;
		FGuid SessionId = FGuid::NewGuid();
		void AdmitController()
		{
			auto* Mode = Wrapper.GetTestWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
			ACatfishingGameModeBase::FAdmissionRecord Admission;
			Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
			Admission.Controller = Controller;
			Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		}
		bool Init(FAutomationTestBase& Test, bool bWithDay = false)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
			auto* World = Wrapper.GetTestWorld();
			if (bWithDay)
			{
				FURL URL;
				URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
				if (!World->SetGameMode(URL)) return false;
			}
			if (!Wrapper.BeginPlayInTestWorld()) return false;
			if (auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>(); bWithDay && Mode)
			{
				Mode->bRunCommandsOpen = true;
				Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
				Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
			}
			Cat = World->SpawnActor<ACatCharacter>();
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			Player = World->SpawnActor<ACatfishingPlayerState>();
			if (!Cat || !Controller || !Player) return false;
			Controller->PlayerState = Player;
			const FUniqueNetIdRef PrimaryId = FUniqueNetIdString::Create(TEXT("R3Primary"), TEXT("CAT_TEST"));
			Player->SetUniqueId(FUniqueNetIdRepl(PrimaryId));
			Controller->Possess(Cat);
			Cat->SetPlayerState(Player);
			Cat->SetActorTickEnabled(false);
			Equipment = Cat->GetEquipmentComponent();
			for (const int32 Id : {37, 9})
				if (!Test.TestTrue(TEXT("正式钓具入库"), Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			if (!Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, 4, 2).bCommitted) return false;
			if (!Equipment->Use(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Equipment->GetSnapshot().RodItemInstanceId).bCommitted) return false;
			const auto Loadout = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				Loadout.RodItemId, Loadout.BaitItemId, Loadout.FloatItemId, Loadout.Revision).bUseAccepted;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingR3BaitDistanceTest,
	"Catfishing.Unit.Fishing.Timing.CurrentBaitAndTrueBiteDistanceAreSampledAtTheirOwnBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingR3BaitDistanceTest::RunTest(const FString& Parameters)
{
	for (const bool bOverlong : {false, true})
	{
		CatR3Tests::FFixture F;
		if (!F.Init(*this, true)) return false;
		TestTrue(TEXT("另一款真实鱼饵入库"), F.Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), F.Equipment->GetSnapshot().Revision, 16, 2).bCommitted);
		auto* Inventory = F.Cat->GetInventoryComponent();
		const auto* Bait = Inventory->GetInventoryEntryAtSlot(Inventory->FindFirstInventorySlotIndexByItemId(16));
		if (!TestNotNull(TEXT("换饵实例存在"), Bait)) return false;
		const auto L = F.Equipment->GetSnapshot();
		TestTrue(TEXT("等待期真实换饵"), F.Equipment->ConfigureLoadoutFromAuthority(FGuid::NewGuid(), L.Revision,
			L.RodItemId, 16, L.FloatItemId, L.ScoopNetItemId, L.RodSkinDefinitionId,
			L.RodItemInstanceId, Bait->Instance->GetItemInstanceId(), L.FloatItemInstanceId, L.ScoopNetItemInstanceId).bCommitted);
		TestEqual(TEXT("抽魚入口读当前饵"), F.Equipment->GetCurrentFishingBaitItemId(F.SessionId), 16);
		auto* World = F.Wrapper.GetTestWorld();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		Session->Snapshot.FishingSessionId = F.SessionId;
		Session->Snapshot.Phase = ECatFishingPhase::Probe;
		Session->Snapshot.HookActor = World->SpawnActor<ACatFishingHookActor>();
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.HookActor->InitializeAuthoritativeIdentity(F.SessionId, Session->Snapshot.CastAttemptId);
		Session->SelectionResolution = ECatFishSelectionResolution::Selected;
		// T10 夹具迁移墓碑（钓鱼规则 §3.4）：选鱼边界现在必须提供鱼种普通响应窗；原扣饵/距离断言全部保留。
		Session->FishDefinition = NewObject<UCatFishDefinition>();
		Session->FishDefinition->ItemId = 1186446;
		Session->FishDefinition->TrueBiteWindowSeconds = 12.0;

		Session->CastEquipment = F.Equipment;
		Session->FisherCharacter = F.Cat;
		Session->AttemptSnapshot.RodItemId = L.RodItemId;
		Session->AttemptSnapshot.RodItemInstanceId = L.RodItemInstanceId;
		Session->bStartupInProgress = true;
		F.Cat->SetActorLocation(FVector(300, 0, 0));
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector(bOverlong ? 10000 : 1000, 0, 0);
		const bool bOpened = Session->OpenTrueBiteWindowFromAuthority();
		TestNull(TEXT("真咬距离门不依赖提前生成鱼"), Session->Snapshot.FishEncounterActor.Get());
		TestEqual(TEXT("真咬超 Lmax 不进入合法窗口"), bOpened, !bOverlong);
		TestEqual(TEXT("抛竿时旧饵从未预扣，无需退款"), Inventory->CountVisibleInventoryQuantityByItemId(4), 2);
		TestEqual(TEXT("只扣真咬当前一份饵"), Inventory->CountVisibleInventoryQuantityByItemId(16), 1);
		if (bOverlong) TestEqual(TEXT("超长鱼逃"), Session->GetSnapshot().Outcome, ECatFishingOutcome::Escaped);
		else
		{
			TestEqual(TEXT("实际普通响应计时器使用鱼种 12 秒"),
				double(World->GetTimerManager().GetTimerRemaining(Session->TrueBiteTimerHandle)), 12.0);
			TestEqual(TEXT("实际完美窗保持基础 1 秒"),
				Session->Snapshot.PerfectWindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, 1.0);
			TestEqual(TEXT("真咬 D0 使用移动后的猫到冻结落点距离"), Session->TrueBiteDistanceCentimeters, 700.0);
			F.Cat->SetActorLocation(FVector(500, 0, 0));
			TestEqual(TEXT("响应窗移动不改冻结 D0"), Session->TrueBiteDistanceCentimeters, 700.0);
			F.Equipment->CommitFishingBaitDeferred(F.SessionId);
			TestEqual(TEXT("真咬重放不重复扣饵"), Inventory->CountVisibleInventoryQuantityByItemId(16), 1);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingR3ResolutionTest,
	"Catfishing.Unit.Fishing.Arbitration.ServerTimestampSeatAndCatchRevivalWaterOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingR3ResolutionTest::RunTest(const FString& Parameters)
{
	CatR3Tests::FFixture F;
	if (!F.Init(*this)) return false;
	auto* World = F.Wrapper.GetTestWorld();
	auto* Second = World->SpawnActor<ACatfishingPlayerController>();
	auto* Player = World->SpawnActor<ACatfishingPlayerState>();
	Second->PlayerState = Player;
	const FUniqueNetIdRef SecondId = FUniqueNetIdString::Create(TEXT("R3Second"), TEXT("CAT_TEST"));
	Player->SetUniqueId(FUniqueNetIdRepl(SecondId));
	auto* Room = World->GetSubsystem<UCatRoomOwnerService>();
	auto* Queue = World->GetSubsystem<UCatFishingResolutionSubsystem>();
	if (!Room || !Queue) return false;
	Room->JoinSequenceByPlayer.Add(TEXT("R3Primary"), 1);
	Room->JoinSequenceByPlayer.Add(TEXT("R3Second"), 2);
	TArray<int32> Order;
	Queue->Enqueue(ECatFishingResolution::Water, nullptr, FGuid::NewGuid(), [&]() { Order.Add(4); });
	Queue->Enqueue(ECatFishingResolution::Revival, nullptr, FGuid::NewGuid(), [&]() { Order.Add(3); });
	Queue->Enqueue(ECatFishingResolution::Catch, Second, FGuid::NewGuid(), [&]() { Order.Add(2); });
	Queue->Enqueue(ECatFishingResolution::Catch, F.Controller, FGuid::NewGuid(), [&]() { Order.Add(1); });
	TestTrue(TEXT("接收回调不提前裁决"), Order.IsEmpty());
	World->Tick(LEVELTICK_All, 0.01f);
	TestTrue(TEXT("同刻逆序提交仍按席位收鱼→苏醒→落水"), Order == TArray<int32>({1, 2, 3, 4}));
	World->Tick(LEVELTICK_All, 0.01f);
	TestEqual(TEXT("队列只执行一次"), Order.Num(), 4);
	// 真实 Pickup 消费者：反序到达的同刻请求不能让回调先执行者拿走实物。
	auto* OtherCat = World->SpawnActor<ACatCharacter>();
	Second->Possess(OtherCat);
	OtherCat->SetPlayerState(Player);
	F.Cat->SetActorLocation(FVector(0, -50, 0));
	OtherCat->SetActorLocation(FVector(0, 50, 0));
	F.Cat->GetCharacterMovement()->DisableMovement();
	OtherCat->GetCharacterMovement()->DisableMovement();
	UCatFishDefinition* Definition = nullptr;
	for (const auto& Asset : GetDefault<UCatFishCatalogSettings>()->Definitions)
	{
		auto* Candidate = Asset.LoadSynchronous();
		if (Candidate && Candidate->IsRuntimeDefinitionReady()) { Definition = Candidate; break; }
	}
	auto* Pickup = World->SpawnActor<ACatFishPickupActor>(FVector(100, 0, 0), FRotator::ZeroRotator);
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = TEXT("R3TestRegion");
	if (!TestTrue(TEXT("正式鱼种生成真实落地鱼"), Pickup && Definition && Pickup->InitializeFromAuthority(
		F.SessionId, FGuid::NewGuid(), Definition, 1.0, 1.0, Condition, TEXT("R3Primary"), {}))) return false;
	const FGuid LoserRequest = FGuid::NewGuid(), WinnerRequest = FGuid::NewGuid();
	Pickup->Interact_Implementation(Second, LoserRequest);
	Pickup->Interact_Implementation(F.Controller, WinnerRequest);
	TestEqual(TEXT("未仲裁前鱼仍在地上"), Pickup->GetPresentationState().State, ECatFishPickupState::Available);
	F.Wrapper.TickTestWorld(0.01f);
	TestEqual(TEXT("席位早者叼走唯一实物"), ACatFishPickupActor::FindCarriedFish(F.Cat), Pickup);
	TestNull(TEXT("失败者嘴里没有第二份鱼"), ACatFishPickupActor::FindCarriedFish(OtherCat));
	TestTrue(TEXT("抢落地鱼失败进入完整硬直"), UCatGE_FishingScoopCooldown::IsOperationBlocked(OtherCat));
	TestFalse(TEXT("真正成功收鱼不受硬直"), UCatGE_FishingScoopCooldown::IsOperationBlocked(F.Cat));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingR3HoldTest,
	"Catfishing.Unit.Fishing.Input.CancelHoldReleaseAndMissStunUseServerTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingR3HoldTest::RunTest(const FString& Parameters)
{
	CatR3Tests::FFixture F;
	if (!F.Init(*this)) return false;
	auto* World = F.Wrapper.GetTestWorld();
	auto* Session = World->SpawnActor<ACatFishingSession>();
	auto* Rod = World->SpawnActor<ACatFishingRodActor>();
	const auto L = F.Equipment->GetSnapshot();
	// 禁止无地板夹具自由坠落；停运动会释放握持，必须放在真实握竿之前。
	F.Cat->GetCharacterMovement()->DisableMovement();
	if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), L.RodItemInstanceId, L.RodItemId, NAME_None, F.Player, F.Player, true, false)) return false;
	if (!TestTrue(TEXT("真实握竿先于保持计时"), Rod->BeginPhysicalHoldFromAuthority(F.Player, true)
		&& Rod->SetPrimaryOperatorFromAuthority(F.Player, Rod->GetPresentationState().RodActorRevision)
		&& Rod->GetPhysicalRodComponent()->CommitPrimaryHold(F.Player))) return false;
	Session->Snapshot.RodActor = Rod;
	Session->Snapshot.FishingSessionId = F.SessionId;
	Session->Snapshot.CastAttemptId = FGuid::NewGuid();
	Session->Snapshot.FisherPlayerState = F.Player;
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Session->CastEquipment = F.Equipment;
	Session->FisherCharacter = F.Cat;
	TestTrue(TEXT("服务器接受保持"), Session->SetCancelHeldFromAuthority(F.Controller, true, FGuid::NewGuid()).bCommitted);
	TestEqual(TEXT("权威保持区间为1.5秒"), Session->Snapshot.CancelHoldEndsServerTime - Session->Snapshot.CancelHoldStartedServerTime, 1.5);
	Session->SetCancelHeldFromAuthority(F.Controller, false, FGuid::NewGuid());
	CatR3Tests::Advance(F.Wrapper, 1.6);
	TestFalse(TEXT("提前松手不结束本竿"), Session->IsTerminal());
	TestEqual(TEXT("松手移除进度"), Session->Snapshot.CancelHoldEndsServerTime, 0.0);
	bool bHoldAccepted = false;
	World->GetSubsystem<UCatFishingResolutionSubsystem>()->Enqueue(ECatFishingResolution::Catch,
		F.Controller, FGuid::NewGuid(), [&]()
		{
			bHoldAccepted = Session->SetCancelHeldFromAuthority(F.Controller, true, FGuid::NewGuid()).bCommitted;
		});
	F.Wrapper.TickTestWorld(0.01f); // 在游戏帧内发起，计时器不受帧外夹具的 pending 一帧偏移。
	TestTrue(TEXT("松手后可重新保持"), bHoldAccepted);
	CatR3Tests::Advance(F.Wrapper, 1.0);
	TestFalse(TEXT("一秒不能放弃"), Session->IsTerminal());
	CatR3Tests::Advance(F.Wrapper, 0.51);
	TestTrue(TEXT("完整保持后权威终局"), Session->IsTerminal());
	TestEqual(TEXT("主动放弃走鱼逃切线结果"), Session->Snapshot.Outcome, ECatFishingOutcome::LineCut);
	double StunStart = -1.0;
	World->GetSubsystem<UCatFishingResolutionSubsystem>()->Enqueue(ECatFishingResolution::Catch,
		F.Controller, FGuid::NewGuid(), [&]()
		{
			UCatGE_FishingScoopCooldown::ApplyMissFromAuthority(F.Controller);
			StunStart = World->GetTimeSeconds();
		});
	F.Wrapper.TickTestWorld(0.01f);
	TestTrue(TEXT("失败锁完整操作"), UCatGE_FishingScoopCooldown::IsOperationBlocked(F.Cat));
	CatR3Tests::Advance(F.Wrapper, 2.9);
	TestTrue(TEXT("2.9秒仍硬直"), UCatGE_FishingScoopCooldown::IsOperationBlocked(F.Cat));
	UCatGE_FishingScoopCooldown::ApplyMissFromAuthority(F.Controller);
	CatR3Tests::Advance(F.Wrapper, 0.11);
	TestEqual(TEXT("服务器世界实际推进完整硬直区间"), double(World->GetTimeSeconds()) - StunStart, 3.01, 0.002);
	TestFalse(TEXT("3秒恢复，硬直中重复请求不续期"), UCatGE_FishingScoopCooldown::IsOperationBlocked(F.Cat));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingR3PositiveFishStaminaTest,
	"Catfishing.Unit.Fishing.Effort.PositiveFishBalanceNeverSnapsToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingR3PositiveFishStaminaTest::RunTest(const FString& Parameters)
{
	FCatFightSimulationConfig C;
	C.FixedStepSeconds = 0.05; C.PrimaryOperatorCatStrength = 50; C.PrimaryOperatorMassKilograms = 5;
	C.FishStrength = 50; C.FishMassKilograms = 5; C.RodDurability = 1000; C.CatStaminaMaximum = 60;
	C.ReelSpeedCentimetersPerSecond = 80; C.FishFullEffortSpeedCentimetersPerSecond = 180;
	C.MaximumLineLengthCentimeters = 1500; C.FishStaminaPerUnfulfilledMeter = 0.001;
	FCatFightSimulationState S;
	S.CatStamina = 60; S.FishStamina = 0.1; S.FishWorldPosition = FVector(500, 0, 0);
	S.LineLengthCentimeters = 500; S.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Rod;
	Rod.bRodHeld = true; Rod.RodForwardWorld = FVector::RightVector;
	const auto Step = FCatFishingFightSimulator::Step(C, S, Rod, FVector::ForwardVector);
	TestTrue(TEXT("实际对抗步骤成立"), Step.bSucceeded);
	TestTrue(TEXT("真实小费用保留正体力"), Step.FishStaminaDrain > 0 && Step.FishStaminaDrain < S.FishStamina);
	TestEqual(TEXT("正余额不翻肚"), Step.Outcome, ECatFightStepOutcome::None);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatGrowthWearDeliveryTest,
	"Catfishing.Unit.Growth.CatchWearChoiceReachesEquipmentTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatGrowthWearDeliveryTest::RunTest(const FString&)
{
	CatR3Tests::FFixture F;
	if (!F.Init(*this)) return false;
	auto* Growth = F.Cat->GetGrowthComponent();
	Growth->Snapshot.CompletedChoiceCount = 4;
	Growth->Snapshot.PendingChoiceCount = 1;
	Growth->Snapshot.CurrentOffer = {ECatGrowthOptionId::RodWear};
	Growth->Snapshot.OfferSerial = 1;
	TestTrue(TEXT("选择命令产生竿磨损减免"), Growth->ChooseOfferedOptionFromAuthority(F.Controller,
		FGuid::NewGuid(), ECatGrowthOptionId::RodWear, 1).bCommitted);
	auto* Session = F.Wrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	Session->Snapshot.FishingSessionId = F.SessionId;
	Session->FisherCharacter = F.Cat;
	Session->CastEquipment = F.Equipment;
	Session->AttemptSnapshot.RodItemInstanceId = F.Equipment->GetSnapshot().RodItemInstanceId;
	double Before = 0.0, After = 0.0;
	bool Broken = false;
	TestFalse(TEXT("未真咬不能在收鱼阶段补扣鱼饵"), Session->CommitCatchEquipmentFromAuthority());
	TestEqual(TEXT("拒绝收鱼保持原鱼饵数量"), F.Cat->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 2);
	if (!TestTrue(TEXT("真咬前置只扣一份饵"), F.Equipment->CommitFishingBaitDeferred(F.SessionId).bApplied)) return false;
	TestTrue(TEXT("真实竿耐久可读取"), F.Equipment->GetFishingRodDurability(F.SessionId, Before, Broken));
	TestTrue(TEXT("收鱼生产扣费成功"), Session->CommitCatchEquipmentFromAuthority());
	TestTrue(TEXT("真实竿耐久回执可读取"), F.Equipment->GetFishingRodDurability(F.SessionId, After, Broken));
	TestTrue(TEXT("每条 -1 经成长后只扣 0.9，未回补竿"), FMath::IsNearlyEqual(Before - After, 0.9, 0.000001));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCatalogTimingTimersTest,
	"Catfishing.Unit.Fishing.BiteTiming.FormalRarityDefaultsReachResponseTimersAndSeparatePerfectWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingCatalogTimingTimersTest::RunTest(const FString& Parameters)
{
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	for (const auto& Ref : Catalog->Definitions)
	{
		CatR3Tests::FFixture F;
		if (!F.Init(*this, true)) return false;
		auto* World = F.Wrapper.GetTestWorld();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		auto* Fish = Ref.LoadSynchronous();
		if (!Fish || !Session) return false;
		Session->FishDefinition = Fish;
		Session->Snapshot.FishingSessionId = F.SessionId;
		Session->Snapshot.ItemId = Fish->ItemId;
		Session->Snapshot.Phase = ECatFishingPhase::Probe;
		Session->Snapshot.HookActor = World->SpawnActor<ACatFishingHookActor>();
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.HookActor->InitializeAuthoritativeIdentity(F.SessionId, Session->Snapshot.CastAttemptId);
		Session->SelectionResolution = ECatFishSelectionResolution::Selected;
		Session->CastEquipment = F.Equipment;
		Session->FisherCharacter = F.Cat;
		Session->AttemptSnapshot.RodItemId = F.Equipment->GetSnapshot().RodItemId;
		Session->AttemptSnapshot.RodItemInstanceId = F.Equipment->GetSnapshot().RodItemInstanceId;
		Session->bStartupInProgress = true;
		F.Cat->SetActorLocation(FVector(0, 0, 100));
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector(1000, 0, 0);
		if (!TestTrue(TEXT("正式鱼默认值进入真实真咬开窗入口"), Session->OpenTrueBiteWindowFromAuthority())) return false;
		const double Expected = Catalog->ResolveBiteTiming(*Fish).TrueBiteWindowSeconds;
		TestEqual(TEXT("计时器真正使用配置响应秒数"), double(World->GetTimerManager().GetTimerRemaining(Session->TrueBiteTimerHandle)), Expected);
		TestEqual(TEXT("复制快照使用相同响应秒数"), Session->Snapshot.WindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, Expected);
		TestEqual(TEXT("完美窗独立保持1秒"), Session->Snapshot.PerfectWindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, 1.0);
		const auto View = FCatFishingViewState::FromSnapshot(Session->Snapshot);
		TestEqual(TEXT("UI收到同一响应截止时间"), View.WindowEndsServerTime, Session->Snapshot.WindowEndsServerTime);
		TestEqual(TEXT("UI收到独立完美截止时间"), View.PerfectWindowEndsServerTime, Session->Snapshot.PerfectWindowEndsServerTime);
		TestEqual(TEXT("真咬成立只扣一份饵"), F.Cat->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(4), 1);
		World->GetTimerManager().ClearTimer(Session->TrueBiteTimerHandle);
		F.Equipment->ReleaseFishingUse(F.SessionId);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingPerfectLineProductionTest,
	"Catfishing.Unit.Fishing.Timing.FormalPerfectLineMultiplierReachesFightRunnerAndFishPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingPerfectLineProductionTest::RunTest(const FString& Parameters)
{
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	TestEqual(TEXT("读取正式ini完美线长倍率"), Catalog->PerfectInitialLineLengthMultiplier, 0.9);
	for (const int32 Scenario : {0, 1, 2, 3})
	{
		const bool bPerfect = Scenario == 1;
		CatR3Tests::FFixture F;
		if (!F.Init(*this, true)) return false;
		auto* World = F.Wrapper.GetTestWorld();
		F.AdmitController();
		FCatWaterGeometryBuildInput Geometry;
		Geometry.RegionId = TEXT("River");
		Geometry.WaterPointVerticalToleranceCm = 100;
		Geometry.BankHeightToleranceCm = 50;
		Geometry.BoundaryToleranceCm = 1;
		Geometry.MaxLandingCorrectionCm = 100;
		Geometry.MinimumWaterInsetCm = 1;
		auto& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("TimingLineWater");
		Boundary.Vertices = {{-5000, -5000}, {5000, -5000}, {5000, 5000}, {-5000, 5000}};
		const auto L = F.Equipment->GetSnapshot();
		const auto* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(L.RodItemId);
		// 两条正式鱼提供不同游速系数，防止模板基速误读被系数1的样本掩盖。
		const auto* Fish = Catalog->FindRuntimeDefinition(bPerfect ? 22 : 30);
		const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
		if (!TestTrue(TEXT("正式鱼、竿、平衡资产齐全"), RodDefinition && Fish && Balance)) return false;
		auto* Rod = World->SpawnActor<ACatFishingRodActor>(RodDefinition->GetEquipmentDefinition()->ActorClass.LoadSynchronous());
		F.Cat->GetCharacterMovement()->DisableMovement();
		F.Cat->SetActorLocation(FVector(0, 0, 100));
		if (!TestTrue(TEXT("真实鱼竿绑定并建立握持"), Rod && Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), L.RodItemInstanceId,
			L.RodItemId, NAME_None, F.Player, F.Player, true, false)
			&& Rod->BeginPhysicalHoldFromAuthority(F.Player, true)
			&& Rod->GetPhysicalRodComponent()->CommitPrimaryHold(F.Player))) return false;
		// 竿尖与水面同高，隔离水面吸附补足物理线长的既有安全修正，直接验证倍率消费。
		const double SurfaceZ = Rod->GetRodTipWorldTransform().GetLocation().Z;
		Geometry.PlaneToWorld = FTransform(FVector(0, 0, SurfaceZ));
		const auto Built = FCatWaterGeometry::Build(Geometry);
		auto* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
		if (!TestTrue(TEXT("真实水面几何可用"), Built.bSucceeded && Region)) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
		Region->FinishSpawning(FTransform::Identity);
		auto* Session = World->SpawnActor<ACatFishingSession>();
		const FVector InitialFishPosition(1000, 0, Built.Cache.PlaneToWorld.GetLocation().Z);
		const double D0 = FVector::Distance(F.Cat->GetActorLocation(), InitialFishPosition);
		const FGuid CastId = FGuid::NewGuid();
		auto* Hook = World->SpawnActor<ACatFishingHookActor>();
		if (!TestTrue(TEXT("真实鱼钩接收同场身份和落点"), Hook && Hook->InitializeAuthoritativeIdentity(F.SessionId, CastId)
			&& Hook->FinalizeAuthoritativeLandingOnce(true, InitialFishPosition))) return false;
		Hook->SetOwner(Rod);
		Session->FishDefinition = const_cast<UCatFishDefinition*>(Fish);
		Session->FishWeightKilograms = 2.0;
		Session->Snapshot.FishingSessionId = F.SessionId;
		Session->Snapshot.CastAttemptId = CastId;
		Session->Snapshot.ItemId = Fish->ItemId;
		Session->Snapshot.HookActor = Hook;
		Session->Snapshot.RodActor = Rod;
		Session->Snapshot.FisherPlayerState = F.Player;
		Session->Snapshot.Phase = ECatFishingPhase::TrueBiteWindow;
		Session->Snapshot.PhaseStartedServerTime = World->GetTimeSeconds();
		Session->Snapshot.WindowEndsServerTime = World->GetTimeSeconds() + 10.0;
		Session->Snapshot.PerfectWindowEndsServerTime = World->GetTimeSeconds() + (bPerfect ? 1.0 : -1.0);
		Session->bTrueBiteWindowAcceptingHook = true;
		Session->Snapshot.FishStrength = 20.0;
		Session->Snapshot.FishFightStaminaRemaining = 100.0;
		Session->SelectionResolution = ECatFishSelectionResolution::Selected;
		Session->FrozenSelectionResult.BaseFishStrength = 20.0;
		Session->FrozenSelectionContext.StrengthPerKilogram = Balance->StrengthPerKilogram;
		Session->AttemptSnapshot.RodActor = Rod;
		Session->AttemptSnapshot.RodItemId = L.RodItemId;
		Session->AttemptSnapshot.RodItemInstanceId = L.RodItemInstanceId;
		Session->AttemptSnapshot.CastAttemptId = CastId;
		Session->AttemptSnapshot.WaterRegion = Built.Cache.Handle;
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = InitialFishPosition;
		Session->TrueBiteDistanceCentimeters = D0;
		Session->FisherCharacter = F.Cat;
		Session->CastEquipment = F.Equipment;
		Session->bStartupInProgress = true;
		// 猫力超过鱼力两倍，普通和完美提钩都必须进入真实搏斗，不得直接交鱼。
		F.Cat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
		// 夹具仅准备真咬已经完成的扣饵和冻结数据，生成实体与初始化搏斗走正式提钩事务。
		if (!TestTrue(TEXT("中鱼前置通过唯一库存入口提交鱼饵"), F.Equipment->CommitFishingBaitDeferred(F.SessionId).bApplied)) return false;
		const auto CountFishActors = [&]()
		{
			int32 Count = 0;
			for (TActorIterator<ACatFishEncounterActor> It(World); It; ++It)
				if (IsValid(*It)) ++Count;
			return Count;
		};
		TestEqual(TEXT("有效提钩前没有鱼实体"), CountFishActors(), 0);
		const FGuid HookRequestId = FGuid::NewGuid();
		if (Scenario == 3)
		{
			auto* RodFragment = const_cast<UCatEquipmentFragment_Rod*>(RodDefinition->FindFragment<UCatEquipmentFragment_Rod>());
			TGuardValue<double> WeakRod(RodFragment->FishingStrength, 10.0);
			AddExpectedErrorPlain(TEXT("Event=fishing_rod_strength_snapped"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedErrorPlain(TEXT("Event=fishing_rod_broken"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::LineBroken"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue(TEXT("legal hook acknowledges immediate rod break"), Session->RequestHookFromAuthority(HookRequestId).bCommitted);
			TestEqual(TEXT("rod break is not rewritten as initialization failure"), Session->Snapshot.Outcome, ECatFishingOutcome::LineBroken);
			TestTrue(TEXT("terminal hook result remains replayable"), Session->RequestHookFromAuthority(HookRequestId).bCommitted);
			continue;
		}
		if (Scenario == 2)
		{
			auto* Presentation = GetMutableDefault<UCatFishingPresentationSettings>();
			TGuardValue<TSoftClassPtr<ACatFishEncounterActor>> MissingClass(Presentation->FishEncounterActorClass, {});
			AddExpectedErrorPlain(TEXT("Event=fishing_hook_fish_spawn_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Invalidated"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("实体类缺失拒绝有效提钩"), Session->RequestHookFromAuthority(HookRequestId).bCommitted);
			TestEqual(TEXT("生成失败终止会话"), Session->Snapshot.Outcome, ECatFishingOutcome::Invalidated);
			TestFalse(TEXT("失败请求重放仍返回原拒绝"), Session->RequestHookFromAuthority(HookRequestId).bCommitted);
			TestEqual(TEXT("生成失败及重放没有残留实体"), CountFishActors(), 0);
			TestFalse(TEXT("生成失败不遗留搏斗"), Session->IsFightRunnerRunning());
			TestFalse(TEXT("生成失败释放装备使用记录"), F.Equipment->IsFishingUseActive(F.SessionId));
			continue;
		}
		if (!TestTrue(TEXT("有效提钩生成实体并启动真实Runner"), Session->RequestHookFromAuthority(HookRequestId).bCommitted && Session->IsFightRunnerRunning())) return false;
		auto* Encounter = Session->Snapshot.FishEncounterActor.Get();
		if (!TestNotNull(TEXT("有效提钩绑定实体"), Encounter)) return false;
		TestEqual(TEXT("生成鱼沿用已选鱼种"), Encounter->GetPresentationState().ItemId, Fish->ItemId);
		TestEqual(TEXT("生成鱼沿用冻结比例"), Encounter->GetPresentationState().VisualScale, Session->FishVisualScale);
		TestEqual(TEXT("提钩判定仍区分普通和完美"), Session->Snapshot.bPerfectHook, bPerfect);
		TestTrue(TEXT("同一提钩请求重放返回原回执"), Session->RequestHookFromAuthority(HookRequestId).bCommitted);
		TestFalse(TEXT("搏斗中新的提钩请求被拒绝"), Session->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
		TestEqual(TEXT("重复提钩只保留一个实体"), CountFishActors(), 1);
		TestEqual(TEXT("重复提钩不替换实体"), Session->Snapshot.FishEncounterActor.Get(), Encounter);
		TestTrue(TEXT("生产入口读取正式非零嘴部标定并绑定真实钩嘴"), Session->FightRunner->Config.FishBody.Geometry.HasMouthLever()
			&& Hook->GetAttachParentActor() == Encounter && Hook->GetActorLocation().Equals(Encounter->GetMouthWorldLocation(), 0.001));
		const auto* Template = GetDefault<UCatFishingSettings>()->FindFightPersonality(Fish->FightPersonalityId);
		if (!TestNotNull(TEXT("未裁横切参数仍有正式模板来源"), Template)) return false;
		TestTrue(TEXT("生产Runner外冲节拍使用正式逐鱼列"), Session->FightRunner->SteeringConfig.OutwardDurationRangeSeconds.Equals(Fish->OutwardSegmentDurationRangeSeconds, 1e-9));
		TestTrue(TEXT("生产Runner缓游节拍使用正式逐鱼列"), Session->FightRunner->SteeringConfig.EaseOffDurationRangeSeconds.Equals(Fish->RestSegmentDurationRangeSeconds, 1e-9));
		const double ExpectedFullEffortSpeed = Template->FullEffortMovementSpeedCentimetersPerSecond * Fish->SwimSpeedCoefficient;
		TestEqual(TEXT("生产Runner游速消费逐鱼系数"), Session->FightRunner->Config.FishFullEffortSpeedCentimetersPerSecond, ExpectedFullEffortSpeed, 1e-9);
		TestEqual(TEXT("初始鱼表现消费与Runner相同的逐鱼游速"), double(Encounter->GetPresentationState().IntendedSwimSpeedCentimetersPerSecond), ExpectedFullEffortSpeed, 1e-4);
		TestTrue(TEXT("真实StateTree首段时长落在逐鱼发力区间"),
			Session->FightRunner->SteeringState.BehaviorDurationSeconds >= Fish->OutwardSegmentDurationRangeSeconds.X
			&& Session->FightRunner->SteeringState.BehaviorDurationSeconds <= Fish->OutwardSegmentDurationRangeSeconds.Y);
		TestTrue(TEXT("横切时长保留模板不借逐鱼发力列重定义"), Session->FightRunner->SteeringConfig.LateralDurationRangeSeconds.Equals(Template->AdaptiveSteeringConfig.LateralDurationRangeSeconds, 1e-9));
		if (bPerfect) TestTrue(TEXT("游速测试具有区别于模板的正式系数"), Fish->SwimSpeedCoefficient != 1.0);
		TestTrue(TEXT("强猫提钩后保持活跃且未生成岸上渔获"), !Session->IsTerminal()
			&& Session->Snapshot.Outcome == ECatFishingOutcome::None
			&& !TActorIterator<ACatFishPickupActor>(World));
		TestTrue(TEXT("本场猫力确实超过旧倍数门槛"), 50.0 >= Session->Snapshot.FishStrength * 2.0);
		const double Expected = D0 * (bPerfect ? 0.9 : 1.0);
		const double Actual = Session->FightRunner->State.LineLengthCentimeters;
		const double Distance = FVector::Distance(Rod->GetRodTipWorldTransform().GetLocation(), Encounter->GetMouthWorldLocation());
		TestEqual(TEXT("实际入场线长完美乘0.9，非完美乘1"), Actual, Expected, 0.01);
		if (bPerfect)
			TestEqual(TEXT("完美时实际鱼嘴投影到缩短的线长"), Distance, Expected, 0.01);
		else
		{
			TestTrue(TEXT("非完美保持原鱼位置，允许既有松线"), Encounter->GetActorLocation().Equals(InitialFishPosition, 0.01));
			TestTrue(TEXT("实际鱼始终在合法线长内"), Distance <= Actual + 0.01);
		}
		AddInfo(FString::Printf(TEXT("Event=formal_perfect_line_verified Perfect=%d D0Cm=%.3f ExpectedCm=%.3f RunnerLineCm=%.3f ActorDistanceCm=%.3f"), bPerfect, D0, Expected, Actual, Distance));

		// 同一真实Session/Runner和同一端点几何，只切换身体输入方向。
		// 不推进身体、不预支移动，验证实际地形/操杆接收方没有再次投影掉主控力量。
		auto* Runner = Session->FightRunner.Get();
		auto* Body = F.Cat->GetPhysicalBodyComponent();
		const auto InitialState = Runner->State;
		Runner->OperatorState.bPullHeld = true;
		Runner->OperatorState.bSlackHeld = false;
		FCatFightRodConstraintInput Constraint;
		Constraint.RodTipWorldPosition = Rod->GetRodTipWorldTransform().GetLocation();
		Constraint.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
		Constraint.bRodHeld = true;
		const FVector Away = (InitialState.FishWorldPosition - Constraint.RodTipWorldPosition).GetSafeNormal2D();
		const FVector Side(-Away.Y, Away.X, 0.0);
		double ReferenceReelForce = -1.0, ReferenceReelDistance = -1.0;
		for (const FVector Intent : {FVector::ZeroVector, Away, -Away, Side, -Side, (Side - Away).GetSafeNormal()})
		{
			Body->SetMoveIntent(Intent);
			Runner->State = InitialState;
			Runner->State.FishEffortRatio = 0.1;
			if (!TestTrue(TEXT("真实Runner按身体方向刷新同一主控属性"), Runner->UpdateOperatorIntentAndProperties())) return false;
			auto Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, Away);
			FCatWaterSpatialResult WaterResult;
			bool bBeached = false;
			FVector GroundNormal;
			AActor* GroundActor = nullptr;
			FCatFishingRodResistanceResult Resistance;
			const auto Motion = Runner->ResolveFishSurfaceFromAuthority(Step, Constraint, WaterResult, bBeached, GroundNormal, GroundActor, Resistance);
			if (!TestTrue(TEXT("同一真实水域地形接收方完成线力和操杆结算"), Step.bSucceeded && Motion.bSucceeded && Resistance.bSucceeded)) return false;
			TestEqual(TEXT("前后左右输入均保留主控完整操杆力量"), Resistance.CatTorqueCapacityStrengthMeters, 50.0, 1e-9);
			if (ReferenceReelForce < 0.0)
			{
				ReferenceReelForce = Step.Trace.ReelForceLimitNewtons;
				ReferenceReelDistance = Step.ActualReelDistanceCentimeters;
				TestTrue(TEXT("测试包含可完成的主动收线"), ReferenceReelDistance > 0.0 && ReferenceReelForce > 0.0);
			}
			TestEqual(TEXT("身体方向不会在相同真实几何上改变卷线能力"), Step.Trace.ReelForceLimitNewtons, ReferenceReelForce, 1e-9);
			TestEqual(TEXT("身体方向不会凭空关闭本可完成的卷线"), Step.ActualReelDistanceCentimeters, ReferenceReelDistance, 1e-9);
		}
		Body->SetMoveIntent(FVector::ZeroVector);
		Runner->State = InitialState;
		Runner->HandleFixedStep();
		TestTrue(TEXT("正式会话完成包含鱼身转向的完整固定步"), Runner->IsRunning() && !Session->IsTerminal()
			&& Runner->PreviousFishExpectedSwimSpeedCentimetersPerSecond > 0.0);
		TestTrue(TEXT("固定步鱼身和鱼钩共用已提交的物理姿态"),
			Encounter->GetActorForwardVector().Equals(Runner->State.FishBody.Heading, 0.0001)
			&& Hook->GetActorLocation().Equals(Encounter->GetMouthWorldLocation(), 0.001));
		TestEqual(TEXT("Session提交的鱼线直距来自同一嘴点"), Hook->GetPresentationState().StraightLineDistanceCentimeters,
			FVector::Distance(Rod->GetRodTipWorldTransform().GetLocation(), Hook->GetActorLocation()), 0.001);
		// 重新接管走生产换主路径，保留同一鱼实体与 Runner。
		Session->FisherStableNetId.Reset();
		F.Cat->GetCatAbilitySystemComponent()->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 500.0f);
		TestTrue(TEXT("高力量重新接管继续同一物理会话"), Session->ResumePrimaryControlFromAuthority(F.Controller));
		TestTrue(TEXT("接管不会交鱼或重建Runner"), !Session->IsTerminal() && Session->FightRunner == Runner
			&& Runner->IsRunning() && Session->Snapshot.FishEncounterActor == Encounter
			&& !TActorIterator<ACatFishPickupActor>(World));
		Session->FightRunner->Stop();
		F.Equipment->ReleaseFishingUse(F.SessionId);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatScoopInventoryDeferredUseTest,
	"Catfishing.Unit.Fishing.Inventory.ScoopUsePreservesTargetAndCompletesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatScoopInventoryDeferredUseTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		const bool bExpectedSuccess = Scenario == 0;
		CatR3Tests::FFixture F;
		if (!F.Init(*this, true)) return false;
		F.AdmitController();
		F.Cat->GetCharacterMovement()->DisableMovement();
		auto* World = F.Wrapper.GetTestWorld();
		auto* Inventory = F.Cat->FindComponentByClass<UCatBackPackComponent>();
		if (!TestNotNull(TEXT("正式背包"), Inventory)) return false;
		if (!TestTrue(TEXT("正式抄网入库"), F.Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(),
			F.Equipment->GetSnapshot().Revision, 38).bCommitted)) return false;
		int32 Slot = INDEX_NONE;
		for (int32 I=0; I<Inventory->GetInventorySlotCount(); ++I)
			if (const auto* E=Inventory->GetInventoryEntryAtSlot(I); E && E->Instance && E->Instance->GetItemId()==38) { Slot=I; break; }
		if (!TestTrue(TEXT("抄网实例在正式格中"), Slot!=INDEX_NONE)) return false;
		const FGuid ItemId = Inventory->GetInventoryEntryAtSlot(Slot)->Instance->GetItemInstanceId();
		UCatFishDefinition* Definition = nullptr;
		for (const auto& Asset : GetDefault<UCatFishCatalogSettings>()->Definitions)
			if (auto* Candidate=Asset.LoadSynchronous(); Candidate && Candidate->IsRuntimeDefinitionReady()) { Definition=Candidate; break; }
		auto* Fish = World->SpawnActor<ACatFishPickupActor>(F.Cat->GetActorLocation()+FVector(100,0,0), FRotator::ZeroRotator);
		FCatCaptureConditionSnapshot Condition; Condition.RegionId=TEXT("ScoopUseTest");
		if (!TestTrue(TEXT("真实鱼实体初始化"), Fish && Definition && Fish->InitializeFromAuthority(F.SessionId,
			FGuid::NewGuid(), Definition, 1, 1, Condition, TEXT("R3Primary"), {}))) return false;
		FCatInventoryItemUseContext Context;
		Context.RequestId=FGuid::NewGuid(); Context.RequestingController=F.Controller; Context.UserPawn=F.Cat;
		Context.SourceInventory=Inventory; Context.InventorySlotIndex=Slot;
		Context.Target.bHasViewRay=true;
		Context.Target.ViewOrigin=F.Cat->GetPawnViewLocation();
		Context.Target.ViewDirection=(Fish->GetFishingCollisionCenter()-Context.Target.ViewOrigin).GetSafeNormal();
		Context.Target.Actor=Fish;
		F.Controller->SetControlRotation(Context.Target.ViewDirection.Rotation());
		int32 Completions=0; FCatDomainCommandResult Final;
		Context.OnCompleted=[&](const FCatDomainCommandResult& Result) { ++Completions; Final=Result; };
		const auto Begin=F.Controller->GetFishingCommandComponent()->ScoopFromInventoryUseOnAuthority(F.Controller, Context, ItemId);
		TestTrue(TEXT("排队不误报失败或成功"), Begin.bPending && !Begin.bCommitted && Begin.Error==ECatDomainCommandError::None);
		TestNull(TEXT("帧末之前不提前叼鱼"), F.Cat->GetMouthCarriedActor());
		const auto PendingReplay=F.Controller->GetFishingCommandComponent()->ScoopFromInventoryUseOnAuthority(F.Controller, Context, ItemId);
		TestTrue(TEXT("处理中重放不重复排队且不伪装终态"), PendingReplay.bPending && !PendingReplay.bTerminalReplay);
		auto Forged=Context; Forged.Target.Actor=nullptr;
		TestTrue(TEXT("内部重复提交不能替换已冻结的目标"), F.Controller->GetFishingCommandComponent()->ScoopFromInventoryUseOnAuthority(F.Controller, Forged, ItemId).bPending);
		if (Scenario == 1) F.Controller->GetFishingCommandComponent()->ResetTransientCommandState();
		if (Scenario == 2)
		{
			FCatInventoryEntry Removed;
			TestTrue(TEXT("帧末之前移走原抄网"), Inventory->RemoveInventoryEntryAtSlotFromAuthority(Slot, Removed));
		}
		F.Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("异步完成只回调一次"), Completions, 1);
		TestEqual(TEXT("成功、取消和失去抄网的事实正确"), Final.bCommitted, bExpectedSuccess);
		TestEqual(TEXT("原目标成为嘴叼鱼，取消或失去抄网不拾取"), F.Cat->GetMouthCarriedActor(), bExpectedSuccess ? static_cast<AActor*>(Fish) : nullptr);
		// 生命周期重置清空领域请求历史；旧 GA 此时已经取消，不应在新生命周期重新提交旧上下文。
		if (Scenario != 1)
		{
			const auto Replay=F.Controller->GetFishingCommandComponent()->ScoopFromInventoryUseOnAuthority(F.Controller, Context, ItemId);
			TestTrue(TEXT("领域终态读取不重复捕获"), !Replay.bPending && Replay.bCommitted == bExpectedSuccess);
		}
		F.Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("重放不重复完成"), Completions, 1);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingSessionScoopMouthCarryTest,
	"Catfishing.Unit.Fishing.Inventory.HookedFishUseCarriesAndSettlesExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingSessionScoopMouthCarryTest::RunTest(const FString& Parameters)
{
	CatR3Tests::FFixture F;
	if (!F.Init(*this,true)) return false;
	F.AdmitController();
	auto* World=F.Wrapper.GetTestWorld();
	F.Cat->GetCharacterMovement()->DisableMovement();
	F.Cat->SetActorLocation(FVector(0,0,40));
	FCatWaterGeometryBuildInput Geometry;
	Geometry.RegionId=TEXT("River"); Geometry.WaterPointVerticalToleranceCm=100; Geometry.BankHeightToleranceCm=100;
	Geometry.BoundaryToleranceCm=1; Geometry.MaxLandingCorrectionCm=100; Geometry.MinimumWaterInsetCm=1;
	auto& Boundary=Geometry.Boundaries.AddDefaulted_GetRef(); Boundary.BoundaryId=TEXT("ScoopShore");
	Boundary.Vertices={{50,-500},{1000,-500},{1000,500},{50,500}};
	const auto Built=FCatWaterGeometry::Build(Geometry);
	auto* Region=World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(),FTransform::Identity);
	if (!TestTrue(TEXT("真实岸线构建"),Built.bSucceeded && Region)) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region,Built.Cache); Region->FinishSpawning(FTransform::Identity);
	auto* Floor=World->SpawnActor<AStaticMeshActor>();
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube")));
	Floor->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,-20),FVector(10,10,0.2)));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	auto* Definition=GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(22);
	if (!TestNotNull(TEXT("正式鱼定义"),Definition)) return false;
	auto* Session=World->SpawnActor<ACatFishingSession>();
	auto* Encounter=World->SpawnActor<ACatFishEncounterActor>(FVector(120,0,0),FRotator::ZeroRotator);
	const FGuid CastId=FGuid::NewGuid();
	if (!TestTrue(TEXT("上钩鱼身份"),Encounter->InitializeAuthoritativeIdentity(F.SessionId,CastId,Definition->ItemId,120,1))) return false;
	Session->Snapshot.FishingSessionId=F.SessionId; Session->Snapshot.CastAttemptId=CastId;
	Session->Snapshot.Phase=ECatFishingPhase::HookedFight; Session->Snapshot.Revision=1;
	Session->Snapshot.FishEncounterActor=Encounter; Session->Snapshot.ItemId=Definition->ItemId;
	Session->Snapshot.FisherPlayerState=F.Player; Session->Snapshot.FishFightStaminaRemaining=100;
	Session->FishDefinition=Definition; Session->FishWeightKilograms=1; Session->FishVisualScale=1;
	Session->FisherCharacter=F.Cat; Session->CastEquipment=F.Equipment; Session->CatchFisherStableNetId=TEXT("R3Primary");
	Session->AttemptSnapshot.WaterRegion=Region->GetWaterRegionHandle();
	Session->AttemptSnapshot.RodItemInstanceId=F.Equipment->GetSnapshot().RodItemInstanceId;
	World->GetSubsystem<UCatFishingService>()->Sessions.Add(F.SessionId,Session);
	if (!TestTrue(TEXT("真咬已扣一份鱼饵"),F.Equipment->CommitFishingBaitDeferred(F.SessionId).bApplied)) return false;
	if (!TestTrue(TEXT("正式抄网入库"),F.Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(),F.Equipment->GetSnapshot().Revision,38).bCommitted)) return false;
	auto* Inventory=F.Cat->GetInventoryComponent();
	const int32 Slot=Inventory->FindFirstInventorySlotIndexByItemId(38);
	const FGuid Item=Inventory->GetInventoryEntryAtSlot(Slot)->Instance->GetItemInstanceId();
	FCatInventoryItemUseContext Context;
	Context.RequestId=FGuid::NewGuid(); Context.RequestingController=F.Controller; Context.UserPawn=F.Cat; Context.SourceInventory=Inventory; Context.InventorySlotIndex=Slot;
	Context.Target.bHasViewRay=true; Context.Target.Actor=Encounter; Context.Target.ViewOrigin=F.Cat->GetPawnViewLocation();
	Context.Target.ViewDirection=(Encounter->GetFishingCollisionCenter()-Context.Target.ViewOrigin).GetSafeNormal();
	F.Controller->SetControlRotation(Context.Target.ViewDirection.Rotation());
	// 从正式授予的来源 Spec 进入 GA；夹具只提供网络层已解码的目标，不越过能力的搏斗例外和资源校验。
	auto* ASC = F.Cat->GetCatAbilitySystemComponent();
	const auto* Spec = ASC->FindAbilitySpecFromClass(UCatGA_UseScoopNet::StaticClass());
	if (!TestNotNull(TEXT("库存入库已授予抄网能力"), Spec)) return false;
	FCatItemAbilityTargetData Target;
	Target.Inventory = Inventory; Target.ItemId = Item; Target.RequestId = Context.RequestId; Target.Aim = Context.Target;
	UGameplayAbility* Activated = nullptr;
	// 夹具模拟远端已经发送的预测键；不授予客户端 Spec，也不修改生产能力的网络执行策略。
	FPredictionKey ReceivedPredictionKey; ReceivedPredictionKey.Current = 1;
	if (!TestTrue(TEXT("搏斗中激活正式抄网能力"), ASC->InternalTryActivateAbility(Spec->Handle, ReceivedPredictionKey, &Activated))) return false;
	if (!TestNotNull(TEXT("真实抄网能力实例"), Activated)) return false;
	ASC->AbilityTargetDataSetDelegate(Spec->Handle, Activated->GetCurrentActivationInfo().GetActivationPredictionKey())
		.Broadcast(FGameplayAbilityTargetDataHandle(new FCatItemAbilityTargetData(Target)), FGameplayTag());
	F.Wrapper.TickTestWorld(0.01f);
	TestFalse(TEXT("抄鱼后能力结束"), Activated->IsActive());
	TestEqual(TEXT("会话捕获终态"),Session->GetSnapshot().Outcome,ECatFishingOutcome::Caught);
	TestNotNull(TEXT("满体力上钩鱼直接叼嘴"),ACatFishPickupActor::FindCarriedFish(F.Cat));
	TestEqual(TEXT("抄鱼不重复扣饵"),Inventory->CountVisibleInventoryQuantityByItemId(4),1);
	const auto Replay=F.Controller->GetFishingCommandComponent()->ScoopFromInventoryUseOnAuthority(F.Controller,Context,Item);
	TestTrue(TEXT("读取权威终态不重复结算"),Replay.bCommitted);
	return !HasAnyErrors();
}
#endif
