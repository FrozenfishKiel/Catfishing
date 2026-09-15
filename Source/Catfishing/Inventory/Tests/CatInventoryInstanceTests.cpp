#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "GameFramework/Actor.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Interaction/CatInteractable.h"
#include "Items/CatEquipmentItem.h"
#include "UObject/GarbageCollection.h"

namespace CatInventoryInstanceTests
{
	// 测试定义创建流程：只配置普通库存必须的稳定 ID 与不可堆叠规则，让用例聚焦实例所有权而非装备或玩法语义。
	UCatInventoryItemDefinition* CreateOrdinaryDefinition()
	{
		UCatInventoryItemDefinition* Definition = NewObject<UCatInventoryItemDefinition>(GetTransientPackage());
		Definition->InventoryDefinitionId = TEXT("InventoryInstanceBoundaryItem");
		Definition->InventoryMaxStackCount = 1;
		return Definition;
	}

	// 装备拾取定义创建流程：只写入装备目录身份和运行 gate，使 ACatEquipmentItem 经正式库存批次创建装备实例而不依赖装备玩法字段。
	UCatEquipmentDefinition* CreatePickupEquipmentDefinition()
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->EquipmentDefinitionId = TEXT("EquipmentPickupBoundaryItem");
		Definition->FunctionalRouteId = TEXT("EquipmentPickupBoundaryRoute");
		Definition->bEnableRuntimeDefinition = true;
		return Definition;
	}

	// Actor 库存安装流程：把运行期创建的组件登记并注册到 authority Actor，再建立指定数量的真实空槽，保证后续操作经过正式组件入口。
	UCatInventoryComponent* AddInventoryComponent(AActor& Owner, const int32 SlotCount)
	{
		UCatInventoryComponent* Inventory = NewObject<UCatInventoryComponent>(&Owner);
		Owner.AddInstanceComponent(Inventory);
		Inventory->RegisterComponent();
		Inventory->SetInventorySlotCountFromAuthority(SlotCount);
		return Inventory;
	}

	// Actor 背包安装流程：保留普通库存实现，只以 BackPack 的收货优先级构造同 Actor 双库存场景。
	UCatBackPackComponent* AddBackPackComponent(AActor& Owner, const int32 SlotCount)
	{
		UCatBackPackComponent* BackPack = NewObject<UCatBackPackComponent>(&Owner);
		Owner.AddInstanceComponent(BackPack);
		BackPack->RegisterComponent();
		BackPack->SetInventorySlotCountFromAuthority(SlotCount);
		return BackPack;
	}

	// 库存内容断言流程：读取首格的实例和数量，确认拒绝路径没有替换或部分覆盖既有正式内容。
	bool TestPreservesEntry(FAutomationTestBase& Test, const UCatInventoryComponent& Inventory,
		const UCatInventoryItemInstance* ExpectedInstance)
	{
		const FCatInventoryEntry* Entry = Inventory.GetInventoryEntryAtSlot(0);
		return Test.TestTrue(TEXT("失败恢复保留原实例"), Entry != nullptr && Entry->Instance == ExpectedInstance)
			&& Test.TestEqual(TEXT("失败恢复保留原数量"), Entry != nullptr ? Entry->StackCount : 0, 1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryRestoreRejectsInvalidEntriesTest,
	"Catfishing.Unit.Inventory.RestoreRejectsInvalidEntriesWithoutReplacingCurrentContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 恢复边界测试流程：先建立已有库存，再分别提交空实例正数量、有效实例零数量和重复 ID；每次失败后都确认原对象仍在原格。
bool FCatInventoryRestoreRejectsInvalidEntriesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 authority 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	AActor* Owner = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建库存宿主"), Owner)) return false;

	UCatInventoryComponent* Inventory = CatInventoryInstanceTests::AddInventoryComponent(*Owner, 2);
	UCatInventoryItemDefinition* Definition = CatInventoryInstanceTests::CreateOrdinaryDefinition();
	if (!TestTrue(TEXT("写入恢复前的正式库存内容"), Inventory->AddItemDefinition(Definition, 1))) return false;
	const FCatInventoryEntry* OriginalEntry = Inventory->GetInventoryEntryAtSlot(0);
	if (!TestNotNull(TEXT("恢复前物品已入首格"), OriginalEntry) || !OriginalEntry->Instance) return false;
	UCatInventoryItemInstance* OriginalInstance = OriginalEntry->Instance;

	FText Failure;
	TArray<FCatInventoryEntry> NullInstanceEntries;
	FCatInventoryEntry& NullInstanceEntry = NullInstanceEntries.AddDefaulted_GetRef();
	NullInstanceEntry.StackCount = 1;
	TestFalse(TEXT("空实例与正数量不能恢复"), Inventory->RestoreInventorySlotsFromAuthority(NullInstanceEntries, 1, Failure));
	if (!CatInventoryInstanceTests::TestPreservesEntry(*this, *Inventory, OriginalInstance)) return false;

	UCatInventoryItemInstance* ZeroCountInstance = NewObject<UCatInventoryItemInstance>(Owner);
	ZeroCountInstance->SetItemDefinition(Definition);
	ZeroCountInstance->SetRuntimeOwnerActor(Owner);
	TArray<FCatInventoryEntry> ZeroCountEntries;
	FCatInventoryEntry& ZeroCountEntry = ZeroCountEntries.AddDefaulted_GetRef();
	ZeroCountEntry.Instance = ZeroCountInstance;
	TestFalse(TEXT("有效实例与零数量不能恢复"), Inventory->RestoreInventorySlotsFromAuthority(ZeroCountEntries, 1, Failure));
	if (!CatInventoryInstanceTests::TestPreservesEntry(*this, *Inventory, OriginalInstance)) return false;

	UCatInventoryItemInstance* FirstDuplicate = NewObject<UCatInventoryItemInstance>(Owner);
	FirstDuplicate->SetItemDefinition(Definition);
	FirstDuplicate->SetRuntimeOwnerActor(Owner);
	UCatInventoryItemInstance* SecondDuplicate = NewObject<UCatInventoryItemInstance>(Owner);
	SecondDuplicate->SetItemDefinition(Definition);
	SecondDuplicate->SetRuntimeOwnerActor(Owner);
	SecondDuplicate->SetItemInstanceIdFromAuthority(FirstDuplicate->GetItemInstanceId());
	TArray<FCatInventoryEntry> DuplicateIdEntries;
	FCatInventoryEntry& FirstDuplicateEntry = DuplicateIdEntries.AddDefaulted_GetRef();
	FirstDuplicateEntry.Instance = FirstDuplicate;
	FirstDuplicateEntry.StackCount = 1;
	FCatInventoryEntry& SecondDuplicateEntry = DuplicateIdEntries.AddDefaulted_GetRef();
	SecondDuplicateEntry.Instance = SecondDuplicate;
	SecondDuplicateEntry.StackCount = 1;
	TestFalse(TEXT("重复实例 ID 不能恢复"), Inventory->RestoreInventorySlotsFromAuthority(DuplicateIdEntries, 2, Failure));
	return CatInventoryInstanceTests::TestPreservesEntry(*this, *Inventory, OriginalInstance) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryReturnHeldUsesOriginalComponentTest,
	"Catfishing.Unit.Inventory.ReturnHeldReturnsToOriginalComponentWhenBackPackHasHigherPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Held 归还路由测试流程：同一 Actor 同时挂普通库存与高优先级背包，从普通库存借出后由同一组件归还，验证对象没有被 Actor 级收货优先级改道。
bool FCatInventoryReturnHeldUsesOriginalComponentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 authority 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	AActor* Owner = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建双库存宿主"), Owner)) return false;

	UCatInventoryComponent* OrdinaryInventory = CatInventoryInstanceTests::AddInventoryComponent(*Owner, 1);
	UCatBackPackComponent* BackPack = CatInventoryInstanceTests::AddBackPackComponent(*Owner, 1);
	if (!TestTrue(TEXT("背包具有更高统一收货优先级"),
		BackPack->GetUnifiedInventoryIntakePriority() > OrdinaryInventory->GetUnifiedInventoryIntakePriority())) return false;
	UCatInventoryItemDefinition* Definition = CatInventoryInstanceTests::CreateOrdinaryDefinition();
	if (!TestTrue(TEXT("普通库存持有测试物品"), OrdinaryInventory->AddItemDefinition(Definition, 1))) return false;
	const FCatInventoryEntry* SourceEntry = OrdinaryInventory->GetInventoryEntryAtSlot(0);
	if (!TestNotNull(TEXT("普通库存首格有物品"), SourceEntry) || !SourceEntry->Instance) return false;
	UCatInventoryItemInstance* OriginalInstance = SourceEntry->Instance;
	const FGuid InstanceId = OriginalInstance->GetItemInstanceId();

	FCatInventoryEntry HeldEntry;
	if (!TestTrue(TEXT("普通库存成功借出同一实例"), OrdinaryInventory->HoldInventoryEntryAtSlotFromAuthority(0, HeldEntry))) return false;
	FCatInventoryEntry ReturnedEntry;
	const FCatDomainCommandResult ReturnResult = OrdinaryInventory->ReturnHeldInventoryItemInstanceFromAuthority(
		FGuid::NewGuid(), InstanceId, 1, ReturnedEntry);
	if (!TestTrue(TEXT("普通库存成功归还 held 实例"), ReturnResult.bCommitted)) return false;
	const FCatInventoryEntry* OrdinaryReturnedEntry = OrdinaryInventory->GetInventoryEntryAtSlot(0);
	const FCatInventoryEntry* BackPackEntry = BackPack->GetInventoryEntryAtSlot(0);
	TestTrue(TEXT("归还对象仍是同一 UObject"), OrdinaryReturnedEntry != nullptr && OrdinaryReturnedEntry->Instance == OriginalInstance);
	TestEqual(TEXT("归还对象保持原稳定 ID"), OrdinaryReturnedEntry && OrdinaryReturnedEntry->Instance
		? OrdinaryReturnedEntry->Instance->GetItemInstanceId() : FGuid(), InstanceId);
	TestTrue(TEXT("高优先级背包没有接收普通库存的归还"), BackPackEntry != nullptr && BackPackEntry->Instance == nullptr);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryTransferSurvivesSourceActorCollectionTest,
	"Catfishing.Unit.Inventory.TransferKeepsInstanceAliveAfterSourceActorCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 跨 Actor 实例生存测试流程：把普通物品移入目标库存，销毁源 Actor 并触发 GC；目标格的强引用、稳定 ID 与运行宿主都必须仍指向同一运行实例。
bool FCatInventoryTransferSurvivesSourceActorCollectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 authority 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	AActor* SourceActor = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	AActor* TargetActor = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	if (!TestTrue(TEXT("创建源与目标库存宿主"), SourceActor != nullptr && TargetActor != nullptr)) return false;

	UCatInventoryComponent* SourceInventory = CatInventoryInstanceTests::AddInventoryComponent(*SourceActor, 1);
	UCatInventoryComponent* TargetInventory = CatInventoryInstanceTests::AddInventoryComponent(*TargetActor, 1);
	UCatInventoryItemDefinition* Definition = CatInventoryInstanceTests::CreateOrdinaryDefinition();
	if (!TestTrue(TEXT("源库存写入普通物品"), SourceInventory->AddItemDefinition(Definition, 1))) return false;
	const FCatInventoryEntry* SourceEntry = SourceInventory->GetInventoryEntryAtSlot(0);
	if (!TestNotNull(TEXT("源库存首格持有实例"), SourceEntry) || !SourceEntry->Instance) return false;
	TWeakObjectPtr<UCatInventoryItemInstance> MovedInstance(SourceEntry->Instance);
	const FGuid MovedInstanceId = MovedInstance->GetItemInstanceId();

	const FCatDomainCommandResult MoveResult = SourceInventory->MoveItemToInventoryFromAuthority(
		FGuid::NewGuid(), 0, TargetInventory, 0, TEXT("InventoryInstanceLifetimeTest"));
	if (!TestTrue(TEXT("普通物品成功转移到目标库存"), MoveResult.bCommitted)) return false;
	const FCatInventoryEntry* TargetEntry = TargetInventory->GetInventoryEntryAtSlot(0);
	if (!TestTrue(TEXT("转移保留同一实例对象"), TargetEntry != nullptr && TargetEntry->Instance == MovedInstance.Get())) return false;

	SourceActor->Destroy();
	SourceActor = nullptr;
	CollectGarbage(RF_NoFlags);
	TargetEntry = TargetInventory->GetInventoryEntryAtSlot(0);
	if (!TestTrue(TEXT("源 Actor 回收后目标强引用实例仍有效"), IsValid(MovedInstance.Get()))) return false;
	TestTrue(TEXT("源 Actor 回收后目标格仍持有同一对象"), TargetEntry != nullptr && TargetEntry->Instance == MovedInstance.Get());
	TestEqual(TEXT("源 Actor 回收后实例稳定 ID 不变"), MovedInstance->GetItemInstanceId(), MovedInstanceId);
	TestTrue(TEXT("源 Actor 回收后运行宿主已保持目标 Actor"), MovedInstance->GetRuntimeOwnerActor() == TargetActor);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatEquipmentItemPickupBoundaryTest,
	"Catfishing.Unit.Inventory.EquipmentItemPickupRejectsFullBagAndPreventsReentrantDoubleGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 装备世界物拾取测试流程：先让背包满载验证失败会保留且可重试，再释放空位并在库存变更回调重入同一物品；
// 成功只能发货一次，原 Actor 交给库存实例保管（隐藏、关碰撞、不可再拾取），不销毁——再次丢弃要复用它。
bool FCatEquipmentItemPickupBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 authority 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	if (!TestTrue(TEXT("创建角色与正式玩家控制器"), Character != nullptr && Controller != nullptr)) return false;
	Controller->Possess(Character);
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("角色拥有正式随身库存"), Inventory)) return false;

	UCatEquipmentDefinition* EquipmentDefinition = CatInventoryInstanceTests::CreatePickupEquipmentDefinition();
	ACatEquipmentItem* Pickup = World->SpawnActor<ACatEquipmentItem>(Character->GetActorLocation(), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("创建真实装备世界物"), Pickup)) return false;
	Pickup->SetEquipmentDefinition(EquipmentDefinition);
	// BlueprintNativeEvent 必须在已启动的世界中通过 ProcessEvent 分发；否则反射调用被引擎拦截，测不到实际拾取逻辑。
	if (!TestTrue(TEXT("启动世界物和角色生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
	// 占有与 BeginPlay 会装配正式背包容量；按实际容量填满，不能假设缩小配置会截断既有槽位。
	const int32 SlotCapacity = Inventory->GetInventorySlotCount();
	if (!TestTrue(TEXT("正式背包具有可用容量"), SlotCapacity > 0)) return false;
	UCatInventoryItemDefinition* FillerDefinition = CatInventoryInstanceTests::CreateOrdinaryDefinition();
	if (!TestTrue(TEXT("预先填满正式背包"), Inventory->AddItemDefinition(FillerDefinition, SlotCapacity))) return false;
	TestEqual(TEXT("每格都被不可堆叠物品占用"), Inventory->CountVisibleInventoryQuantityByDefinitionId(FillerDefinition->GetInventoryDefinitionId()), SlotCapacity);
	const FCatInventoryEntry* FillerEntry = Inventory->GetInventoryEntryAtSlot(0);
	if (!TestNotNull(TEXT("满包格存在填充物"), FillerEntry) || !FillerEntry->Instance) return false;
	UCatInventoryItemInstance* FillerInstance = FillerEntry->Instance;
	if (!TestTrue(TEXT("拾取前通过正式交互资格"), ICatInteractable::Execute_CanInteract(Pickup, Controller))) return false;
	TestFalse(TEXT("满包拾取被正式库存拒绝"), ICatInteractable::Execute_Interact(Pickup, Controller, FGuid::NewGuid()));
	TestTrue(TEXT("满包失败保留世界物"), IsValid(Pickup) && !Pickup->IsActorBeingDestroyed());
	TestTrue(TEXT("满包失败不产生装备实例"),
		Inventory->CountVisibleInventoryQuantityByDefinitionId(EquipmentDefinition->GetInventoryDefinitionId()) == 0);

	Inventory->RemoveItemInstance(FillerInstance);
	if (!TestTrue(TEXT("释放空位后世界物仍可交互"), ICatInteractable::Execute_CanInteract(Pickup, Controller))) return false;
	int32 ReentrantAttemptCount = 0;
	bool bReentrantPickupResult = true;
	const FDelegateHandle ObservedHandle = Inventory->OnInventoryObservedChanged.AddLambda(
		[&ReentrantAttemptCount, &bReentrantPickupResult, Pickup, Controller]()
		{
			++ReentrantAttemptCount;
			bReentrantPickupResult = ICatInteractable::Execute_Interact(Pickup, Controller, FGuid::NewGuid());
		});
	const bool bPickupSucceeded = ICatInteractable::Execute_Interact(Pickup, Controller, FGuid::NewGuid());
	Inventory->OnInventoryObservedChanged.Remove(ObservedHandle);
	TestTrue(TEXT("空位拾取成功"), bPickupSucceeded);
	TestEqual(TEXT("库存变更回调只重入一次拾取"), ReentrantAttemptCount, 1);
	TestFalse(TEXT("重入拾取被世界物占用状态拒绝"), bReentrantPickupResult);
	TestEqual(TEXT("成功拾取只入账一件装备"),
		Inventory->CountVisibleInventoryQuantityByDefinitionId(EquipmentDefinition->GetInventoryDefinitionId()), 1);
	// 2026-09-13 修：原来这里断言「成功拾取销毁世界物」，那是旧生命周期。
	// 现行链路把原 Actor 交给库存实例保管（CatItem.cpp:126 交接、:154 保留并关掉物理／碰撞／可见性），
	// 只有没有实例保留它的批次才走 :160 的 Destroy——因为再次丢弃要复用同一个 Actor
	//（CatInventoryComponent.cpp:2401 取 GetWorldActor()、:2448 重设变换与碰撞）。
	// Editor 侧同口径回归 09-11 已随 1681d48 迁移（CatInventoryWorldActionsTests.cpp:351-364），
	// 这条 Unit 末尾的销毁断言当时漏了迁移，从此一直红着。
	// 原断言要抓的是「成功后世界里还留着可重复拾取的副本」，下面第二条直接验这件事，比验销毁更贴那个意图。
	TestTrue(TEXT("成功拾取保留原Actor并隐藏关碰撞"), IsValid(Pickup) && !Pickup->IsActorBeingDestroyed()
		&& Pickup->IsHidden() && !Pickup->GetActorEnableCollision());
	TestFalse(TEXT("库存保管的原物不可重复拾取"), ICatInteractable::Execute_CanInteract(Pickup, Controller));
	bool bInstanceHoldsPickup = false;
	for (int32 SlotIndex = 0; SlotIndex < Inventory->GetInventorySlotCount() && !bInstanceHoldsPickup; ++SlotIndex)
	{
		const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(SlotIndex);
		bInstanceHoldsPickup = Entry && Entry->Instance && Entry->Instance->GetWorldActor() == Pickup;
	}
	TestTrue(TEXT("入账实例保存原Actor"), bInstanceHoldsPickup);
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryUseAtomicCallbackTest,
	"Catfishing.Unit.Inventory.UseCallbackIsIdempotentAndPublishesOnlyCommittedQuantity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 真实库存回调边界：用一条带身份的鱼进入同一扣量事务，验证预检重入、效果拒绝、通知时重放都不产生第二次效果或临时空格。
bool FCatInventoryUseAtomicCallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 authority 世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	AActor* Owner = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	UCatInventoryComponent* Inventory = CatInventoryInstanceTests::AddInventoryComponent(*Owner, 2);
	UCatFishDefinition* Definition = NewObject<UCatFishDefinition>();
	Definition->FishDefinitionId = TEXT("UseCallbackFish");
	UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(Owner);
	Fish->SetItemDefinition(Definition);
	Fish->SetRuntimeOwnerActor(Owner);
	if (!TestTrue(TEXT("写入实际鱼身份"), Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("TestPlayer"), 1.0))) return false;
	TArray<FCatInventoryEntry> Entries;
	FCatInventoryEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Instance = Fish; Entry.StackCount = 1;
	if (!TestTrue(TEXT("恢复一条真实鱼"), Inventory->ReplaceInventoryEntriesFromAuthority(Entries, 2))) return false;
	int32 Notifications = 0;
	int32 Effects = 0;
	const FDelegateHandle Observer = Inventory->OnInventoryObservedChanged.AddLambda([&]() { ++Notifications; });
	const FGuid FailedRequest = FGuid::NewGuid();
	const auto Ready = [](FCatInventoryItemUseResult&) { return ECatDomainCommandError::None; };
	const auto UnexpectedEffect = [&](FCatInventoryItemUseResult&) { ++Effects; return true; };
	const FCatInventoryItemUseResult Failed = Inventory->UseItemInstanceFromAuthority(FailedRequest, Fish->GetItemInstanceId(), 1, TEXT("failure"),
		[&](FCatInventoryItemUseResult&)
		{
			const auto Reentrant = Inventory->UseItemInstanceFromAuthority(FailedRequest, Fish->GetItemInstanceId(), 1, TEXT("failure"), Ready, UnexpectedEffect);
			TestFalse(TEXT("预检中的重入不能提交"), Reentrant.bCommitted);
			TestTrue(TEXT("预检中的重入有缓存标记"), Reentrant.bTerminalReplay);
			return ECatDomainCommandError::None;
		},
		[&](FCatInventoryItemUseResult&) { ++Effects; return false; });
	TestFalse(TEXT("拒绝效果不提交"), Failed.bCommitted);
	TestEqual(TEXT("失败只执行一次效果尝试"), Effects, 1);
	TestEqual(TEXT("失败不向UI发布临时扣量"), Notifications, 0);
	CatInventoryInstanceTests::TestPreservesEntry(*this, *Inventory, Fish);
	const auto FailedReplay = Inventory->UseItemInstanceFromAuthority(FailedRequest, Fish->GetItemInstanceId(), 1, TEXT("failure"), Ready, UnexpectedEffect);
	TestTrue(TEXT("失败终态可以重放"), FailedReplay.bTerminalReplay);
	TestEqual(TEXT("失败重放不重试效果"), Effects, 1);
	const FGuid SuccessRequest = FGuid::NewGuid();
	const auto Success = Inventory->UseItemInstanceFromAuthority(SuccessRequest, Fish->GetItemInstanceId(), 1, TEXT("success"), Ready, UnexpectedEffect);
	TestTrue(TEXT("首次成功正式提交"), Success.bCommitted);
	TestEqual(TEXT("成功只发布一次库存"), Notifications, 1);
	const auto SuccessReplay = Inventory->UseItemInstanceFromAuthority(SuccessRequest, Fish->GetItemInstanceId(), 1, TEXT("success"), Ready, UnexpectedEffect);
	TestTrue(TEXT("原格耗尽后仍可重放成功"), SuccessReplay.bReplayedTerminalCommitted);
	TestEqual(TEXT("成功重放不重复效果"), Effects, 2);
	TestEqual(TEXT("成功重放不重复通知"), Notifications, 1);
	Inventory->OnInventoryObservedChanged.Remove(Observer);
	return !HasAnyErrors();
}

#endif
