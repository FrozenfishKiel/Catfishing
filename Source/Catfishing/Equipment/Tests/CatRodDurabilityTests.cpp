#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"
#include "Equipment/Fragments/CatEquipmentFragment_Float.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace CatRodDurabilityTests
{
	// 耐久测试夹具流程：只替换本测试窗口内的正式库存目录和装备策略配置；所有实例、会话和磨损均经过正式公开入口。
	struct FFixture
	{
		/** 装备运行策略默认对象；本测试只写 Profile 信任策略，不向它注册物品定义。 */
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		/** 正式库存目录默认对象；本测试把临时装备定义注册到这里，验证路径与运行时一致。 */
		UCatInventorySettings* InventorySettings = GetMutableDefault<UCatInventorySettings>();
		/** 进入测试前的正式库存目录；析构时写回，防止本测试定义泄漏到后续用例。 */
		TArray<FCatInventoryCatalogDefinition> SavedInventoryDefinitions = InventorySettings->Definitions;
		/** 进入测试前的装配信任策略；本夹具只在测试窗口内启用服务器授予流程。 */
		ECatDomainPolicy SavedTrustPolicy = Settings->ProfileLoadoutTrustPolicy;
		/** 进入测试前的玩家背包容量；测试需要固定容量，结束后恢复项目默认对象。 */
		int32 SavedCapacity = InventorySettings->PlayerInventorySlotCapacity;
		/** 进入测试前的数量堆叠容量；测试需要固定堆叠上限，结束后恢复项目默认对象。 */
		int32 SavedStackCapacity = InventorySettings->DefaultQuantityStackCapacity;
		/** 本测试创建的装备定义保活集合；目录身份仍由 InventorySettings::Definitions 持有。 */
		TArray<TStrongObjectPtr<UCatEquipmentDefinition>> CreatedDefinitions;
		/** 本测试持有的 authority World；析构由 FTestWorldWrapper 负责关闭。 */
		FTestWorldWrapper WorldWrapper;
		/** 本测试生成的角色；用来读取真实 EquipmentComponent 和 InventoryComponent。 */
		ACatCharacter* Character = nullptr;
		/** 角色拥有的装备组件；测试通过它提交公开装备和 Fishing 使用命令。 */
		UCatEquipmentComponent* Equipment = nullptr;
		/** 测试鱼竿实例的稳定 ID；后续磨损、跨会话和换竿断言都对齐这同一件物品。 */
		FGuid RodId;

		// 夹具恢复流程：测试结束时恢复正式库存目录、装备策略和容量配置；测试创建的运行对象交给 WorldWrapper 清理。
		~FFixture()
		{
			InventorySettings->Definitions = SavedInventoryDefinitions;
			Settings->ProfileLoadoutTrustPolicy = SavedTrustPolicy;
			InventorySettings->PlayerInventorySlotCapacity = SavedCapacity;
			InventorySettings->DefaultQuantityStackCapacity = SavedStackCapacity;
		}

		// 测试定义注册流程：创建一条内存装备定义，写入正式库存目录映射，并返回对象给调用方补齐对应能力字段。
		UCatEquipmentDefinition* AddDefinition(const FName Id, const FName LoadoutSlotId = NAME_None)
		{
			UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
			CreatedDefinitions.Emplace(Definition);
			Definition->EquipmentDefinitionId = Id;
			Definition->FunctionalRouteId = Id;
			Definition->LoadoutSlotId = LoadoutSlotId;
			Definition->bEnableRuntimeDefinition = true;
			FCatInventoryCatalogDefinition& CatalogEntry = InventorySettings->Definitions.AddDefaulted_GetRef();
			CatalogEntry.DefinitionId = Id;
			CatalogEntry.ItemDefinition = TSoftObjectPtr<UCatInventoryItemDefinition>(Definition);
			return Definition;
		}

		// 夹具初始化流程：先替换正式库存目录和必要装备策略，再创建 authority World、角色、PlayerState 和真实组件，最后通过公开授予入口拿到可部署鱼竿。
		bool Initialize(FAutomationTestBase& Test)
		{
			InventorySettings->Definitions.Reset();
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
			InventorySettings->PlayerInventorySlotCapacity = 12;
			InventorySettings->DefaultQuantityStackCapacity = 20;
			UCatEquipmentDefinition* Rod =
				AddDefinition(TEXT("DurabilityTestRod"), UCatEquipmentDefinition::FishingRodLoadoutSlotId());
			UCatEquipmentFragment_Rod* RodFragment = NewObject<UCatEquipmentFragment_Rod>(Rod);
			Rod->Fragments.Add(RodFragment);
			RodFragment->MaximumRodDurability = 100.0;
			RodFragment->MaximumLineLengthCentimeters = 1500.0;
			RodFragment->HighTensionWearMultiplier = 1.0;
			Rod->UseActorClass = ACatFishingRodActor::StaticClass();
			UCatEquipmentDefinition* Bait =
				AddDefinition(TEXT("DurabilityTestBait"), UCatEquipmentDefinition::FishingBaitLoadoutSlotId());
			UCatEquipmentFragment_Bait* BaitFragment = NewObject<UCatEquipmentFragment_Bait>(Bait);
			Bait->Fragments.Add(BaitFragment);
			Bait->bRunConsumable = true;
			BaitFragment->BiteRateMultiplier = 1.0;
			BaitFragment->MinimumBiteDelayMultiplier = 1.0;
			UCatEquipmentDefinition* Float =
				AddDefinition(TEXT("DurabilityTestFloat"), UCatEquipmentDefinition::FishingFloatLoadoutSlotId());
			UCatEquipmentFragment_Float* FloatFragment = NewObject<UCatEquipmentFragment_Float>(Float);
			Float->Fragments.Add(FloatFragment);
			FloatFragment->MaximumCastDistanceCentimeters = 1000.0;
			for (const TStrongObjectPtr<UCatEquipmentDefinition>& Definition : CreatedDefinitions)
			{
				if (!Test.TestTrue(TEXT("test equipment definition is complete"), Definition->IsRuntimeDefinitionReady())) return false;
			}
			if (!Test.TestTrue(TEXT("creates authority equipment world"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			UWorld* World = WorldWrapper.GetTestWorld();
			Character = World->SpawnActor<ACatCharacter>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			if (!Test.TestTrue(TEXT("spawns character and trusted player state"), Character && PlayerState)) return false;
			Character->SetPlayerState(PlayerState);
			Equipment = Character->GetEquipmentComponent();
			if (!Test.TestNotNull(TEXT("character owns real equipment component"), Equipment)) return false;
			for (const FName Id : {FName(TEXT("DurabilityTestRod")), FName(TEXT("DurabilityTestFloat"))})
			{
				if (!Test.TestTrue(TEXT("grants real equipment instance"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("grants bait for several sessions"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("DurabilityTestBait"), 8).bCommitted)) return false;
			RodId = Equipment->GetSnapshot().RodItemInstanceId;
			return Test.TestTrue(TEXT("grants full durable rod with stable instance identity"),
				RodId.IsValid() && Equipment->GetSnapshot().RodDurability == 100.0);
		}

		bool Deploy(FAutomationTestBase& Test, const FGuid Id)
		{
			return Test.TestTrue(TEXT("deploys the existing inventory instance"), Equipment->Use(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted);
		}

		bool Begin(FAutomationTestBase& Test, const FGuid SessionId, const bool bCommitBait = true)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			if (!Test.TestTrue(TEXT("freezes fishing use of the deployed rod"), Equipment->BeginFishingUse(SessionId,
				Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
				Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId,
				Loadout.Revision).bBaitFrozen)) return false;
			return !bCommitBait || Test.TestTrue(TEXT("commits this session bait"),
				Equipment->CommitFishingBaitDeferred(SessionId).bApplied);
		}

		// 从正式背包按实例 ID 读取鱼竿运行格；耐久断言必须观察同一个库存实例，不能从 Equipment 读模型推导库存内容。
		bool FindRod(const FGuid Id, const UCatEquipmentInventoryItemInstance*& OutInstance) const
		{
			OutInstance = nullptr;
			const UCatInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
			const int32 SlotIndex = Inventory ? Inventory->FindInventorySlotIndexFromInstanceId(Id) : INDEX_NONE;
			const FCatInventoryEntry* Entry = Inventory ? Inventory->GetInventoryEntryAtSlot(SlotIndex) : nullptr;
			OutInstance = Entry != nullptr && Entry->StackCount > 0
				? Cast<UCatEquipmentInventoryItemInstance>(Entry->Instance) : nullptr;
			return OutInstance != nullptr;
		}

	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodCumulativeWearTest,
	"Catfishing.Unit.Equipment.RodDurability.CumulativeWearIsImmediateAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodCumulativeWearTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid SessionId = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId)
		|| !Fixture.Begin(*this, SessionId, false)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_wear_rejected"), EAutomationExpectedErrorFlags::Contains, 10);
	TestEqual(TEXT("wear cannot precede bait commitment"), Equipment->ApplyFishingRodWear(SessionId, 1, 12.5).Error,
		ECatDomainCommandError::InvalidPhase);
	if (!TestTrue(TEXT("commits bait before active fight"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
	const FCatFishingUseOperationResult First = Equipment->ApplyFishingRodWear(SessionId, 1, 12.5);
	TestTrue(TEXT("first cumulative sample applies immediately"), First.bApplied);
	TestEqual(TEXT("first result reads instance durability"), First.RemainingRodDurability, 87.5);
	TestEqual(TEXT("public snapshot changes before session close"), Equipment->GetSnapshot().RodDurability, 87.5);
	const int64 FirstRevision = Equipment->GetSnapshot().Revision;
	const FCatFishingUseOperationResult Replay = Equipment->ApplyFishingRodWear(SessionId, 1, 12.5);
	TestEqual(TEXT("same sequence and total is a replay"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("replay does not apply twice"), Replay.bApplied);
	TestEqual(TEXT("replay does not publish a new revision"), Equipment->GetSnapshot().Revision, FirstRevision);
	TestEqual(TEXT("changed payload cannot reuse a sequence"), Equipment->ApplyFishingRodWear(SessionId, 1, 15.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("missing intermediate sequence is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3, 20.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestTrue(TEXT("next cumulative sample applies"), Equipment->ApplyFishingRodWear(SessionId, 2, 20.0).bApplied);
	TestEqual(TEXT("second sample deducts only its delta"), Equipment->GetSnapshot().RodDurability, 80.0);
	const int64 AcceptedRevision = Equipment->GetSnapshot().Revision;
	TestEqual(TEXT("out of order older sample is rejected"), Equipment->ApplyFishingRodWear(SessionId, 1, 12.5).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("cumulative total cannot decrease"), Equipment->ApplyFishingRodWear(SessionId, 3, 19.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("negative total is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3, -1.0).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("non finite total is rejected"), Equipment->ApplyFishingRodWear(SessionId, 3,
		std::numeric_limits<double>::quiet_NaN()).Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("unknown session has no write authority"), Equipment->ApplyFishingRodWear(FGuid::NewGuid(), 1, 5.0).Error,
		ECatDomainCommandError::NotFound);
	Fixture.Character->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("client cannot mutate a valid server session"), Equipment->ApplyFishingRodWear(SessionId, 3, 25.0).Error,
		ECatDomainCommandError::DependencyUnavailable);
	Fixture.Character->SetRole(ROLE_Authority);
	TestEqual(TEXT("all rejected writes preserve durability"), Equipment->GetSnapshot().RodDurability, 80.0);
	TestEqual(TEXT("all rejected writes preserve revision"), Equipment->GetSnapshot().Revision, AcceptedRevision);
	double Durability = -1.0;
	bool bBroken = true;
	TestTrue(TEXT("session reads its bound deployed instance"), Equipment->GetFishingRodDurability(SessionId, Durability, bBroken));
	TestEqual(TEXT("bound instance sees cumulative wear"), Durability, 80.0);
	TestFalse(TEXT("positive durability remains usable"), bBroken);
	TestTrue(TEXT("release closes session without another wear deduction"), Equipment->ReleaseFishingUse(SessionId).bApplied);
	TestEqual(TEXT("late released session wear cannot change the rod"), Equipment->ApplyFishingRodWear(SessionId, 3, 25.0).Error,
		ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("release preserves the already deducted durability"), Equipment->GetSnapshot().RodDurability, 80.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodDurabilityAcrossSessionsTest,
	"Catfishing.Unit.Equipment.RodDurability.PersistsAcrossSessionsAndRedeployment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodDurabilityAcrossSessionsTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid FirstSession = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, FirstSession)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	if (!TestTrue(TEXT("first fight wears the rod"), Equipment->ApplyFishingRodWear(FirstSession, 1, 35.0).bApplied)) return false;
	TestTrue(TEXT("first fight closes"), Equipment->ReleaseFishingUse(FirstSession).bApplied);
	const FCatInventoryItemUseResult Recalled = Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId);
	if (!TestTrue(TEXT("recalls worn instance"), Recalled.bCommitted)) return false;
	const UCatEquipmentInventoryItemInstance* RecalledUseInstance = Recalled.Item.StackCount > 0
		? Cast<UCatEquipmentInventoryItemInstance>(Recalled.Item.Instance) : nullptr;
	if (!TestTrue(TEXT("recall returns the original equipment instance"), RecalledUseInstance != nullptr)) return false;
	TestEqual(TEXT("recall retains the original instance"), RecalledUseInstance->GetItemInstanceId(), Fixture.RodId);
	TestEqual(TEXT("recall returns worn durability"), RecalledUseInstance->GetRodDurability(), 65.0);
	const UCatEquipmentInventoryItemInstance* RecalledRod = nullptr;
	if (!TestTrue(TEXT("same rod reappears in inventory"), Fixture.FindRod(Fixture.RodId, RecalledRod))) return false;
	TestEqual(TEXT("inventory is not restored to definition maximum"), RecalledRod->GetRodDurability(), 65.0);
	if (!Fixture.Deploy(*this, Fixture.RodId)) return false;
	const FGuid SecondSession = FGuid::NewGuid();
	if (!Fixture.Begin(*this, SecondSession)) return false;
	double Durability = 0.0;
	bool bBroken = true;
	TestTrue(TEXT("next session binds the original instance"), Equipment->GetFishingRodDurability(SecondSession, Durability, bBroken));
	TestEqual(TEXT("next fight starts with remaining durability"), Durability, 65.0);
	TestTrue(TEXT("new session has its own cumulative wear counter"), Equipment->ApplyFishingRodWear(SecondSession, 1, 10.0).bApplied);
	TestEqual(TEXT("both sessions accumulate on one rod"), Equipment->GetSnapshot().RodDurability, 55.0);
	TestTrue(TEXT("second fight closes"), Equipment->ReleaseFishingUse(SecondSession).bApplied);
	const FCatInventoryItemUseResult RecalledAgain = Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId);
	TestTrue(TEXT("second recall succeeds"), RecalledAgain.bCommitted);
	const UCatEquipmentInventoryItemInstance* RecalledAgainInstance = RecalledAgain.Item.StackCount > 0
		? Cast<UCatEquipmentInventoryItemInstance>(RecalledAgain.Item.Instance) : nullptr;
	if (!TestTrue(TEXT("second recall returns an equipment instance"), RecalledAgainInstance != nullptr)) return false;
	TestEqual(TEXT("second recall preserves total lifetime wear"), RecalledAgainInstance->GetRodDurability(), 55.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatPurchasedRodIndependenceTest,
	"Catfishing.Unit.Equipment.RodDurability.NewPurchaseAndOldSessionRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatPurchasedRodIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid OldSession = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId) || !Fixture.Begin(*this, OldSession)) return false;
	UCatEquipmentComponent* Equipment = Fixture.Equipment;
	if (!TestTrue(TEXT("previous rod receives wear"), Equipment->ApplyFishingRodWear(OldSession, 1, 30.0).bApplied)) return false;
	TestTrue(TEXT("previous session releases"), Equipment->ReleaseFishingUse(OldSession).bApplied);
	if (!TestTrue(TEXT("recalled rod returns to inventory"), Equipment->UnUse(FGuid::NewGuid(), Fixture.RodId).bCommitted)) return false;
	if (!TestTrue(TEXT("shop grant creates another rod instance"), Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("DurabilityTestRod")).bCommitted)) return false;
	FGuid NewRodId;
	int32 RodCount = 0;
	const UCatInventoryComponent* Inventory = Fixture.Character ? Fixture.Character->GetInventoryComponent() : nullptr;
	if (!TestNotNull(TEXT("inventory stores both rod instances"), Inventory)) return false;
	for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
	{
		const UCatEquipmentInventoryItemInstance* RodInstance = Entry.StackCount > 0
			? Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance) : nullptr;
		if (RodInstance == nullptr)
		{
			continue;
		}
		if (RodInstance->GetItemDefinitionId() != FName(TEXT("DurabilityTestRod"))) continue;
		++RodCount;
		if (RodInstance->GetItemInstanceId() != Fixture.RodId)
		{
			NewRodId = RodInstance->GetItemInstanceId();
			TestEqual(TEXT("only new instance starts full"), RodInstance->GetRodDurability(), 100.0);
		}
	}
	if (!TestEqual(TEXT("both purchased instances coexist"), RodCount, 2)
		|| !TestTrue(TEXT("new purchase has a separate identity"), NewRodId.IsValid())) return false;
	const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
	if (!TestTrue(TEXT("selects new rod by its exact instance"), Equipment->ConfigureLoadoutFromAuthority(
		FGuid::NewGuid(), Loadout.Revision, Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
		Loadout.FloatDefinitionId, NAME_None, NAME_None, NewRodId, Loadout.BaitItemInstanceId,
		Loadout.FloatItemInstanceId, FGuid()).bCommitted)) return false;
	double OldDurability = 0.0;
	bool bOldBroken = true;
	TestTrue(TEXT("previous session still reads its bound previous rod"), Equipment->GetFishingRodDurability(OldSession, OldDurability, bOldBroken));
	TestEqual(TEXT("changing selection cannot redirect the session lookup"), OldDurability, 70.0);
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_wear_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCatFishingUseOperationResult Late = Equipment->ApplyFishingRodWear(OldSession, 2, 60.0);
	TestEqual(TEXT("late wear from previous session is terminal"), Late.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("late result reports the previous bound rod"), Late.RemainingRodDurability, 70.0);
	TestEqual(TEXT("late previous wear cannot damage newly selected rod"), Equipment->GetSnapshot().RodDurability, 100.0);
	const UCatEquipmentInventoryItemInstance* OldRodInstance = nullptr;
	if (!TestTrue(TEXT("worn rod remains separately stored"), Fixture.FindRod(Fixture.RodId, OldRodInstance))) return false;
	TestEqual(TEXT("buying another rod leaves previous wear unchanged"), OldRodInstance->GetRodDurability(), 70.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodSessionDurabilityTest,
	"Catfishing.Unit.Equipment.RodDurability.FightWritesSameInstanceAndBreakReleasesOperators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodSessionDurabilityTest::RunTest(const FString& Parameters)
{
	using namespace CatRodDurabilityTests;
	FFixture Fixture;
	const FGuid FirstId = FGuid::NewGuid();
	if (!Fixture.Initialize(*this) || !Fixture.Deploy(*this, Fixture.RodId)
		|| !Fixture.Begin(*this, FirstId)) return false;
	UWorld* World = Fixture.WorldWrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	APlayerState* Owner = Fixture.Character->GetPlayerState();
	if (!TestTrue(TEXT("initializes the deployed rod projection"), Rod && Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), Fixture.RodId, TEXT("DurabilityTestRod"), TEXT("TestSkin"), Owner, Owner, true, false))) return false;
	const auto MakeSession = [&](const FGuid Id)
	{
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		Session->Snapshot.FishingSessionId = Id;
		Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
		Session->Snapshot.RodActor = Rod;
		Session->AttemptSnapshot.RodItemInstanceId = Fixture.RodId;
		Session->CastEquipment = Fixture.Equipment;
		Session->FightRunner = NewObject<UCatFishingFightRunner>(Session);
		return Session;
	};
	ACatFishingSession* First = MakeSession(FirstId);
	FCatFightStepResult Step;
	Step.RodWearDelta = 25.0;
	Step.AbsoluteRodWear = 25.0;
	Step.bStrongConfrontation = true;
	Step.NormalizedLineLoad = 1.0;
	Step.NormalizedTension = 1.0;
	Step.OperatorCatStrength = 50.0;
	First->HandleFightRunnerStepFromAuthority(Step, 80.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("strong confrontation keeps the authority session fighting"), First->GetSnapshot().Phase, ECatFishingPhase::HookedFight);
	TestEqual(TEXT("strong confrontation does not publish a terminal outcome"), First->GetSnapshot().Outcome, ECatFishingOutcome::None);
	TestTrue(TEXT("strong confrontation remains visible in the public snapshot"), First->GetSnapshot().bStrongConfrontation);
	TestEqual(TEXT("fight snapshot mirrors immediately written instance durability"), First->GetSnapshot().RodDurabilityRemaining, 75.0);
	TestEqual(TEXT("fight wear is already in equipment before termination"), Fixture.Equipment->GetSnapshot().RodDurability, 75.0);
	AddExpectedErrorPlain(TEXT("Event=fishing_session_terminated"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedErrorPlain(TEXT("Event=fishing_rod_broken"), EAutomationExpectedErrorFlags::Contains, 1);
	First->FinalizeSession(ECatFishingPhase::Terminated, ECatFishingOutcome::Cancelled, TEXT("durability test cancel"));
	First->HandleFightRunnerStepFromAuthority(Step, 80.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("cancel and late step neither refund nor duplicate wear"), Fixture.Equipment->GetSnapshot().RodDurability, 75.0);
	const FGuid SecondId = FGuid::NewGuid();
	if (!Fixture.Begin(*this, SecondId)) return false;
	ACatFishingSession* Second = MakeSession(SecondId);
	Step.RodWearDelta = 75.0;
	Step.AbsoluteRodWear = 75.0;
	Step.Outcome = ECatFightStepOutcome::RodBroken;
	Second->HandleFightRunnerStepFromAuthority(Step, 0.0, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("depletion is a rod-broken terminal"), Second->GetSnapshot().Outcome, ECatFishingOutcome::RodBroken);
	TestEqual(TEXT("terminal mirror stays at zero"), Second->GetSnapshot().RodDurabilityRemaining, 0.0);
	TestTrue(TEXT("the real item is broken"), Fixture.Equipment->GetSnapshot().bRodBroken);
	TestTrue(TEXT("the deployed rod replicates broken state"), Rod->GetPresentationState().bBroken);
	TestEqual(TEXT("broken rod releases every operator"), Rod->GetOperatorCount(), 0);
	TestFalse(TEXT("broken session no longer blocks inventory recall"), Fixture.Equipment->HasActiveFishingUse());
	return !HasAnyErrors();
}

#endif
