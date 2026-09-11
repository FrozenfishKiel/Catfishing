#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Inventory/CatInventoryComponent.h"

namespace CatFishingBaitRestockTests
{
	// 鱼饵补货回归夹具；它把角色正式背包、Equipment 读模型和营地公共仓库放在同一个 authority World 中，方便验证移动后选择与保存载荷是否跟随真实库存。
	struct FFixture
	{
		// 本用例独占的测试 World；Actor 生命周期和子系统都从这里启动，离开作用域后由测试框架清理。
		FTestWorldWrapper WorldWrapper;
		// 当前测试玩家的正式角色；随身 InventoryComponent 挂在它身上，是补饵目标事实源。
		ACatCharacter* Character = nullptr;
		// 当前角色的钓鱼选择读模型；测试只通过它观察选择是否跟随正式库存刷新。
		UCatEquipmentComponent* Equipment = nullptr;
		// 本用例生成的营地公共仓库；补货源物品先进入它的正式 InventoryComponent，再通过通用交换移动到玩家背包。
		ACatCampInventoryActor* Camp = nullptr;

		// 初始化补货场景：创建 authority World、角色和公共仓库，授予开局鱼竿、鱼漂与指定数量鱼饵，最后部署鱼竿让后续 Fishing 使用冻结能走完整链路。
		bool Initialize(FAutomationTestBase& Test, const int32 BaitQuantity = 1)
		{
			if (!Test.TestTrue(TEXT("创建鱼饵回归 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			if (!Test.TestTrue(TEXT("启动 Actor 生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			Character = World->SpawnActor<ACatCharacter>();
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

		// 开始一次 Fishing 使用冻结：读取当前 Equipment 选择和 Revision 作为输入，结果用于验证鱼饵扣减、重放和释放是否都回到同一会话。
		FCatFishingUseFreezeResult Begin(const FGuid SessionId)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			return Equipment->BeginFishingUse(SessionId, Loadout.RodItemInstanceId, Loadout.BaitItemInstanceId,
				Loadout.FloatItemInstanceId, Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
				Loadout.FloatDefinitionId, Loadout.Revision);
		}

		// 统计玩家正式背包中可见的红虫总数；测试用它确认公共仓库转移后库存事实没有丢数量或重复堆栈。
		int32 Quantity() const
		{
			const UCatInventoryComponent* PlayerInventory = Character ? Character->GetInventoryComponent() : nullptr;
			return PlayerInventory ? PlayerInventory->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")) : 0;
		}

		// 从公共仓库补回鱼饵：
		// 1. 先把鱼饵交付到营地正式库存，模拟商店或奖励写入共享仓库。
		// 2. 再用 InventoryComponent 的交换入口移动到玩家正式背包，覆盖右键自动目标和显式拖拽目标两条 UI 语义。
		// 3. 最后刷新 Equipment 读模型，验证选中鱼饵实例来自真实背包。
		bool Restock(FAutomationTestBase& Test, const bool bExplicitTargetSlot)
		{
			UCatInventoryComponent* CampInventory = Camp->GetInventoryComponent();
			UCatInventoryComponent* PlayerInventory = Character ? Character->GetInventoryComponent() : nullptr;
			if (!Test.TestTrue(TEXT("公共仓库和玩家正式库存存在"), CampInventory && PlayerInventory)) return false;
			if (!Test.TestTrue(TEXT("同种鱼饵交付公共仓库"),
				CampInventory->GrantInventoryDefinitionFromAuthority(
					FGuid::NewGuid(), TEXT("BugBait"), 3).bCommitted)) return false;
			const int32 SourceIndex = CampInventory->FindFirstInventorySlotIndexByDefinitionId(TEXT("BugBait"));
			const FCatInventoryEntry* SourceEntry = CampInventory->GetInventoryEntryAtSlot(SourceIndex);
			const TArray<FCatInventoryEntry> PlayerEntries = PlayerInventory->GetInventoryEntries();
			const int32 TargetIndex = PlayerEntries.IndexOfByPredicate(
				[](const FCatInventoryEntry& Entry) { return Entry.StackCount <= 0 || Entry.Instance == nullptr; });
			if (!Test.TestTrue(TEXT("存在仓库源格与背包空格"),
				SourceIndex != INDEX_NONE && SourceEntry && SourceEntry->Instance != nullptr
				&& TargetIndex != INDEX_NONE)) return false;
			const int32 DestinationIndex = bExplicitTargetSlot
				? TargetIndex : PlayerInventory->FindAvailableSlot(SourceEntry->Instance, SourceEntry->StackCount);
			if (!Test.TestTrue(TEXT("正式库存能解析补饵目标格"), DestinationIndex != INDEX_NONE)) return false;
			if (!Test.TestTrue(TEXT("通过正式库存交换补饵"),
				UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(
					CampInventory, SourceIndex, PlayerInventory, DestinationIndex))) return false;
			return Test.TestTrue(TEXT("补饵后刷新装备选择读模型"),
				Equipment->RefreshLoadoutFromInventoryComponentFromAuthority());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBaitRestockTest,
	"Catfishing.Unit.Equipment.BaitRestock.DepletedBaitCanCastAgainThroughGenericInventoryMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 缺饵补货回归流程：耗尽当前鱼饵后分别用右键目标和拖拽目标从公共仓库补货，验证正式库存交换完成后 Equipment 会恢复可用鱼饵实例。
bool FCatFishingBaitRestockTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBaitRestockTests;
	for (const bool bDrag : {false, true})
	{
		FFixture Fixture;
		if (!Fixture.Initialize(*this)) return false;
		const FGuid FirstSession = FGuid::NewGuid();
		if (!TestTrue(TEXT("最后一份饵可冻结"), Fixture.Begin(FirstSession).bBaitFrozen)) return false;
		TestEqual(TEXT("冻结阶段只扣一份"), Fixture.Quantity(), 0);
		TestFalse(TEXT("耗尽后没有悬空鱼饵实例"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid());
		if (!TestTrue(TEXT("上一场确认消耗鱼饵"), Fixture.Equipment->CommitFishingBaitDeferred(FirstSession).bApplied)) return false;
		if (!TestTrue(TEXT("终局释放钓具"), Fixture.Equipment->ReleaseFishingUse(FirstSession).bApplied)) return false;
		const int64 BeforeEmptyCast = Fixture.Equipment->GetSnapshot().Revision;
		TestFalse(TEXT("没有鱼饵不能继续冻结"), Fixture.Begin(FGuid::NewGuid()).bBaitFrozen);
		TestEqual(TEXT("缺饵拒绝不推进装备读模型版本"), Fixture.Equipment->GetSnapshot().Revision, BeforeEmptyCast);
		if (!Fixture.Restock(*this, bDrag)) return false;
		TestEqual(TEXT("补给数量没有损失或重复"), Fixture.Quantity(), 3);
		TestTrue(TEXT("补回同种饵后自动恢复有效实例"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid());
		const FGuid NextSession = FGuid::NewGuid();
		if (!TestTrue(TEXT("使用恢复后的当前选择再次通过抛钩装备裁决"), Fixture.Begin(NextSession).bBaitFrozen)) return false;
		TestEqual(TEXT("再次抛钩只冻结一份"), Fixture.Quantity(), 2);
		TestEqual(TEXT("同一会话回放原有冻结"), Fixture.Begin(NextSession).Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("重放不重复扣饵"), Fixture.Quantity(), 2);
		TestTrue(TEXT("取消抛钩返还鱼饵"), Fixture.Equipment->ReleaseFishingUse(NextSession).bApplied);
		TestEqual(TEXT("取消后恢复补给总数"), Fixture.Quantity(), 3);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBaitSelectionPreservedTest,
	"Catfishing.Unit.Equipment.BaitRestock.ValidSelectionAndLastBaitCancellationRemainUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 有效选择保留回归流程：取消最后一份饵后保持原实例，再补入同种堆栈，验证刷新只修正失效选择，不抢占仍有效的当前选择。
bool FCatFishingBaitSelectionPreservedTest::RunTest(const FString& Parameters)
{
	using namespace CatFishingBaitRestockTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FGuid SessionId = FGuid::NewGuid();
	if (!TestTrue(TEXT("冻结最后一份饵"), Fixture.Begin(SessionId).bBaitFrozen)) return false;
	TestTrue(TEXT("尚未咬钩时取消返饵"), Fixture.Equipment->ReleaseFishingUse(SessionId).bApplied);
	TestEqual(TEXT("最后一份饵原数返还"), Fixture.Quantity(), 1);
	const FGuid SelectedId = Fixture.Equipment->GetSnapshot().BaitItemInstanceId;
	TestTrue(TEXT("退饵后选择有效"), SelectedId.IsValid());
	if (!Fixture.Restock(*this, true)) return false;
	TestEqual(TEXT("新同种堆栈不抢已有选择"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId, SelectedId);
	TestEqual(TEXT("保留两份库存的总量"), Fixture.Quantity(), 4);
	const FGuid NextSession = FGuid::NewGuid();
	TestTrue(TEXT("取消后仍可再次抛钩冻结"), Fixture.Begin(NextSession).bBaitFrozen);
	TestTrue(TEXT("耗尽原栈后切到剩余同种堆栈"), Fixture.Equipment->GetSnapshot().BaitItemInstanceId.IsValid()
		&& Fixture.Equipment->GetSnapshot().BaitItemInstanceId != SelectedId);
	TestTrue(TEXT("清理回归会话"), Fixture.Equipment->ReleaseFishingUse(NextSession).bApplied);
	return !HasAnyErrors();
}

#endif
