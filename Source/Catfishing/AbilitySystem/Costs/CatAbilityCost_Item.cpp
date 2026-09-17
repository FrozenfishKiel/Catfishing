#include "AbilitySystem/Costs/CatAbilityCost_Item.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Abilities/GameplayAbilityTypes.h"

// 成本预检流程：标准激活先于目标传输，尚无目标时不假造来源；绑定后库存核对精确实例数量，嘴叼鱼要求消耗一条并复核使用权限。
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
	return Entry && Entry->Instance && Entry->StackCount >= Config->ConsumeCount;
}

// 支付流程：只在服务器处理；嘴叼鱼先准备独占再完成实物消费，库存来源按实例和请求键提交数量。
// 库存的零数量也走终态去重；只有首次成功才允许能力施加效果，重复请求不产生第二次收益。
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
	const FCatDomainCommandResult Result = Target.Inventory->ConsumeAbilityItemFromAuthority(Target.RequestId, Target.ItemId, Config->ConsumeCount);
	ItemAbility->SetResourceCommitted(Result.bCommitted && !Result.bTerminalReplay);
}
