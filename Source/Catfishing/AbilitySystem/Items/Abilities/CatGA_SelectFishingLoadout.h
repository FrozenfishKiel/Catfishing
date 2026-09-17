#pragma once

#include "AbilitySystem/Items/Abilities/CatEquipmentItemAbility.h"
#include "CatGA_SelectFishingLoadout.generated.h"

/** 鱼饵和鱼漂装配行为；仅更改选择，鱼饵仍在既定真咬时点消耗。 */
UCLASS()
class CATFISHING_API UCatGA_SelectFishingLoadout : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
protected:
	/** 按定义的明确目标槽装配原实例，保留其余槽选择和现有消耗规则。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};
