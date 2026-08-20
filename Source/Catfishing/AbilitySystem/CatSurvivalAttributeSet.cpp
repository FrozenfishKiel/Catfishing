#include "AbilitySystem/CatSurvivalAttributeSet.h"

#include "Net/UnrealNetwork.h"

// 复制声明流程：先保留父类字段，再为三个局内属性全部注册无条件、Always RepNotify；每项都经 GAS 标准宏收敛基值和属性
// delegate，复制层不夹带阈值或表现裁决。
void UCatSurvivalAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FishingStrength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FightStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Poison, COND_None, REPNOTIFY_Always);
}

// FishingStrength 复制通知流程：把旧基值交给 ASC，使客户端属性 delegate 与服务器最终力量收敛；不派生多人合力结论。
void UCatSurvivalAttributeSet::OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FishingStrength, OldFishingStrength);
}

// FightStamina 复制通知流程：使用标准 RepNotify 更新短周期搏斗体力；它只在一次搏斗内消耗，不代表日常疲惫。
void UCatSurvivalAttributeSet::OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FightStamina, OldFightStamina);
}

// Poison 复制通知流程：使用标准 RepNotify 更新中毒累积的客户端读模型；客户端不自行判断倒地、恢复或死亡。
void UCatSurvivalAttributeSet::OnRep_Poison(const FGameplayAttributeData& OldPoison)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Poison, OldPoison);
}
