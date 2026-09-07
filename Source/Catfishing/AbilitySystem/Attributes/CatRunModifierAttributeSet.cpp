#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"

#include "Net/UnrealNetwork.h"

namespace
{
	// 来源倍率规整流程：Run 公式只接受有限且非负的输入；0 会让调用方 fail-closed，而不是悄悄套默认值继续推进。
	float ClampRunModifierValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	// 属性命中流程：只处理本来源属性集拥有的三项倍率，避免误夹其他 AttributeSet 的同名或未来字段。
	bool IsRunModifierAttribute(const FGameplayAttribute& Attribute)
	{
		return Attribute == UCatRunModifierAttributeSet::GetQuotaTargetMultiplierAttribute()
			|| Attribute == UCatRunModifierAttributeSet::GetDailyPressureAttribute()
			|| Attribute == UCatRunModifierAttributeSet::GetSacrificeEfficiencyAttribute();
	}
}

// 复制声明流程：三项来源倍率都使用 Always RepNotify，让 UI 或调试面板能按 GAS 标准委托观察当前 Run 公式输入。
void UCatRunModifierAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunModifierAttributeSet, QuotaTargetMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunModifierAttributeSet, DailyPressure, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunModifierAttributeSet, SacrificeEfficiency, COND_None, REPNOTIFY_Always);
}

// 基础值变化流程：GE 或初始化覆盖来源倍率前先清除非有限值和负数，确保后续 ExecCalc 不会把坏输入写入最终额度。
void UCatRunModifierAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (IsRunModifierAttribute(Attribute))
	{
		NewValue = ClampRunModifierValue(NewValue);
	}
}

// 当前值变化流程：来源 GE 叠加后的聚合值同样不能越过非负有限约束；是否能结算仍交给具体 ExecCalc 判断。
void UCatRunModifierAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (IsRunModifierAttribute(Attribute))
	{
		NewValue = ClampRunModifierValue(NewValue);
	}
}

// 目标倍率复制通知流程：把旧值交给 ASC，保持目标倍率观察者与 GAS 预测/复制收敛一致。
void UCatRunModifierAttributeSet::OnRep_QuotaTargetMultiplier(const FGameplayAttributeData& OldQuotaTargetMultiplier)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunModifierAttributeSet, QuotaTargetMultiplier, OldQuotaTargetMultiplier);
}

// 日压力复制通知流程：把旧值交给 ASC；Run 阶段、截止时间和公开 DTO 仍只由服务器 GameMode 投影。
void UCatRunModifierAttributeSet::OnRep_DailyPressure(const FGameplayAttributeData& OldDailyPressure)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunModifierAttributeSet, DailyPressure, OldDailyPressure);
}

// 献祭效率复制通知流程：把旧值交给 ASC；客户端只观察倍率变化，不发起本地额度补偿。
void UCatRunModifierAttributeSet::OnRep_SacrificeEfficiency(const FGameplayAttributeData& OldSacrificeEfficiency)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatRunModifierAttributeSet, SacrificeEfficiency, OldSacrificeEfficiency);
}
