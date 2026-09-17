#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Costs/CatAbilityCost.h"
#include "CatAbilityCost_Item.generated.h"

/** 消耗本次能力指定的精确物品，数量由该物品使用配置提供；永不查找同类替代品。 */
UCLASS(meta=(DisplayName="来源物品数量"))
class CATFISHING_API UCatAbilityCost_Item : public UCatAbilityCost
{
	GENERATED_BODY()
public:
	/** 读取冻结来源的当前数量；激活初期尚未收到目标时把领域预检留到提交点。 */
	virtual bool CheckCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* FailureTags) const override;
	/** 服务器消费精确实例并记录结果；真实库存自己维护幂等和复制通知。 */
	virtual void ApplyCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo) const override;
};
