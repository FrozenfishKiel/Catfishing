#include "AbilitySystem/Items/Abilities/CatGA_DeployFishingRod.h"
#include "Inventory/CatBackPackComponent.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"

// 拿竿流程：复核物品的鱼竿配置，命令携带原实例及当前权威装备版本，领域服务继续拥有放置、借出与物理初始化。
FCatDomainCommandResult UCatGA_DeployFishingRod::ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId; Result.Error = ECatDomainCommandError::InvalidPayload;
	auto* Character = Cast<ACatCharacter>(Context.UserPawn);
	auto* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	auto* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!Definition.CanServeFishingRod() || !Character || !Commands) return Result;
	// 借出前保留本次来源格，防止库存通知中的收货占掉原位；拿竿失败释放同一预留，仍由领域服务归还原实例。
	auto* BackPack = Cast<UCatBackPackComponent>(Context.SourceInventory);
	if (!BackPack || !BackPack->ReserveQuickbarHeldSlotFromAuthority(Context.InventorySlotIndex, UseTarget.ItemId)) return Result;
	FCatPlaceRodCommand Command; Command.RequestId = Context.RequestId; Command.RequestedRodItemInstanceId = UseTarget.ItemId;
	Command.ExpectedEquipmentRevision = Character->GetEquipmentComponent()->GetSnapshot().Revision;
	Result = Commands->PlaceRodFromInventoryUseOnAuthority(Controller, Command);
	if (!Result.bCommitted && BackPack->GetQuickbarHeldSlot().ItemInstanceId == UseTarget.ItemId)
		BackPack->ClearQuickbarHeldSlotFromAuthority();
	return Result;
}

