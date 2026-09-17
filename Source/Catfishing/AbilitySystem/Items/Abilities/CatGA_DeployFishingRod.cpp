#include "AbilitySystem/Items/Abilities/CatGA_DeployFishingRod.h"
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
	// 原格预留与失败释放由切格和 GA 共用的部署入口处理。
	FCatPlaceRodCommand Command; Command.RequestId = Context.RequestId; Command.RequestedRodItemInstanceId = UseTarget.ItemId;
	Command.ExpectedEquipmentRevision = Character->GetEquipmentComponent()->GetSnapshot().Revision;
	return Commands->PlaceRodFromInventoryUseOnAuthority(Controller, Command);
}
