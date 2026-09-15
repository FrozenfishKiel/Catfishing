#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Items/CatItem.h"
#include "UObject/StrongObjectPtr.h"

namespace CatInventoryActionsTests
{
	// 动作测试 World 初始化：用独立 authority World 承载真实角色、库存和物理查询，退出由 FTestWorldWrapper 自动配对清理。
	bool StartWorld(FAutomationTestBase& Test, FTestWorldWrapper& Wrapper)
	{
		if (!Test.TestTrue(TEXT("创建独立动作 authority World"), Wrapper.CreateTestWorld(EWorldType::Game)))
		{
			return false;
		}
		const bool bStarted = Wrapper.BeginPlayInTestWorld();
		Wrapper.ForwardErrorMessages(&Test);
		return Test.TestTrue(TEXT("启动动作测试 World 生命周期"), bStarted);
	}

	// 支撑地面构造：为真实放置/丢弃求解提供阻挡平面，避免动作测试因无地面提前拒绝而覆盖不到实例分发和幂等提交。
	AActor* AddFloor(ACatCharacter& Character)
	{
		AActor* Floor = Character.GetWorld()->SpawnActor<AActor>();
		if (Floor == nullptr)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->AddInstanceComponent(Box);
		Floor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(2000.0, 2000.0, 10.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetWorldLocation(Character.GetActorLocation() - FVector(0.0, 0.0,
			Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0));
		Box->SetMobility(EComponentMobility::Static);
		Box->RegisterComponent();
		return Floor;
	}

	// 测试定义构造：只声明可选数量的 Drop，确保未声明动作被服务器拒绝而不是由实例类型或 UI 名称暗中放行。
	UCatInventoryItemDefinition* MakeActionDefinition()
	{
		UCatInventoryItemDefinition* Definition = NewObject<UCatInventoryItemDefinition>();
		Definition->InventoryDefinitionId = TEXT("InventoryActionContract");
		Definition->InventoryMaxStackCount = 3;
		Definition->WorldActorClass = ACatItem::StaticClass();
		Definition->InventoryActions = {
			{CatInventoryActionTags::Drop, NSLOCTEXT("CatInventory", "TestDrop", "丢弃"), ECatInventoryActionQuantityMode::Select}};
		return Definition;
	}

	// 服务器上下文构造：请求身份只来自已占有的角色和组件，测试不会伪造定义、数量或实例以外的权威事实。
	FCatInventoryItemUseContext MakeContext(ACatCharacter& Character, UCatInventoryComponent& Inventory,
		const FGuid RequestId, const int32 Slot)
	{
		FCatInventoryItemUseContext Context;
		Context.RequestId = RequestId;
		Context.RequestingController = Character.GetController();
		Context.UserPawn = &Character;
		Context.SourceInventory = &Inventory;
		Context.InventorySlotIndex = Slot;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryActionAuthorityContractTest,
	"Catfishing.Contract.Inventory.Actions.AuthorityIdentityQuantityCapabilityAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 统一动作服务器合同：验证服务端只接受当前格的实例身份、定义已声明的能力和合法数量；拒绝和成功均缓存终态，重放不会重复改变库存。
bool FCatInventoryActionAuthorityContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatInventoryActionsTests;
	FTestWorldWrapper Wrapper;
	if (!StartWorld(*this, Wrapper))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	if (!TestTrue(TEXT("创建动作角色和控制器"), Character != nullptr && Controller != nullptr))
	{
		return false;
	}
	Controller->Possess(Character);
	if (!TestNotNull(TEXT("创建动作地面"), AddFloor(*Character)))
	{
		return false;
	}
	UCatInventoryComponent* Inventory = Character->GetInventoryComponent();
	TStrongObjectPtr<UCatInventoryItemDefinition> Definition(MakeActionDefinition());
	if (!TestTrue(TEXT("三件动作测试物入正式背包"), Inventory != nullptr && Inventory->AddItemDefinition(Definition.Get(), 3)))
	{
		return false;
	}
	const int32 Slot = Inventory->FindFirstInventorySlotIndexByDefinitionId(Definition->GetInventoryDefinitionId());
	const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Slot);
	if (!TestTrue(TEXT("定位动作测试槽位和实例"), Entry != nullptr && Entry->Instance != nullptr))
	{
		return false;
	}
	const FGuid ItemId = Entry->Instance->GetItemInstanceId();

	const FGuid InvalidQuantityRequest = FGuid::NewGuid();
	FCatDomainCommandResult Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, InvalidQuantityRequest, Slot), ItemId, CatInventoryActionTags::Drop, 0);
	TestEqual(TEXT("零数量由服务器拒绝"), Result.Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("拒绝不改变原堆数量"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 3);
	Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, InvalidQuantityRequest, Slot), ItemId, CatInventoryActionTags::Drop, 0);
	TestTrue(TEXT("非法数量的同请求返回终态重放"), Result.bTerminalReplay);
	TestEqual(TEXT("非法数量重放保留首次拒绝原因"), Result.Error, ECatDomainCommandError::InvalidPayload);

	Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, FGuid::NewGuid(), Slot), ItemId, CatInventoryActionTags::Use, 1);
	TestEqual(TEXT("定义未声明 Use 时服务器拒绝能力伪造"), Result.Error, ECatDomainCommandError::InvalidPayload);
	Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, FGuid::NewGuid(), Slot), FGuid::NewGuid(), CatInventoryActionTags::Drop, 1);
	TestEqual(TEXT("非当前格实例身份被服务器拒绝"), Result.Error, ECatDomainCommandError::InvalidPayload);

	const FGuid DropRequest = FGuid::NewGuid();
	Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, DropRequest, Slot), ItemId, CatInventoryActionTags::Drop, 1);
	if (!TestTrue(TEXT("已声明 Drop 首次提交成功"), Result.bCommitted && Result.Error == ECatDomainCommandError::None))
	{
		return false;
	}
	TestEqual(TEXT("首次 Drop 只扣一件"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 2);
	Result = Inventory->ExecuteItemActionFromAuthority(
		MakeContext(*Character, *Inventory, DropRequest, Slot), ItemId, CatInventoryActionTags::Drop, 1);
	TestTrue(TEXT("成功动作同请求标记终态重放"), Result.bTerminalReplay);
	TestTrue(TEXT("成功动作重放保留首次提交事实"), Result.bReplayedTerminalCommitted);
	TestEqual(TEXT("成功动作重放返回 AlreadyResolved"), Result.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("成功动作重放不二次扣量"), Inventory->GetInventoryEntryAtSlot(Slot)->StackCount, 2);
	return !HasAnyErrors();
}

#endif
