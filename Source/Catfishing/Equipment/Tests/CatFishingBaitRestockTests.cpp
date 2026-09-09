#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Framework/Game/CatGameplayTypes.h"

namespace CatFishingBaitRestockTests
{
	struct FFixture
	{
		FTestWorldWrapper WorldWrapper;
		UCatEquipmentComponent* Equipment = nullptr;
		ACatCampInventoryActor* Camp = nullptr;

		bool Initialize(FAutomationTestBase& Test, const int32 BaitQuantity = 1)
		{
			if (!Test.TestTrue(TEXT("创建鱼饵回归 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			if (!Test.TestTrue(TEXT("启动 Actor 生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			Camp = World->SpawnActor<ACatCampInventoryActor>();
			if (!Test.TestTrue(TEXT("创建角色、玩家状态和公共仓库"), Character && PlayerState && Camp)) return false;
			Character->SetPlayerState(PlayerState);
			Equipment = Character->GetEquipmentComponent();
			for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
			{
				if (!Test.TestTrue(TEXT("授予正式钓具"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("授予正式红虫饵"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), BaitQuantity).bCommitted)) return false;
			return Test.TestTrue(TEXT("部署鱼竿实例"), Equipment->Use(FGuid::NewGuid(),
				Equipment->GetSnapshot().Revision, Equipment->GetSnapshot().RodItemInstanceId).bCommitted);
		}

		FCatFishingUseReservationResult Begin(const FGuid SessionId)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId,
				Loadout.FloatItemInstanceId, Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
				Loadout.FloatDefinitionId, Loadout.Revision);
		}

		int32 Quantity() const
		{
			int32 Total = 0;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			{
				if (Slot.DefinitionId == FName(TEXT("BugBait"))) Total += Slot.Quantity;
			}
			return Total;
		}

		bool Restock(FAutomationTestBase& Test, const bool bDrag)
		{
			if (!Test.TestTrue(TEXT("同种鱼饵交付公共仓库"), Camp->AddItemFromAuthority(
				FGuid::NewGuid(), Camp->GetSnapshot().Revision, TEXT("BugBait"), 3).bCommitted)) return false;
			const int32 SourceIndex = Camp->GetSnapshot().InventorySlots.IndexOfByPredicate(
				[](const FCatRunInventorySlot& Slot) { return Slot.DefinitionId == FName(TEXT("BugBait")); });
			const int32 TargetIndex = Equipment->GetSnapshot().InventorySlots.IndexOfByPredicate(
				[](const FCatRunInventorySlot& Slot) { return Slot.Quantity <= 0; });
			if (!Test.TestTrue(TEXT("存在仓库源格与背包空格"), SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE)) return false;
			const FCatDomainCommandResult Result = bDrag
				? Camp->WithdrawToEquipmentSlotFromAuthority(FGuid::NewGuid(), Camp->GetSnapshot().Revision,
					SourceIndex, Equipment, Equipment->GetSnapshot().Revision, TargetIndex)
				: Camp->WithdrawToEquipmentFromAuthority(FGuid::NewGuid(), Camp->GetSnapshot().Revision,
					SourceIndex, 3, Equipment, Equipment->GetSnapshot().Revision);
			return Test.TestTrue(TEXT("通过正式仓库取用或拖拽补饵"), Result.bCommitted);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBaitRestockTest,
	"Catfishing.Unit.Equipment.BaitRestock.DepletedBaitCanCastAgainThroughBothCampRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBaitRestockTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBaitRestockTests;
	AddExpectedErrorPlain(TEXT("Event=equipment_rod_session_rejected"), EAutomationExpectedErrorFlags::Contains, 2);
	for (const bool bDrag : {false, true})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		const FGuid FirstSession = FGuid::NewGuid();
		if (!TestTrue(TEXT("最后一份饵可预留"), Fixture.Begin(FirstSession).bReserved)) return false;
		TestEqual(TEXT("预留阶段只扣一份"), Fixture.Quantity(), 0);
		TestFalse(TEXT("耗尽后没有悬空鱼饵实例"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid());
		if (!TestTrue(TEXT("上一场确认消耗鱼饵"), Fixture.Equipment->CommitFishingBaitDeferred(FirstSession).bApplied)) return false;
		if (!TestTrue(TEXT("终局释放钓具"), Fixture.Equipment->ReleaseFishingUse(FirstSession).bApplied)) return false;
		const int64 BeforeEmptyCast = Fixture.Equipment->GetSnapshot().Revision;
		TestFalse(TEXT("没有鱼饵不能继续预留"), Fixture.Begin(FGuid::NewGuid()).bReserved);
		TestEqual(TEXT("缺饵拒绝不推进库存版本"), Fixture.Equipment->GetSnapshot().Revision, BeforeEmptyCast);
		if (!Fixture.Restock(*this, bDrag)) return false;
		TestEqual(TEXT("补给数量没有损失或重复"), Fixture.Quantity(), 3);
		TestTrue(TEXT("补回同种饵后自动恢复有效实例"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid());
		const FGuid NextSession = FGuid::NewGuid();
		if (!TestTrue(TEXT("使用恢复后的当前选择再次通过抛钩装备裁决"), Fixture.Begin(NextSession).bReserved)) return false;
		TestEqual(TEXT("再次抛钩只预留一份"), Fixture.Quantity(), 2);
		TestEqual(TEXT("同一会话回放原有预留"), Fixture.Begin(NextSession).Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("重放不重复扣饵"), Fixture.Quantity(), 2);
		TestTrue(TEXT("取消抛钩返还鱼饵"), Fixture.Equipment->ReleaseFishingUse(NextSession).bApplied);
		TestEqual(TEXT("取消后恢复补给总数"), Fixture.Quantity(), 3);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBaitSelectionPreservedTest,
	"Catfishing.Unit.Equipment.BaitRestock.ValidSelectionAndLastBaitCancellationRemainUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBaitSelectionPreservedTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBaitRestockTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("预留最后一份饵"), Fixture.Begin(SessionId).bReserved)) return false;
	TestTrue(TEXT("尚未咬钩时取消返饵"), Fixture.Equipment->ReleaseFishingUse(SessionId).bApplied);
	TestEqual(TEXT("最后一份饵原数返还"), Fixture.Quantity(), 1);
	const FGuid SelectedId = Fixture.Equipment->GetSnapshot().BaitItemInstanceId;
	TestTrue(TEXT("退饵后选择有效"), SelectedId.IsValid());
	if (!Fixture.Restock(*this, true)) return false;
	TestEqual(TEXT("新同种堆栈不抢已有选择"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId, SelectedId);
	TestEqual(TEXT("保留两份库存的总量"), Fixture.Quantity(), 4);
	const FGuid NextSession = FGuid::NewGuid();
	TestTrue(TEXT("取消后仍可再次抛钩预留"), Fixture.Begin(NextSession).bReserved);
	TestTrue(TEXT("耗尽原栈后切到剩余同种堆栈"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid()
		&& Fixture.Equipment->GetSnapshot().BaitItemInstanceId != SelectedId);
	TestTrue(TEXT("清理回归会话"), Fixture.Equipment->ReleaseFishingUse(NextSession).bApplied);
	return !HasAnyErrors();
}

#endif
