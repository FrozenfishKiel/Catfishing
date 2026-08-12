#include "AbilitySystem/CatSurvivalAttributeSet.h"

#include "Net/UnrealNetwork.h"

// 复制声明流程：先保留父类字段，再为五个局内属性全部注册无条件、Always RepNotify；每项都经 GAS 标准宏收敛基值和属性 delegate，复制层不夹带阈值或表现裁决。
void UCatSurvivalAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Hunger, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Fatigue, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FishingStrength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FightStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Poison, COND_None, REPNOTIFY_Always);
}

// Hunger 复制通知流程：把属性类型、当前值和旧值交给 GAS 标准宏，让 ActiveGameplayEffects 更新客户端基值并广播对应属性 delegate。
void UCatSurvivalAttributeSet::OnRep_Hunger(const FGameplayAttributeData& OldHunger)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Hunger, OldHunger);
}

// Fatigue 复制通知流程：复用 GAS 标准 RepNotify 收敛预测与属性 delegate；不在客户端回写服务器真相或触发临时数值规则。
void UCatSurvivalAttributeSet::OnRep_Fatigue(const FGameplayAttributeData& OldFatigue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Fatigue, OldFatigue);
}

// FishingStrength 复制通知流程：把旧基值交给 ASC，使客户端属性 delegate 与服务器最终力量收敛；不派生多人合力结论。
void UCatSurvivalAttributeSet::OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FishingStrength, OldFishingStrength);
}

// FightStamina 复制通知流程：使用标准 RepNotify 更新短周期搏斗体力；日常 Fatigue 始终保留独立字段和生命周期。
void UCatSurvivalAttributeSet::OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FightStamina, OldFightStamina);
}

// Poison 复制通知流程：使用标准 RepNotify 更新中毒累积的客户端读模型；客户端不自行判断倒地、恢复或死亡。
void UCatSurvivalAttributeSet::OnRep_Poison(const FGameplayAttributeData& OldPoison)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Poison, OldPoison);
}
