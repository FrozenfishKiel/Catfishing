#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "OnlineSubsystemTypes.h"

namespace CatRodReplacementTests
{
	// 使用正式 T1/T2 资产和公共仓库入口，不改资产数值或构造第二份装备选择状态。
	struct FFixture
	{
		// 本用例独占的 authority World；Controller、角色、公共仓库和 FishingService 都在这里共同运行。
		FTestWorldWrapper WorldWrapper;
		// 测试玩家的控制器；它提供 PlayerState 身份并驱动部署、收杆和移动后的选择验证。
		ACatfishingPlayerController* Controller = nullptr;
		// 当前被控制的角色；正式随身 InventoryComponent 挂在它身上，是 T2 从公共仓库移入背包的目标。
		ACatCharacter* Character = nullptr;
		// 当前角色的 Equipment 读模型；测试用它观察鱼竿选择是否在正式库存变化后正确切换或保持。
		UCatEquipmentComponent* Equipment = nullptr;
		// 本用例生成的营地公共仓库；T2 先交付到它的正式库存，再通过通用库存交换进入玩家背包。
		ACatCampInventoryActor* Camp = nullptr;
		// 已部署到世界中的原 T1 鱼竿 Actor；它承载 broken 状态，收杆后应回到玩家正式库存。
		ACatFishingRodActor* OldRod = nullptr;
		// 原 T1 的稳定物品实例 ID；断竿、收杆和保存验证都用它确认没有误换到同定义其他实例。
		FGuid OldItemId;
		// 新 T2 的稳定物品实例 ID；移动出公共仓库后当前选择必须指向这同一件实例。
		FGuid NewItemId;

		// 初始化换竿场景：创建玩家身份、角色和公共仓库，授予基础钓具与鱼饵，并记录开局 T1 实例作为后续断竿对象。
		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("创建真实装备 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			if (!Test.TestTrue(TEXT("启动 Actor 生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			Character = World->SpawnActor<ACatCharacter>();
			Camp = World->SpawnActor<ACatCampInventoryActor>();
			if (!Test.TestTrue(TEXT("创建玩家与公共仓库"), Controller && PlayerState && Character && Camp)) return false;
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("RodReplacementOwner"), FName(TEXT("CAT_TEST")));
			PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			Controller->PlayerState = PlayerState;
			Character->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			Equipment = Character->GetEquipmentComponent();
			for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
			{
				if (!Test.TestTrue(TEXT("授予正式钓具"), Equipment->GrantEquipmentFromAuthority(
					FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted)) return false;
			}
			if (!Test.TestTrue(TEXT("授予正式鱼饵"), Equipment->GrantInventoryQuantityFromAuthority(
				FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 2).bCommitted)) return false;
			OldItemId = Equipment->GetSnapshot().RodItemInstanceId;
			return Test.TestTrue(TEXT("T1 自动选中"), OldItemId.IsValid());
		}

		// 制造一根已部署的断 T1：先把当前 T1 放到世界，再通过 Fishing 使用记录提交磨损，最后同步世界鱼竿 broken 表现。
		bool BreakDeployedRod(FAutomationTestBase& Test)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			if (!Test.TestTrue(TEXT("部署原 T1 实例"), Equipment->Use(
				FGuid::NewGuid(), Loadout.Revision, OldItemId).bCommitted)) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			OldRod = World->SpawnActor<ACatFishingRodActor>();
			if (!Test.TestNotNull(TEXT("创建 T1 世界鱼竿"), OldRod)) return false;
			OldRod->SetInstigator(Character); // 和正式 PlaceRod 一样绑定原物品存储来源。
			if (!Test.TestTrue(TEXT("绑定 T1 世界身份"), OldRod->InitializeAuthoritativeIdentity(
				FGuid::NewGuid(), OldItemId, Loadout.RodDefinitionId, NAME_None, Controller->PlayerState, nullptr, true, false))) return false;
			OldRod->SetActorLocation(Controller->GetPawn()->GetActorLocation() + FVector(80, 0, 0));
			if (!Test.TestTrue(TEXT("登记原鱼竿"), World->GetSubsystem<UCatFishingService>()->RegisterDeployedRod(
				Controller->PlayerState, OldRod))) return false;
			const FGuid SessionId = FGuid::NewGuid();
			if (!Test.TestTrue(TEXT("绑定原鱼竿钓鱼会话"), Equipment->BeginFishingUse(SessionId,
				OldItemId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId, Loadout.RodDefinitionId,
				Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Equipment->GetSnapshot().Revision).bBaitFrozen)) return false;
			if (!Test.TestTrue(TEXT("提交鱼饵"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
			if (!Test.TestTrue(TEXT("耗尽 T1 实例耐久"), Equipment->ApplyFishingRodWear(
				SessionId, 1, Loadout.RodDurability + 1).bRodBroken)) return false;
			if (!Test.TestTrue(TEXT("同步世界断竿事实"), OldRod->SetBrokenFromAuthority(true,
				OldRod->GetPresentationState().RodActorRevision))) return false;
			return Test.TestTrue(TEXT("结束原会话"), Equipment->ReleaseFishingUse(SessionId).bApplied);
		}

		// 收回原鱼竿：通过 FishingService 的正式收杆链把世界 Actor 持有的实例归还到玩家正式库存，并释放部署名额。
		bool Pack(FAutomationTestBase& Test)
		{
			FCatPackRodCommand Command;
			Command.Context.RequestId = FGuid::NewGuid();
			Command.Context.RodActorId = OldRod->GetPresentationState().RodActorId;
			Command.Context.ExpectedRodActorRevision = OldRod->GetPresentationState().RodActorRevision;
			UCatFishingService* Fishing = WorldWrapper.GetTestWorld()->GetSubsystem<UCatFishingService>();
			if (!Test.TestTrue(TEXT("正式收杆链归还 T1"), Fishing->PackRod(Controller, Command).bCommitted)) return false;
			return Test.TestNull(TEXT("收回后释放放竿名额"), Fishing->FindDeployedRod(Controller->PlayerState));
		}

		// 接收 T2 流程：
		// 1. 先把 T2 放入公共仓库正式库存，记录它的真实实例 ID。
		// 2. 再用同一个 InventoryComponent 交换入口覆盖右键自动入包和显式拖拽入包。
		// 3. 移动成功后刷新 Equipment 读模型，验证断竿场景只切到已进入玩家背包的 T2。
		bool ReceiveT2(FAutomationTestBase& Test, const bool bDrag)
		{
			UCatInventoryComponent* CampInventory = Camp->GetInventoryComponent();
			UCatInventoryComponent* PlayerInventory = Character ? Character->GetInventoryComponent() : nullptr;
			if (!Test.TestTrue(TEXT("公共仓库和玩家正式库存存在"), CampInventory && PlayerInventory)) return false;
			if (!Test.TestTrue(TEXT("商店交付 T2 到公共仓库"),
				CampInventory->GrantInventoryDefinitionFromAuthority(
					FGuid::NewGuid(), TEXT("ShopRodT2"), 1).bCommitted)) return false;
			const int32 CampIndex = CampInventory->FindFirstInventorySlotIndexByDefinitionId(TEXT("ShopRodT2"));
			const FCatInventoryEntry* SourceEntry = CampInventory->GetInventoryEntryAtSlot(CampIndex);
			if (!Test.TestTrue(TEXT("仓库存在真实 T2"),
				CampIndex != INDEX_NONE && SourceEntry && SourceEntry->Instance != nullptr)) return false;
			NewItemId = SourceEntry->Instance->GetItemInstanceId();
			const TArray<FCatInventoryEntry> PlayerEntries = PlayerInventory->GetInventoryEntries();
			const int32 TargetIndex = PlayerEntries.IndexOfByPredicate(
				[](const FCatInventoryEntry& Entry) { return Entry.StackCount <= 0 || Entry.Instance == nullptr; });
			if (!Test.TestTrue(TEXT("玩家背包有接收 T2 的格子"), TargetIndex != INDEX_NONE)) return false;
			const int32 DestinationIndex = bDrag ? TargetIndex
				: PlayerInventory->FindAvailableSlot(SourceEntry->Instance, SourceEntry->StackCount);
			if (!Test.TestTrue(TEXT("正式库存能解析 T2 目标格"), DestinationIndex != INDEX_NONE)) return false;
			if (!Test.TestTrue(TEXT("通过正式库存移动收到 T2"),
				UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(
					CampInventory, CampIndex, PlayerInventory, DestinationIndex))) return false;
			return Test.TestTrue(TEXT("收到 T2 后刷新装备选择读模型"),
				Equipment->RefreshLoadoutFromInventoryComponentFromAuthority());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBrokenRodReplacementTest,
	"Catfishing.Unit.Equipment.RodReplacement.FormalT2ReplacesPackedBrokenT1ThroughGenericInventoryMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 断竿换新回归流程：覆盖购买早于收杆和收杆后购买两种顺序，再分别走右键目标和拖拽目标，验证当前选择只切到玩家正式库存中的 T2。
bool FCatBrokenRodReplacementTest::RunTest(const FString& Parameters)
{
	using namespace CatRodReplacementTests;
	for (const bool bBuyBeforePack : {false, true})
	{
		for (const bool bDrag : {false, true})
		{
			FFixture Fixture;
			if (!Fixture.Initialize(*this) || !Fixture.BreakDeployedRod(*this)) return false;
			if (bBuyBeforePack)
			{
				if (!Fixture.ReceiveT2(*this, bDrag)) return false;
				TestEqual(TEXT("坏竿尚未收回时保持原活动实例"), Fixture.Equipment->GetSnapshot().RodItemInstanceId, Fixture.OldItemId);
			}
			if (!Fixture.Pack(*this)) return false;
			if (!bBuyBeforePack && !Fixture.ReceiveT2(*this, bDrag)) return false;
			const FCatEquipmentLoadoutSnapshot Loadout = Fixture.Equipment->GetSnapshot();
			TestEqual(TEXT("自动切到不同型号 T2"), Loadout.RodDefinitionId, FName(TEXT("ShopRodT2")));
			TestEqual(TEXT("选中公共仓库交付的同一实例"), Loadout.RodItemInstanceId, Fixture.NewItemId);
			const UCatEquipmentDefinition* T2 = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("ShopRodT2"));
			if (!TestNotNull(TEXT("T2 正式资产可用于运行"), T2)) return false;
			const UCatEquipmentFragment_Rod* T2RodFragment = T2->FindFragment<UCatEquipmentFragment_Rod>();
			if (!TestNotNull(TEXT("T2 正式资产含鱼竿参数片段"), T2RodFragment)) return false;
			TestEqual(TEXT("新竿使用自己的耐久上限"), Loadout.RodDurability, T2RodFragment->MaximumRodDurability);
			TestFalse(TEXT("T1 破损不污染 T2"), Loadout.bRodBroken);
			UCatInventoryComponent* PlayerInventory = Fixture.Character ? Fixture.Character->GetInventoryComponent() : nullptr;
			const int32 BrokenSlotIndex = PlayerInventory ? PlayerInventory->FindInventorySlotIndexFromInstanceId(Fixture.OldItemId) : INDEX_NONE;
			const FCatInventoryEntry* BrokenEntry = PlayerInventory ? PlayerInventory->GetInventoryEntryAtSlot(BrokenSlotIndex) : nullptr;
			const UCatEquipmentInventoryItemInstance* BrokenRod = BrokenEntry && BrokenEntry->StackCount > 0
				? Cast<UCatEquipmentInventoryItemInstance>(BrokenEntry->Instance) : nullptr;
			if (!TestTrue(TEXT("原竿实例仍保留为破损库存事实"), BrokenRod != nullptr)) return false;
			TestTrue(TEXT("原竿仍保持零耐久和破损"), BrokenRod->IsRodBroken() && BrokenRod->GetRodDurability() == 0);
			const FCatInventoryItemUseResult Use = Fixture.Equipment->Use(FGuid::NewGuid(), Loadout.Revision, Loadout.RodItemInstanceId);
			TestTrue(TEXT("按当前选择通过实际放竿库存裁决"), Use.bCommitted);
			const UCatEquipmentInventoryItemInstance* UsedRod = Use.Item.StackCount > 0
				? Cast<UCatEquipmentInventoryItemInstance>(Use.Item.Instance) : nullptr;
			if (!TestTrue(TEXT("实际使用返回装备实例"), UsedRod != nullptr)) return false;
			TestEqual(TEXT("实际使用的是 T2"), UsedRod->GetItemDefinitionId(), FName(TEXT("ShopRodT2")));
			UClass* RodClass = T2->UseActorClass.LoadSynchronous();
			TestTrue(TEXT("T2 配置了可生成的正式鱼竿 Actor"), RodClass && RodClass->IsChildOf(ACatFishingRodActor::StaticClass()));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHealthyRodSelectionPreservedTest,
	"Catfishing.Unit.Equipment.RodReplacement.HealthySelectionIsNotReplacedByNewModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 健康鱼竿保留回归流程：玩家已有可用 T1 时从公共仓库接收 T2，验证新增物品不会抢占仍有效的当前选择。
bool FCatHealthyRodSelectionPreservedTest::RunTest(const FString& Parameters)
{
	CatRodReplacementTests::FFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.ReceiveT2(*this, true)) return false;
	TestEqual(TEXT("购买 T2 不抢占仍健康的 T1"), Fixture.Equipment->GetSnapshot().RodItemInstanceId, Fixture.OldItemId);
	TestEqual(TEXT("健康选择的型号不变"), Fixture.Equipment->GetSnapshot().RodDefinitionId, FName(TEXT("StarterRodT1")));
	return !HasAnyErrors();
}

#endif
