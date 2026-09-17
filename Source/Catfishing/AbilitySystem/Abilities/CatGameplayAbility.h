#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatGameplayAbility.generated.h"

class UCatAbilityCost;

/** 项目能力的公共执行约定；成本是策划可配置的对象，生命周期和预测仍由 GAS 管理。 */
UCLASS(Abstract)
class CATFISHING_API UCatGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	/** 每个授予拥有独立动作状态，默认允许本地预测启动；子类遇到不可预测的交互时可覆盖网络策略，资源成本仍由服务器确认。 */
	UCatGameplayAbility();
	/** 同时检查 GAS 属性成本和附加资源成本；只读检查不会消耗库存。 */
	virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** 提交已通过检查的成本；附加成本只允许服务器修改真实资源。 */
	virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) const override;
	/** 本能力额外支付的资源；程序提供成本类型，策划在能力资产上配置，不在背包中增加物品名单。 */
	UPROPERTY(EditDefaultsOnly, Instanced, Category="Costs", meta=(DisplayName="附加资源成本"))
	TArray<TObjectPtr<UCatAbilityCost>> AdditionalCosts;
};
