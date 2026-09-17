#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishDefinition.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishTankActor.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryStatics.h"
#include "Items/CatItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "OnlineSubsystemTypes.h"

namespace CatFishEntryTests
{
	struct FFixture
	{
		FTestWorldWrapper Wrapper;
		ACatCharacter* Cat = nullptr;
		ACatfishingPlayerController* Controller = nullptr;
		ACatFishGuardActor* Guard = nullptr;
		UCatFishInventoryItemInstance* Fish = nullptr;
		bool Start()
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
			UWorld* World = Wrapper.GetTestWorld();
			Cat = World->SpawnActor<ACatCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
			Controller = World->SpawnActor<ACatfishingPlayerController>();
			Guard = World->SpawnActor<ACatFishGuardActor>(FVector(100, 100, 100), FRotator::ZeroRotator);
			UClass* CatClass = LoadClass<ACatCharacter>(nullptr, TEXT("/Game/Character/BP_CatCharacter.BP_CatCharacter_C"));
			UCatFishDefinition* Definition = LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
			if (!Cat || !Controller || !Guard || !CatClass || !Definition) return false;
			auto* State = World->SpawnActor<ACatfishingPlayerState>();
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(TEXT("Batch7D"), FName(TEXT("CAT_TEST")));
			State->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			Controller->PlayerState = State;
			Cat->SetPlayerState(State);
			Controller->Possess(Cat);
			const auto* Mesh = CatClass->GetDefaultObject<ACatCharacter>()->GetMesh();
			Cat->GetMesh()->SetSkeletalMeshAsset(Mesh->GetSkeletalMeshAsset());
			Cat->GetMesh()->SetRelativeTransform(Mesh->GetRelativeTransform());
			Fish = NewObject<UCatFishInventoryItemInstance>(Guard);
			Fish->SetItemDefinition(Definition);
			return Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 2.5)
				&& Guard->GetFishInventoryComponent()->AddItemInstance(Fish, 1);
		}
		FCatDomainCommandResult Carry(AActor* Host = nullptr, UCatInventoryComponent* Inventory = nullptr)
		{
			if (!Host) Host = Guard;
			if (!Inventory) Inventory = Guard->GetFishInventoryComponent();
			return Inventory->ReleaseItemToWorldFromAuthority(Cat, FGuid::NewGuid(),
				Inventory->FindInventorySlotIndexFromInstanceId(Fish->GetItemInstanceId()), Fish->GetItemInstanceId(), 1, ECatInventoryWorldAction::Carry);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishAllInventoryEntryTest,
	"Catfishing.Unit.Inventory.FishCarryValidatesGroundContainerAndPreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishAllInventoryEntryTest::RunTest(const FString& Parameters)
{
	CatFishEntryTests::FFixture F;
	if (!TestTrue(TEXT("正式角色和鱼定义夹具"), F.Start())) return false;
	auto* Source = F.Guard->GetFishInventoryComponent();
	auto* Bag = F.Cat->GetInventoryComponent();
	const FGuid Id = F.Fish->GetItemInstanceId();
	FTestWorldWrapper OtherWorld;
	if (!OtherWorld.CreateTestWorld(EWorldType::Game)) return false;
	auto* ForeignCat = OtherWorld.GetTestWorld()->SpawnActor<ACatCharacter>();
	TestFalse(TEXT("跨World Carry拒绝"), Source->ReleaseItemToWorldFromAuthority(ForeignCat, FGuid::NewGuid(), 0, Id, 1, ECatInventoryWorldAction::Carry).bCommitted);
	auto* FakeHost = F.Wrapper.GetTestWorld()->SpawnActor<AActor>();
	auto* FakeInventory = NewObject<UCatInventoryComponent>(FakeHost);
	FakeHost->AddInstanceComponent(FakeInventory);
	FakeInventory->RegisterComponent();
	FakeInventory->SetInventorySlotCountFromAuthority(1);
	auto* FakeFish = NewObject<UCatFishInventoryItemInstance>(FakeHost);
	FakeFish->SetItemDefinition(F.Fish->GetItemDefinition());
	if (!FakeFish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 1.5)
		|| !FakeInventory->AddItemInstance(FakeFish, 1)) return false;
	TestFalse(TEXT("任意Host即使有鱼库存也不能Carry"), FakeInventory->ReleaseItemToWorldFromAuthority(F.Cat, FGuid::NewGuid(), 0,
		FakeFish->GetItemInstanceId(), 1, ECatInventoryWorldAction::Carry).bCommitted);

	const FVector Near = F.Guard->GetActorLocation();
	F.Guard->SetActorLocation(FVector(10000, 0, 100));
	TestFalse(TEXT("底层 Host 直调拒绝远处 Carry"), F.Carry().bCommitted);
	F.Guard->SetActorLocation(Near);
	F.Guard->SetInventoryOwnerFromAuthority(F.Cat);
	TestFalse(TEXT("底层拒绝已入包鱼护 Carry"), F.Carry().bCommitted);
	F.Guard->SetInventoryOwnerFromAuthority(nullptr);
	TestTrue(TEXT("原鱼身份与重量保留"), Source->GetInventoryEntryAtSlot(0)->Instance == F.Fish && F.Fish->GetFishWeightKilograms() == 2.5);
	TestTrue(TEXT("同一容器整理保留"), UCatInventoryStatics::MoveItemBetweenInventoryHostsFromAuthority(
		F.Cat, FGuid::NewGuid(), F.Guard, 0, F.Guard, 1).bCommitted);
	if (!TestTrue(TEXT("正式 Carry 成功"), F.Carry().bCommitted)) return false;
	auto* WorldFish = Cast<ACatFishPickupActor>(F.Cat->GetMouthCarriedActor());
	if (!TestNotNull(TEXT("正式入口创建可见世界鱼"), WorldFish)) return false;
	TestTrue(TEXT("嘴部附着保留原实例"), WorldFish->GetAttachParentActor() == F.Cat && !WorldFish->IsHidden() && F.Fish->GetWorldActor() == WorldFish);
	TestFalse(TEXT("任意 Host 入鱼拒绝"), WorldFish->StoreInFishGuardFromAuthority(F.Controller, FGuid::NewGuid(), F.Cat).Command.bCommitted);
	F.Guard->SetActorLocation(FVector(10000, 0, 100));
	TestFalse(TEXT("入护底层拒绝远距"), WorldFish->StoreInFishGuardFromAuthority(F.Controller, FGuid::NewGuid(), F.Guard).Command.bCommitted);
	F.Guard->SetActorLocation(Near);
	TestTrue(TEXT("正式入护保留同一鱼"), WorldFish->StoreInFishGuardFromAuthority(F.Controller, FGuid::NewGuid(), F.Guard).Command.bCommitted);
	TestTrue(TEXT("入护后的原Actor隐藏保管"), WorldFish->IsHidden() && F.Fish->GetWorldActor() == WorldFish);
	if (!TestTrue(TEXT("再次从原护Carry原鱼"), F.Carry().bCommitted)) return false;
	auto* Tank = F.Wrapper.GetTestWorld()->SpawnActor<ACatFishTankActor>(FVector(100,-100,100), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("正式鱼缸交互入口接入嘴鱼Store"), Tank->Interact_Implementation(F.Controller, FGuid::NewGuid()))) return false;
	TestTrue(TEXT("正式入口把原鱼存进缸且释放嘴"), Tank->GetFishInventoryComponent()->FindInventorySlotIndexFromInstance(F.Fish) != INDEX_NONE
		&& F.Cat->GetMouthCarriedActor() == nullptr && WorldFish->IsHidden() && F.Fish->GetWorldActor() == WorldFish);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGuardMouthOwnershipTest,
	"Catfishing.Unit.Inventory.SixSlotGuardPreservesContentsAndClaimsMouth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishGuardMouthOwnershipTest::RunTest(const FString& Parameters)
{
	CatFishEntryTests::FFixture F;
	if (!TestTrue(TEXT("正式夹具"), F.Start())) return false;
	TestEqual(TEXT("正式 BeginPlay 消费六格配置"), F.Guard->GetFishInventoryComponent()->GetInventorySlotCount(), 6);
	for (int32 Index = 1; Index <= 6; ++Index)
	{
		auto* Extra = NewObject<UCatFishInventoryItemInstance>(F.Guard);
		Extra->SetItemDefinition(F.Fish->GetItemDefinition());
		if (!Extra->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 2.0)) return false;
		const bool bAdded = F.Guard->GetFishInventoryComponent()->AddItemInstance(Extra, 1);
		TestEqual(TEXT("第七条明确拒收，前六条完整保留"), bAdded, Index < 6);
	}
	if (!TestTrue(TEXT("原鱼从鱼护进入嘴部"), F.Carry().bCommitted)) return false;
	auto* FishActor = Cast<ACatFishPickupActor>(F.Cat->GetMouthCarriedActor());
	// 先验证已有嘴鱼阻止鱼护认领，再清嘴并拾护；整个过程不改变护内实例。
	auto* Inside = NewObject<UCatFishInventoryItemInstance>(F.Guard);
	Inside->SetItemDefinition(F.Fish->GetItemDefinition());
	if (!Inside->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 3.75)
		|| !F.Guard->GetFishInventoryComponent()->AddItemInstance(Inside, 1)) return false;
	TestFalse(TEXT("叼鱼时拒绝拾护"), F.Guard->PickUpFromAuthority(F.Controller, FGuid::NewGuid()));
	TestTrue(TEXT("拾护拒绝保留嘴鱼"), F.Cat->GetMouthCarriedActor() == FishActor);
	FishActor->Destroy();
	if (!TestTrue(TEXT("空嘴可拾护"), F.Guard->PickUpFromAuthority(F.Controller, FGuid::NewGuid()))) return false;
	TestTrue(TEXT("原护可见并附着唯一嘴部"), F.Cat->GetMouthCarriedActor() == F.Guard && !F.Guard->IsHidden() && F.Guard->GetAttachParentActor() == F.Cat);
	TestEqual(TEXT("鱼护仍占一背包格"), F.Cat->GetInventoryComponent()->CountVisibleInventoryQuantityByItemId(11), 1);
	TestTrue(TEXT("护内原实例和重量保留"), F.Guard->GetFishInventoryComponent()->GetInventoryEntryAtSlot(0)->Instance == Inside && Inside->GetFishWeightKilograms() == 3.75);
	auto* Other = F.Wrapper.GetTestWorld()->SpawnActor<ACatFishGuardActor>(FVector(100, -100, 100), FRotator::ZeroRotator);
	auto* Next = NewObject<UCatFishInventoryItemInstance>(Other);
	Next->SetItemDefinition(F.Fish->GetItemDefinition());
	if (!Next->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Batch7D"), 1.5)
		|| !Other->GetFishInventoryComponent()->AddItemInstance(Next, 1)) return false;
	TestFalse(TEXT("持护时不能再占嘴叼鱼"), Other->GetFishInventoryComponent()->ReleaseItemToWorldFromAuthority(
		F.Cat, FGuid::NewGuid(), 0, Next->GetItemInstanceId(), 1, ECatInventoryWorldAction::Carry).bCommitted);
	return !HasAnyErrors();
}
#endif
