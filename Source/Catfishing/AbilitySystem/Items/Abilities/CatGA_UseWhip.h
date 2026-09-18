#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_UseWhip.generated.h"
class ACatWhipActor;

/** 可重复使用的皮鞭；请求、预测蒙太奇和精确来源成本沿用物品 GA，物理命中只在服务器提交。 */
UCLASS()
class CATFISHING_API UCatGA_UseWhip : public UCatItemGameplayAbility
{
    GENERATED_BODY()
public:
    virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
    virtual void CommitUse() override;
private:
    UPROPERTY() TObjectPtr<ACatWhipActor> SwingActor;
    FTimerHandle FinishTimer;
    UFUNCTION() void FinishSwing();
};
