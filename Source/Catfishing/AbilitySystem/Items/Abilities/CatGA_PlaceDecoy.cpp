#include "AbilitySystem/Items/Abilities/CatGA_PlaceDecoy.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryStatics.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// 目标流程：只沿服务器接受的视线取首个阻挡，再交给鱼护既有的交互距离与状态门。
ACatFishGuardActor* UCatGA_PlaceDecoy::ResolveGuard() const
{
	FVector Origin, Direction;
	if (!ResolveUseRay(Origin, Direction)) return nullptr;
	FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PlaceDecoy), false, GetAvatarActorFromActorInfo());
	GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 1000.0, ECC_Visibility, Query);
	auto* Guard = Cast<ACatFishGuardActor>(Hit.GetActor());
	return Guard && Guard->CanInteract_Implementation(CurrentActorInfo->PlayerController.Get()) ? Guard : nullptr;
}
// 配置流程：转移保留原实例，任何额外消费或效果都会导致双重结算，因此拒绝。
bool UCatGA_PlaceDecoy::ValidateUseConfiguration(const UCatItemUseFragment& Config, FText& Error) const
{
	const bool bValid = Config.ConsumeCount == 0 && Config.ResourceCapacity == 0 && Config.Effects.IsEmpty();
	if (!bValid) Error = NSLOCTEXT("CatItem", "DecoyConfig", "假鱼放入只移动原物品，请将消耗数量设为零且不配置自用效果。");
	return bValid;
}
// 预检流程：共同能力门通过后找目标的可用格；非堆叠保证只移动一件。
bool UCatGA_PlaceDecoy::ValidateUse() const
{
	if (!Super::ValidateUse()) return false;
	auto* Item = ResolveSourceItem(); auto* Guard = ResolveGuard();
	auto* Inventory = Guard ? Guard->FindComponentByClass<UCatFishOnlyInventoryComponent>() : nullptr;
	return Item && Item->GetItemDefinition()->GetMaxStackCount() == 1 && Inventory && Inventory->FindAvailableSlot(Item, 1) != INDEX_NONE;
}
// 提交流程：作用域锁保护来源能力被移格撤销；只调用已有跨宿主事务，返回后以事务结果结束。
void UCatGA_PlaceDecoy::CommitUse()
{
	if (!IsActive() || bUseCommitted || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
	IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
	if (!ValidateUse()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	auto* Guard = ResolveGuard(); auto* Inventory = Guard->FindComponentByClass<UCatFishOnlyInventoryComponent>();
	const int32 Slot = Inventory->FindAvailableSlot(ResolveSourceItem(), 1);
	const auto Result = UCatInventoryStatics::MoveItemBetweenInventoryHostsFromAuthority(Cast<ACatCharacter>(GetAvatarActorFromActorInfo()),
		UseTarget.RequestId, UseTarget.Inventory->GetOwner(), UseTarget.Inventory->FindInventorySlotIndexFromInstanceId(UseTarget.ItemId), Guard, Slot);
	bUseCommitted = Result.bCommitted;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bUseCommitted);
}
