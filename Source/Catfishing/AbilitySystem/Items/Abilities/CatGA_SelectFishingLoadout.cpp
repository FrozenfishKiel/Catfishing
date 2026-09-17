#include "AbilitySystem/Items/Abilities/CatGA_SelectFishingLoadout.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"

// 装配流程：只接受定义明确声明的饵或漂目标槽，替换该槽实例并保留其他选择；装配不消耗鱼饵数量。
FCatDomainCommandResult UCatGA_SelectFishingLoadout::ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId; Result.Error = ECatDomainCommandError::InvalidPayload;
	auto* Character = Cast<ACatCharacter>(Context.UserPawn);
	auto* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const bool bBait = Definition.TargetSlot == ECatEquipmentLoadoutTargetSlot::Bait && Definition.CanServeFishingBait();
	const bool bFloat = Definition.TargetSlot == ECatEquipmentLoadoutTargetSlot::Float && Definition.CanServeFishingFloat();
	if (!Equipment || (!bBait && !bFloat)) return Result;
	const auto& Current = Equipment->GetSnapshot();
	return Equipment->ConfigureLoadoutFromAuthority(Context.RequestId, Current.Revision,
		Current.RodItemId, bBait ? Definition.ItemId : Current.BaitItemId,
		bFloat ? Definition.ItemId : Current.FloatItemId, Current.ScoopNetItemId, NAME_None,
		Current.RodItemInstanceId, bBait ? UseTarget.ItemId : Current.BaitItemInstanceId,
		bFloat ? UseTarget.ItemId : Current.FloatItemInstanceId, Current.ScoopNetItemInstanceId);
}
