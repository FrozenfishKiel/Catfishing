#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace CatStarterScoopTests
{
	struct FSettingsScope
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		bool Enabled = Settings->bAutoGrantStarterScoopNet;
		bool Configure = Settings->bAutoConfigureStarterLoadout;
		FName DefinitionId = Settings->StarterScoopNetDefinitionId;
		int32 Capacity = Settings->InventorySlotCapacity;
		FSettingsScope()
		{
			Settings->bAutoGrantStarterScoopNet = true;
			Settings->bAutoConfigureStarterLoadout = false;
			Settings->StarterScoopNetDefinitionId = TEXT("StarterScoopNet");
			Settings->InventorySlotCapacity = 24;
		}
		~FSettingsScope()
		{
			Settings->bAutoGrantStarterScoopNet = Enabled;
			Settings->bAutoConfigureStarterLoadout = Configure;
			Settings->StarterScoopNetDefinitionId = DefinitionId;
			Settings->InventorySlotCapacity = Capacity;
		}
	};

	ACatCharacter* SpawnPlayer(UWorld* World, APlayerController*& Controller)
	{
		Controller = World->SpawnActor<APlayerController>();
		ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
		APlayerState* PlayerState = World->SpawnActor<APlayerState>();
		if (!Controller || !Character || !PlayerState) return nullptr;
		Controller->PlayerState = PlayerState;
		Character->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}

	int32 CountNets(const UCatEquipmentComponent* Equipment)
	{
		int32 Count = 0;
		for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
			if (Slot.DefinitionId == TEXT("StarterScoopNet")) Count += Slot.Quantity;
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatStarterScoopLifecycleTest,
	"Catfishing.Unit.Equipment.StarterScoop.EveryPlayerGetsOneUsableInstancePerCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatStarterScoopLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace CatStarterScoopTests;
	TestTrue(TEXT("当前测试配置默认发网"), GetDefault<UCatEquipmentSettings>()->bAutoGrantStarterScoopNet);
	FSettingsScope Restore;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建单人 authority World"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	TSet<FGuid> Instances;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		APlayerController* Controller = nullptr;
		ACatCharacter* Character = SpawnPlayer(Wrapper.GetTestWorld(), Controller);
		if (!TestNotNull(TEXT("生成并占有玩家角色"), Character)) return false;
		UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
		const auto Before = Equipment->GetSnapshot();
		TestEqual(TEXT("每人只发一把"), CountNets(Equipment), 1);
		TestTrue(TEXT("自动选择真实库存实例"), Before.ScoopNetItemInstanceId.IsValid());
		TestFalse(TEXT("不同玩家不会共用物品实例"), Instances.Contains(Before.ScoopNetItemInstanceId));
		Instances.Add(Before.ScoopNetItemInstanceId);
		TestTrue(TEXT("独立发网不会开启鱼竿装配"), Before.RodDefinitionId.IsNone());
		double Reach = 0;
		TestTrue(TEXT("生产抄网入口可解析装备"), UCatFishingAimLibrary::TryResolveScoopReach(Equipment, Reach));
		TestEqual(TEXT("正式抄网有效距离为 200cm"), Reach, 200.0);
		Controller->UnPossess();
		Controller->Possess(Character);
		TestEqual(TEXT("重复占有不发第二把"), CountNets(Equipment), 1);
		TestEqual(TEXT("重复占有不推进库存版本"), Equipment->GetSnapshot().Revision, Before.Revision);
		TestEqual(TEXT("重复占有保持实例"), Equipment->GetSnapshot().ScoopNetItemInstanceId, Before.ScoopNetItemInstanceId);

		ACatCharacter* NewCharacter = Wrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
		if (!TestNotNull(TEXT("生成新角色模拟重生"), NewCharacter)) return false;
		Controller->Possess(NewCharacter);
		TestEqual(TEXT("新角色的新库存获得一把"), CountNets(NewCharacter->GetEquipmentComponent()), 1);
		TestNotEqual(TEXT("新角色获得独立实例"), NewCharacter->GetEquipmentComponent()->GetSnapshot().ScoopNetItemInstanceId,
			Before.ScoopNetItemInstanceId);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatStarterScoopExistingTest,
	"Catfishing.Unit.Equipment.StarterScoop.ExistingAndDepositedInventoryDoesNotDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatStarterScoopExistingTest::RunTest(const FString& Parameters)
{
	using namespace CatStarterScoopTests;
	FSettingsScope Restore;
	Restore.Settings->bAutoGrantStarterScoopNet = false;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建 World"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	APlayerController* Controller = nullptr;
	ACatCharacter* Character = SpawnPlayer(Wrapper.GetTestWorld(), Controller);
	if (!TestNotNull(TEXT("生成玩家"), Character)) return false;
	UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestEqual(TEXT("开关关闭不发网"), CountNets(Equipment), 0);
	if (!TestTrue(TEXT("通过正式来源预先获得抄网"), Equipment->GrantEquipmentFromAuthority(
		FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("StarterScoopNet")).bCommitted)) return false;
	const auto Before = Equipment->GetSnapshot();
	Restore.Settings->bAutoGrantStarterScoopNet = true;
	Controller->UnPossess();
	Controller->Possess(Character);
	TestEqual(TEXT("已有抄网不多占背包格"), CountNets(Equipment), 1);
	TestEqual(TEXT("已有选择保持版本"), Equipment->GetSnapshot().Revision, Before.Revision);
	TestEqual(TEXT("已有选择保持实例"), Equipment->GetSnapshot().ScoopNetItemInstanceId, Before.ScoopNetItemInstanceId);

	ACatCampInventoryActor* Camp = Wrapper.GetTestWorld()->SpawnActor<ACatCampInventoryActor>();
	if (!TestNotNull(TEXT("创建公共仓库"), Camp)) return false;
	const int32 SlotIndex = Before.InventorySlots.IndexOfByPredicate([&](const FCatRunInventorySlot& Slot)
	{
		return Slot.ItemInstanceId == Before.ScoopNetItemInstanceId;
	});
	if (!TestTrue(TEXT("把抄网移入仓库"), Camp->DepositFromEquipmentSlotFromAuthority(FGuid::NewGuid(),
		Camp->GetSnapshot().Revision, 0, Equipment, Before.Revision, SlotIndex).bCommitted)) return false;
	const int64 DepositedRevision = Equipment->GetSnapshot().Revision;
	Controller->UnPossess();
	Controller->Possess(Character);
	TestEqual(TEXT("移出背包后重占有不会刷网"), CountNets(Equipment), 0);
	TestEqual(TEXT("重占有不写库存"), Equipment->GetSnapshot().Revision, DepositedRevision);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatStarterScoopFailureTest,
	"Catfishing.Unit.Equipment.StarterScoop.InvalidConfigurationAndCapacityFailWithoutPartialWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatStarterScoopFailureTest::RunTest(const FString& Parameters)
{
	using namespace CatStarterScoopTests;
	FSettingsScope Restore;
	Restore.Settings->StarterScoopNetDefinitionId = TEXT("FeatherFloat");
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建 World"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	AddExpectedError(TEXT("Error=InvalidScoopDefinition"), EAutomationExpectedErrorFlags::Contains, 1);
	APlayerController* Controller = nullptr;
	ACatCharacter* Character = SpawnPlayer(Wrapper.GetTestWorld(), Controller);
	if (!TestNotNull(TEXT("生成玩家"), Character)) return false;
	UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestEqual(TEXT("误配鱼漂不会发错物品"), Equipment->GetSnapshot().Revision, int64(0));
	TestTrue(TEXT("失败不扩容或写背包"), Equipment->GetSnapshot().InventorySlots.IsEmpty());
	Restore.Settings->StarterScoopNetDefinitionId = TEXT("StarterScoopNet");
	Restore.Settings->InventorySlotCapacity = 0;
	AddExpectedError(TEXT("Error=ECatDomainCommandError::CapacityExceeded"), EAutomationExpectedErrorFlags::Contains, 1);
	Controller->UnPossess();
	Controller->Possess(Character);
	TestEqual(TEXT("无容量不写版本"), Equipment->GetSnapshot().Revision, int64(0));
	TestFalse(TEXT("拒绝不会留下虚假装备选择"), Equipment->GetSnapshot().ScoopNetItemInstanceId.IsValid());
	Restore.Settings->InventorySlotCapacity = 24;
	Controller->UnPossess();
	Controller->Possess(Character);
	TestEqual(TEXT("解除失败条件后允许重试"), CountNets(Equipment), 1);
	return !HasAnyErrors();
}

#endif
