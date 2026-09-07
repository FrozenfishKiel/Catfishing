#include "AbilitySystem/Attributes/CatRunAttributeSet.h"

#include "Net/UnrealNetwork.h"

namespace
{
	// 最终额度规整流程：目标和进度只要求有限且非负；进度不夹到目标，保留超额献祭的公开语义。
	float ClampRunQuotaValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	// 属性命中流程：只处理最终 Run 属性集拥有的目标和进度，来源倍率由独立 AttributeSet 约束。
	bool IsRunQuotaAttribute(const FGameplayAttribute& Attribute)
	{
		return Attribute == UCatRunAttributeSet::GetQuotaTargetAttribute()
			|| Attribute == UCatRunAttributeSet::GetQuotaProgressAttribute();
	}
}

// 复制声明流程：先保留父类字段，再把 Run 的目标和进度按 Always RepNotify 注册；复制层只同步权威事实，不派生阶段或事务结果。
void UCatRunAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, QuotaTarget, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatRunAttributeSet, QuotaProgress, COND_None, REPNOTIFY_Always);
}

// 基础值变化流程：GE 覆盖目标或进度前先清除非有限值和负数，防止 RunPublicState 投影到不可复制的坏状态。
void UCatRunAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (IsRunQuotaAttribute(Attribute))
	{
		NewValue = ClampRunQuotaValue(NewValue);
	}
}

// 当前值变化流程：聚合值同样保持有限非负；是否达标仍由 GameMode 读取投影后按 Run 事务规则处理。
void UCatRunAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (IsRunQuotaAttribute(Attribute))
	{
		NewValue = ClampRunQuotaValue(NewValue);
	}
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
