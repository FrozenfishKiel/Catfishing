#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Fishing/CatFishingSession.h"
#include "Data/CatFishDefinition.h"
#include "TimerManager.h"
#include "Growth/CatGrowthComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingResolutionSubsystem.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Data/CatFishCatalogSettings.h"
#include "Social/CatRoomOwnerService.h"
#include "OnlineSubsystemTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"

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
		bool Init(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
			auto* World = Wrapper.GetTestWorld();
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
			for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
				if (!Test.TestTrue(TEXT("正式钓具入库"), Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			if (!Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 2).bCommitted) return false;
			if (!Equipment->Use(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Equipment->GetSnapshot().RodItemInstanceId).bCommitted) return false;
			const auto Loadout = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision).bBaitFrozen;
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
		if (!F.Init(*this)) return false;
		TestTrue(TEXT("另一款真实鱼饵入库"), F.Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), F.Equipment->GetSnapshot().Revision, TEXT("FruitBait"), 2).bCommitted);
		auto* Inventory = F.Cat->GetInventoryComponent();
		const auto* Bait = Inventory->GetInventoryEntryAtSlot(Inventory->FindFirstInventorySlotIndexByDefinitionId(TEXT("FruitBait")));
		if (!TestNotNull(TEXT("换饵实例存在"), Bait)) return false;
		const auto L = F.Equipment->GetSnapshot();
		TestTrue(TEXT("等待期真实换饵"), F.Equipment->ConfigureLoadoutFromAuthority(FGuid::NewGuid(), L.Revision,
			L.RodDefinitionId, TEXT("FruitBait"), L.FloatDefinitionId, L.ScoopNetDefinitionId, L.RodSkinDefinitionId,
			L.RodItemInstanceId, Bait->Instance->GetItemInstanceId(), L.FloatItemInstanceId, L.ScoopNetItemInstanceId).bCommitted);
		TestEqual(TEXT("抽魚入口读当前饵"), F.Equipment->GetCurrentFishingBaitDefinitionId(F.SessionId), FName(TEXT("FruitBait")));
		auto* World = F.Wrapper.GetTestWorld();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		auto* Fish = World->SpawnActor<ACatFishEncounterActor>();
		Session->Snapshot.FishingSessionId = F.SessionId;
		Session->Snapshot.Phase = ECatFishingPhase::Probe;
		Session->Snapshot.FishEncounterActor = Fish;
		Session->SelectionResolution = ECatFishSelectionResolution::Selected;
		// T10 夹具迁移墓碑（钓鱼规则 §3.4）：选鱼边界现在必须提供鱼种普通响应窗；原扣饵/距离断言全部保留。
		Session->FishDefinition = NewObject<UCatFishDefinition>();
		Session->FishDefinition->FishDefinitionId = TEXT("TestTimingFish");
		Session->FishDefinition->TrueBiteWindowSeconds = 12.0;

		Session->CastEquipment = F.Equipment;
		Session->FisherCharacter = F.Cat;
		Session->AttemptSnapshot.RodDefinitionId = L.RodDefinitionId;
		Session->AttemptSnapshot.RodItemInstanceId = L.RodItemInstanceId;
		Session->bStartupInProgress = true;
		F.Cat->SetActorLocation(FVector(300, 0, 0));
		Fish->SetActorLocation(FVector(bOverlong ? 10000 : 1000, 0, 0));
		const bool bOpened = Session->OpenTrueBiteWindowFromAuthority();
		TestEqual(TEXT("真咬超 Lmax 不进入合法窗口"), bOpened, !bOverlong);
		TestEqual(TEXT("退回抛竿时旧饵"), Inventory->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")), 2);
		TestEqual(TEXT("只扣真咬当前一份饵"), Inventory->CountVisibleInventoryQuantityByDefinitionId(TEXT("FruitBait")), 1);
		if (bOverlong) TestEqual(TEXT("超长鱼逃"), Session->GetSnapshot().Outcome, ECatFishingOutcome::Escaped);
		else
		{
			TestEqual(TEXT("实际普通响应计时器使用鱼种 12 秒"),
				double(World->GetTimerManager().GetTimerRemaining(Session->TrueBiteTimerHandle)), 12.0);
			TestEqual(TEXT("实际完美窗保持基础 1 秒"),
				Session->Snapshot.PerfectWindowEndsServerTime - Session->Snapshot.PhaseStartedServerTime, 1.0);
			TestEqual(TEXT("真咬 D0 使用移动后的鱼猫距离"), Session->TrueBiteDistanceCentimeters, 700.0);
			F.Cat->SetActorLocation(FVector(500, 0, 0));
			TestEqual(TEXT("响应窗移动不改冻结 D0"), Session->TrueBiteDistanceCentimeters, 700.0);
			F.Equipment->CommitFishingBaitDeferred(F.SessionId);
			TestEqual(TEXT("真咬重放不重复扣饵"), Inventory->CountVisibleInventoryQuantityByDefinitionId(TEXT("FruitBait")), 1);
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
	if (!Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), L.RodItemInstanceId, L.RodDefinitionId, NAME_None, F.Player, F.Player, true, false)) return false;
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
	TestTrue(TEXT("真实竿耐久可读取"), F.Equipment->GetFishingRodDurability(F.SessionId, Before, Broken));
	TestTrue(TEXT("收鱼生产扣费成功"), Session->CommitCatchEquipmentFromAuthority());
	TestTrue(TEXT("真实竿耐久回执可读取"), F.Equipment->GetFishingRodDurability(F.SessionId, After, Broken));
	TestTrue(TEXT("每条 -1 经成长后只扣 0.9，未回补竿"), FMath::IsNearlyEqual(Before - After, 0.9, 0.000001));
	return !HasAnyErrors();
}

#endif
