#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// 非负身体数值规整流程：非法值直接归零，合法值只移除负数；更严格的运行就绪判断仍留给配置和会话入口。
	float ClampSurvivalNonNegativeValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	// 体力上限规整流程：MaxFightStamina 是搏斗恢复和模拟的硬上限，不能低于 1，避免后续除法和会话配置出现无意义零上限。
	float ClampMaxFightStaminaValue(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(1.0f, Value) : 1.0f;
	}

	// 当前体力规整流程：FightStamina 是短周期消耗值，必须保持在 0 到当前上限之间；上限缺失时归零并让会话入口 fail-closed。
	float ClampFightStaminaValue(const float Value, const float MaxValue)
	{
		if (!FMath::IsFinite(Value))
		{
			return 0.0f;
		}
		const float EffectiveMax = FMath::IsFinite(MaxValue) && MaxValue > 0.0f ? MaxValue : 0.0f;
		return FMath::Clamp(Value, 0.0f, EffectiveMax);
	}
}

// 复制声明流程：先保留父类字段，再为当前仍是玩法真相的身体/搏斗属性注册无条件、Always RepNotify；复制层不夹带阈值、成长或表现裁决。
void UCatSurvivalAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FishingStrength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, FightStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, MaxFightStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatSurvivalAttributeSet, Poison, COND_None, REPNOTIFY_Always);
}

// 基础值变化流程：配置播种或 GE 覆盖属性前统一清理坏数值；当前体力读取已经存在的 MaxFightStamina，所以上层必须先写上限再回满体力。
void UCatSurvivalAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (Attribute == GetFightStaminaAttribute())
	{
		NewValue = ClampFightStaminaValue(NewValue, GetMaxFightStamina());
	}
	else if (Attribute == GetMaxFightStaminaAttribute())
	{
		NewValue = ClampMaxFightStaminaValue(NewValue);
	}
	else if (Attribute == GetFishingStrengthAttribute() || Attribute == GetPoisonAttribute())
	{
		NewValue = ClampSurvivalNonNegativeValue(NewValue);
	}
}

// 当前值变化流程：聚合后的运行值同样维持非负、有限和体力不超过上限；具体命令失败仍由 ASC/Session 根据上下文判断。
void UCatSurvivalAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetFightStaminaAttribute())
	{
		NewValue = ClampFightStaminaValue(NewValue, GetMaxFightStamina());
	}
	else if (Attribute == GetMaxFightStaminaAttribute())
	{
		NewValue = ClampMaxFightStaminaValue(NewValue);
	}
	else if (Attribute == GetFishingStrengthAttribute() || Attribute == GetPoisonAttribute())
	{
		NewValue = ClampSurvivalNonNegativeValue(NewValue);
	}
}

// 上限变化收口流程：MaxFightStamina 降低时用 ASC 标准 Override 把当前体力压回新上限；上限升高不会自动回满，仍由会话重置入口显式处理。
void UCatSurvivalAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, const float OldValue,
	const float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	(void)OldValue;
	if (Attribute == GetMaxFightStaminaAttribute() && GetFightStamina() > NewValue)
	{
		if (UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent())
		{
			AbilitySystem->ApplyModToAttribute(GetFightStaminaAttribute(), EGameplayModOp::Override, NewValue);
		}
	}
}

// FishingStrength 复制通知流程：把旧基值交给 ASC，使客户端属性 delegate 与服务器最终力量收敛；不派生多人合力结论。
void UCatSurvivalAttributeSet::OnRep_FishingStrength(const FGameplayAttributeData& OldFishingStrength)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FishingStrength, OldFishingStrength);
}

// FightStamina 复制通知流程：使用标准 RepNotify 更新短周期搏斗体力；疲惫已经退为表现层，不在 ASC 里参与数值同步。
void UCatSurvivalAttributeSet::OnRep_FightStamina(const FGameplayAttributeData& OldFightStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, FightStamina, OldFightStamina);
}

// MaxFightStamina 复制通知流程：使用标准 RepNotify 更新搏斗体力上限；显示层和接力会话都只观察 ASC 的同一份上限。
void UCatSurvivalAttributeSet::OnRep_MaxFightStamina(const FGameplayAttributeData& OldMaxFightStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, MaxFightStamina, OldMaxFightStamina);
}

// Poison 复制通知流程：使用标准 RepNotify 更新中毒累积的客户端读模型；客户端不自行判断倒地、恢复或死亡。
void UCatSurvivalAttributeSet::OnRep_Poison(const FGameplayAttributeData& OldPoison)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatSurvivalAttributeSet, Poison, OldPoison);
}
