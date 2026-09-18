#include "AbilitySystem/Items/Abilities/CatGA_RestoreStamina.h"
#include "AbilitySystem/Effects/CatItemEffects.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystemComponent.h"

// 使用校验流程：保留共同来源和搏斗约束，再读取 GAS 绿段缺额；黄色储备不影响能否补绿段。
bool UCatGA_RestoreStamina::ValidateUse() const
{
 const auto* ASC = GetAbilitySystemComponentFromActorInfo();
 return Super::ValidateUse() && ASC
  && ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())
   < ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
}
// 参数流程：复制策划参数，再用当前真实缺额覆盖恢复量；对应 GE 只消费这一数值。
void UCatGA_RestoreStamina::GatherEffectParameters(TMap<FGameplayTag,float>& Parameters) const
{
 Super::GatherEffectParameters(Parameters);
 const auto* ASC = GetAbilitySystemComponentFromActorInfo();
 Parameters.Add(CatItemEffectTags::RestoreAmount, ASC ? FMath::Max(0.f,
  ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute())
  - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())) : 0.f);
}
