#pragma once

#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_ConsumeFish.generated.h"

/** 食用鱼的行为实现；与容器无关，只补充可食用性、实际重量参数及吃过图鉴事件。 */
UCLASS()
class CATFISHING_API UCatGA_ConsumeFish : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 一次食用结算一条实物鱼；拒绝零成本或多条成本与单鱼重量经验不一致的配置。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
	/** 排除不可食用鱼，并在服务器检查成长系统能否接受本条鱼。 */
	virtual bool ValidateUse() const override;
	/** 按本条实物鱼的实际重量生成经验，保持逐条取整规则。 */
	virtual void GatherEffectParameters(TMap<FGameplayTag, float>& Parameters) const override;
	/** 首次确认消费后记录食用知识并释放隐藏鱼载体，不修改经验或库存。 */
	virtual void OnUseCommitted(UCatInventoryItemInstance* ConsumedItem) override;
};
