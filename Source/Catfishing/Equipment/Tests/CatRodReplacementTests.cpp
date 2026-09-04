#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "OnlineSubsystemTypes.h"

namespace CatRodReplacementTests
{
	// 使用正式 T1/T2 资产和公共仓库入口，不改资产数值或构造第二份装备选择状态。
	struct FFixture
	{
		FTestWorldWrapper WorldWrapper;
		ACatfishingPlayerController* Controller = nullptr;
		UCatEquipmentComponent* Equipment = nullptr;
		ACatCampInventoryActor* Camp = nullptr;
		ACatFishingRodActor* OldRod = nullptr;
		FGuid OldItemId;
		FGuid NewItemId;

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("创建真实装备 World"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
			WorldWrapper.ForwardErrorMessages(&Test);
			if (!Test.TestTrue(TEXT("启动 Actor 生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
			ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
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

		bool BreakDeployedRod(FAutomationTestBase& Test)
		{
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
			if (!Test.TestTrue(TEXT("部署原 T1 实例"), Equipment->Use(
				FGuid::NewGuid(), Loadout.Revision, OldItemId).bCommitted)) return false;
			UWorld* World = WorldWrapper.GetTestWorld();
			OldRod = World->SpawnActor<ACatFishingRodActor>();
			if (!Test.TestNotNull(TEXT("创建 T1 世界鱼竿"), OldRod)) return false;
			if (!Test.TestTrue(TEXT("绑定 T1 世界身份"), OldRod->InitializeAuthoritativeIdentity(
				FGuid::NewGuid(), OldItemId, Loadout.RodDefinitionId, NAME_None, Controller->PlayerState, nullptr, true, false))) return false;
			OldRod->SetActorLocation(Controller->GetPawn()->GetActorLocation() + FVector(80, 0, 0));
			if (!Test.TestTrue(TEXT("登记原鱼竿"), World->GetSubsystem<UCatFishingService>()->RegisterDeployedRod(
				Controller->PlayerState, OldRod))) return false;
			const FGuid SessionId = FGuid::NewGuid();
			if (!Test.TestTrue(TEXT("绑定原鱼竿钓鱼会话"), Equipment->BeginFishingUse(SessionId,
				OldItemId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId, Loadout.RodDefinitionId,
				Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Equipment->GetSnapshot().Revision).bReserved)) return false;
			if (!Test.TestTrue(TEXT("提交鱼饵"), Equipment->CommitFishingBaitDeferred(SessionId).bApplied)) return false;
			if (!Test.TestTrue(TEXT("耗尽 T1 实例耐久"), Equipment->ApplyFishingRodWear(
				SessionId, 1, Loadout.RodDurability + 1).bRodBroken)) return false;
			if (!Test.TestTrue(TEXT("同步世界断竿事实"), OldRod->SetBrokenFromAuthority(true,
				OldRod->GetPresentationState().RodActorRevision))) return false;
			return Test.TestTrue(TEXT("结束旧会话"), Equipment->ReleaseFishingUse(SessionId).bApplied);
		}

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

		bool ReceiveT2(FAutomationTestBase& Test, const bool bDrag)
		{
			if (!Test.TestTrue(TEXT("商店交付 T2 到公共仓库"), Camp->AddItemFromAuthority(
				FGuid::NewGuid(), Camp->GetSnapshot().Revision, TEXT("ShopRodT2"), 1).bCommitted)) return false;
			const int32 CampIndex = Camp->GetSnapshot().InventorySlots.IndexOfByPredicate(
				[](const FCatRunInventorySlot& Slot) { return Slot.DefinitionId == FName(TEXT("ShopRodT2")); });
			if (!Test.TestTrue(TEXT("仓库存在真实 T2"), CampIndex != INDEX_NONE)) return false;
			NewItemId = Camp->GetSnapshot().InventorySlots[CampIndex].ItemInstanceId;
			const int32 TargetIndex = Equipment->GetSnapshot().InventorySlots.IndexOfByPredicate(
				[](const FCatRunInventorySlot& Slot) { return Slot.Quantity <= 0; });
			const FCatDomainCommandResult Result = bDrag
				? Camp->WithdrawToEquipmentSlotFromAuthority(FGuid::NewGuid(), Camp->GetSnapshot().Revision,
					CampIndex, Equipment, Equipment->GetSnapshot().Revision, TargetIndex)
				: Camp->WithdrawToEquipmentFromAuthority(FGuid::NewGuid(), Camp->GetSnapshot().Revision,
					CampIndex, 1, Equipment, Equipment->GetSnapshot().Revision);
			return Test.TestTrue(TEXT("通过公共仓库拖拽或取用收到 T2"), Result.bCommitted);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBrokenRodReplacementTest,
	"Catfishing.Unit.Equipment.RodReplacement.FormalT2ReplacesPackedBrokenT1ThroughBothCampRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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
			const UCatEquipmentDefinition* T2 = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(TEXT("ShopRodT2"));
			if (!TestNotNull(TEXT("T2 正式资产可用于运行"), T2)) return false;
			TestEqual(TEXT("新竿使用自己的耐久上限"), Loadout.RodDurability, T2->MaximumRodDurability);
			TestFalse(TEXT("T1 破损不污染 T2"), Loadout.bRodBroken);
			const FCatRunInventorySlot* BrokenSlot = Loadout.InventorySlots.FindByPredicate(
				[&Fixture](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == Fixture.OldItemId; });
			if (!TestNotNull(TEXT("旧竿实例仍保留供维修"), BrokenSlot)) return false;
			TestTrue(TEXT("旧竿仍保持零耐久和破损"), BrokenSlot->bRodBroken && BrokenSlot->RodDurability == 0);
			const FCatInventoryItemUseResult Use = Fixture.Equipment->Use(FGuid::NewGuid(), Loadout.Revision, Loadout.RodItemInstanceId);
			TestTrue(TEXT("按当前选择通过实际放竿库存裁决"), Use.bCommitted);
			TestEqual(TEXT("实际使用的是 T2"), Use.Item.DefinitionId, FName(TEXT("ShopRodT2")));
			UClass* RodClass = T2->UseActorClass.LoadSynchronous();
			TestTrue(TEXT("T2 配置了可生成的正式鱼竿 Actor"), RodClass && RodClass->IsChildOf(ACatFishingRodActor::StaticClass()));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHealthyRodSelectionPreservedTest,
	"Catfishing.Unit.Equipment.RodReplacement.HealthySelectionIsNotReplacedByNewModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHealthyRodSelectionPreservedTest::RunTest(const FString& Parameters)
{
	CatRodReplacementTests::FFixture Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.ReceiveT2(*this, true)) return false;
	TestEqual(TEXT("购买 T2 不抢占仍健康的 T1"), Fixture.Equipment->GetSnapshot().RodItemInstanceId, Fixture.OldItemId);
	TestEqual(TEXT("健康选择的型号不变"), Fixture.Equipment->GetSnapshot().RodDefinitionId, FName(TEXT("StarterRodT1")));
	return !HasAnyErrors();
}

#endif
