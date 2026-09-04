#include "AbilitySystem/Attributes/CatRunAttributeSet.h"

#include "Net/UnrealNetwork.h"

// 复制声明流程：先保留父类字段，再把 Run 的目标、进度和三个倍率都按 Always RepNotify 注册；复制层只同步权威事实，不派生阶段或事务结果。
void UCatRunAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, QuotaTarget, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, QuotaProgress, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, QuotaTargetMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, DailyPressure, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, SacrificeEfficiency, COND_None, REPNOTIFY_Always);
}

// 目标复制通知流程：把旧值交给 ASC，保证依赖目标额度的客户端属性委托能按 GAS 规则收敛。
void UCatRunAttributeSet::OnRep_QuotaTarget(const FGameplayAttributeData& OldQuotaTarget)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, QuotaTarget, OldQuotaTarget);
}

// 进度复制通知流程：把旧值交给 ASC，保留服务端允许的超额进度而不在客户端夹到目标。
void UCatRunAttributeSet::OnRep_QuotaProgress(const FGameplayAttributeData& OldQuotaProgress)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, QuotaProgress, OldQuotaProgress);
}

// 目标倍率复制通知流程：通知 GAS 的属性观察者；倍率本身不触发当前白天重新结算。
void UCatRunAttributeSet::OnRep_QuotaTargetMultiplier(const FGameplayAttributeData& OldQuotaTargetMultiplier)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, QuotaTargetMultiplier, OldQuotaTargetMultiplier);
}

// 日压力复制通知流程：通知 GAS 的属性观察者；压力只在下一次 StartDay 的捕获中生效。
void UCatRunAttributeSet::OnRep_DailyPressure(const FGameplayAttributeData& OldDailyPressure)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, DailyPressure, OldDailyPressure);
}

// 效率复制通知流程：通知 GAS 的属性观察者；实际贡献仍以服务器 GE 的投影为准。
void UCatRunAttributeSet::OnRep_SacrificeEfficiency(const FGameplayAttributeData& OldSacrificeEfficiency)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunAttributeSet, SacrificeEfficiency, OldSacrificeEfficiency);
}
