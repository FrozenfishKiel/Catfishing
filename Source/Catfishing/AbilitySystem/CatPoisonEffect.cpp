#include "AbilitySystem/CatPoisonEffect.h"

#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_Condition_PoisonDelta, "Cat.Data.Condition.PoisonDelta");

// 标签读取流程：把 Poison 的 SetByCaller 名称固定在 GE 类型旁边；外部只拿标签值，不重新声明字符串。
FGameplayTag UCatGE_PoisonDelta::GetPoisonDeltaTag()
{
	return TAG_Data_Condition_PoisonDelta;
}

// 构造流程：声明一个 Instant GameplayEffect，把 SetByCaller 幅度以 Additive 方式作用到 Character Survival Poison。
UCatGE_PoisonDelta::UCatGE_PoisonDelta()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetPoisonAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GetPoisonDeltaTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
