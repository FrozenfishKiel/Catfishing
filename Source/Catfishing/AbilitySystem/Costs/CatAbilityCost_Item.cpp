#include "AbilitySystem/Costs/CatAbilityCost_Item.h"
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Abilities/GameplayAbilityTypes.h"

// 成本预检流程：标准激活先于目标传输，尚无目标时不假造来源；绑定后核对精确实例件数及主操作的剩余资源。
// 副操作不预扣资源，具体补充资格由能力判断；嘴叼鱼要求消耗一条并复核使用权限。
bool UCatAbilityCost_Item::CheckCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* FailureTags) const
{
	const UCatItemGameplayAbility* ItemAbility = Cast<UCatItemGameplayAbility>(Ability);
	if (!ItemAbility || !ActorInfo) return false;
	const FCatItemAbilityTargetData& Target = ItemAbility->GetUseTarget();
	if (!Target.RequestId.IsValid()) return true;
	const UCatItemUseFragment* Config = ItemAbility->GetUseConfiguration();
	if (!Config) return false;
	if (IsValid(Target.WorldFish)) return Config->ConsumeCount == 1 && ItemAbility->ValidateUse();
	if (!IsValid(Target.Inventory)) return false;
	const int32 Slot = Target.Inventory->FindInventorySlotIndexFromInstanceId(Target.ItemId);
	const FCatInventoryEntry* Entry = Target.Inventory->GetInventoryEntryAtSlot(Slot);
	if (Entry && Entry->Instance && Config->ResourceCapacity > 0 && !Target.bSecondaryInput
		&& Entry->Instance->GetRemainingResource() < Config->ResourceCost) return false;
	return Entry && Entry->Instance && Entry->StackCount >= Config->ConsumeCount;
}

// 支付流程：只在服务器处理；嘴叼鱼先准备独占再完成实物消费，库存来源先计算本次资源余量及是否耗尽本体。
// 然后按实例和请求键提交件数，只有首次成功才写资源余额；余额落定后按能力约定广播，重放不重复扣费或通知。
// 库存的零数量也走终态去重；按能力约定决定是否延迟通知，延迟时由领域提交方负责发布，首次支付结果决定能否继续生效。
void UCatAbilityCost_Item::ApplyCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UCatItemGameplayAbility* ItemAbility = Cast<UCatItemGameplayAbility>(Ability);
	if (!ItemAbility || !ActorInfo || !ActorInfo->IsNetAuthority()) return;
	const UCatItemUseFragment* Config = ItemAbility->GetUseConfiguration();
	const FCatItemAbilityTargetData& Target = ItemAbility->GetUseTarget();
	if (!Config) return;
	if (IsValid(Target.WorldFish))
	{
		auto* Controller = ActorInfo->PlayerController.Get();
		const bool bPrepared = Config->ConsumeCount == 1 && Target.WorldFish->PrepareConsumptionFromAuthority(Controller, Target.RequestId);
		if (bPrepared) Target.WorldFish->FinishConsumptionFromAuthority(Controller, Target.RequestId, true);
		ItemAbility->SetResourceCommitted(bPrepared);
		return;
	}
	if (!IsValid(Target.Inventory)) return;
	auto* Item = ItemAbility->ResolveSourceItem();
	const bool bPayResource = Config->ResourceCapacity > 0 && !Target.bSecondaryInput;
	if (bPayResource && (!Item || Item->GetRemainingResource() < Config->ResourceCost)) return;
	const int32 Remaining = bPayResource ? Item->GetRemainingResource() - Config->ResourceCost : -1;
	const int32 Count = bPayResource && Config->bConsumeWhenEmpty && Remaining == 0 ? 1 : Config->ConsumeCount;
	const FCatDomainCommandResult Result = Target.Inventory->ConsumeAbilityItemFromAuthority(Target.RequestId, Target.ItemId, Count, false);
	if (Result.bCommitted && !Result.bTerminalReplay && bPayResource) Item->SetRemainingResourceFromAuthority(Remaining);
	ItemAbility->SetResourceCommitted(Result.bCommitted && !Result.bTerminalReplay);
	if (Result.bCommitted && !Result.bTerminalReplay && !ItemAbility->DefersInventoryCostNotification())
		Target.Inventory->BroadcastInventoryChange();
}
