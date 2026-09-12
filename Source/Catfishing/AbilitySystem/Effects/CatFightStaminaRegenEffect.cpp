#include "AbilitySystem/Effects/CatFightStaminaRegenEffect.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_Fishing_FightStaminaRegenPerPeriod, "Cat.Data.Fishing.FightStaminaRegenPerPeriod");

// 标签读取流程：把每周期补体点数的 SetByCaller 名称固定在 GE 类型旁边；外部只拿标签值，不重新声明字符串。
FGameplayTag UCatGE_FightStaminaRegen::GetRegenPerPeriodTag()
{
	return TAG_Data_Fishing_FightStaminaRegenPerPeriod;
}

// 构造流程：声明一个 Infinite + Period 的 GameplayEffect，把每周期点数以 Additive 方式作用到 FightStamina。
// 上限夹取仍归 UCatSurvivalAttributeSet 的 PreAttributeChange/PreAttributeBaseChange，这里不再写第二份夹取。
// 不在应用瞬间执行一次，避免反复开关恢复时凭开关次数白拿体力。
UCatGE_FightStaminaRegen::UCatGE_FightStaminaRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(PeriodSeconds);
	bExecutePeriodicEffectOnApplication = false;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetFightStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GetRegenPerPeriodTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
