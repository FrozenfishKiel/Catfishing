#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "CatAbilityCost.generated.h"

class UCatGameplayAbility;
struct FGameplayAbilityActorInfo;

/** 非属性资源的能力成本；参照 Lyra AdditionalCosts，只表达资源检查和支付，不执行道具效果。 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType)
class CATFISHING_API UCatAbilityCost : public UObject
{
	GENERATED_BODY()
public:
	/** 资源支付的只读资格检查；客户端数据只决定能否开始预测，服务器必须在提交点复核，检查本身不得扣量。 */
	virtual bool CheckCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* FailureTags) const PURE_VIRTUAL(UCatAbilityCost::CheckCost, return false;);
	/** 只支付已经预检的资源；不得从这里播放表现或施加 GE。 */
	virtual void ApplyCost(const UCatGameplayAbility* Ability, const FGameplayAbilityActorInfo* ActorInfo) const
		PURE_VIRTUAL(UCatAbilityCost::ApplyCost, );
};
