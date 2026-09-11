#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatHerbRecoveryItemFragment.generated.h"

/** 草药恢复物品片段；它只声明这份定义可被 Condition 恢复命令消费，库存核心不会因此认识草药规则或保存额外状态。 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType)
class CATFISHING_API UCatHerbRecoveryItemFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
};
