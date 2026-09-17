#pragma once

#include "AbilitySystem/Items/Abilities/CatEquipmentItemAbility.h"
#include "CatGA_DeployFishingRod.generated.h"

/** 鱼竿拿出行为；复用既有部署与真实鱼竿物理，库存实例不再编排部署。 */
UCLASS()
class CATFISHING_API UCatGA_DeployFishingRod : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
protected:
	/** 将原竿身份交给部署命令；选择格子本身不会触发本能力。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};
