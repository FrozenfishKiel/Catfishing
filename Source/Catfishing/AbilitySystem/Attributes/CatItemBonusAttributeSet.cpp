#include "AbilitySystem/Attributes/CatItemBonusAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

// 复制流程：沿父类注册，再复制三项 GE 聚合属性；客户端不自行生成或消费加成。
void UCatItemBonusAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION_NOTIFY(ThisClass, RareFishWeightMultiplier, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(ThisClass, DropChanceBonus, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(ThisClass, StaminaCostMultiplier, COND_None, REPNOTIFY_Always);
}
// 权重复制流程：把旧值交给 GAS，让基值与聚合值按标准路径收敛。
void UCatItemBonusAttributeSet::OnRep_RareFishWeightMultiplier(const FGameplayAttributeData& OldValue)
{ GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, RareFishWeightMultiplier, OldValue); }
// 掉落复制流程：只更新 GAS 观察，不在回调里重新抽取奖励。
void UCatItemBonusAttributeSet::OnRep_DropChanceBonus(const FGameplayAttributeData& OldValue)
{ GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, DropChanceBonus, OldValue); }
// 减耗复制流程：只更新 GAS 观察，不重复支付服务器已经结算的体力。
void UCatItemBonusAttributeSet::OnRep_StaminaCostMultiplier(const FGameplayAttributeData& OldValue)
{ GAMEPLAYATTRIBUTE_REPNOTIFY(ThisClass, StaminaCostMultiplier, OldValue); }
