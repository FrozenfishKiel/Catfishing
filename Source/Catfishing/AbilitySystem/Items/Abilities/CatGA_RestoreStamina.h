#pragma once
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_RestoreStamina.generated.h"

/** 小鱼干使用行为；只把缺少的绿色体力交给配置 GE，满体力不消费。 */
UCLASS()
class CATFISHING_API UCatGA_RestoreStamina : public UCatItemGameplayAbility
{
 GENERATED_BODY()
public:
 /** 沿共同状态门校验，只有绿段确实未满才能开始或提交。 */
 virtual bool ValidateUse() const override;
 /** 读取提交时体力缺额，避免前摇期间自然恢复造成超额支付。 */
 virtual void GatherEffectParameters(TMap<FGameplayTag,float>& Parameters) const override;
};
