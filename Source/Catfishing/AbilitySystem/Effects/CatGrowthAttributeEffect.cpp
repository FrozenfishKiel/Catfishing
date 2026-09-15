#include "AbilitySystem/Effects/CatGrowthAttributeEffect.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_Growth_FishingStrengthDelta, "Cat.Data.Growth.FishingStrengthDelta");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_Growth_MaxFightStaminaDelta, "Cat.Data.Growth.MaxFightStaminaDelta");

// 标签读取流程：把 SetByCaller 名称固定在 GE 类型旁边；外部只拿标签值，不重新声明字符串。
FGameplayTag UCatGE_FishingStrengthDelta::GetFishingStrengthDeltaTag()
{
	return TAG_Data_Growth_FishingStrengthDelta;
}

// 构造流程：Instant GE，把 SetByCaller 幅度以 Additive 方式作用到 Character Survival FishingStrength。
UCatGE_FishingStrengthDelta::UCatGE_FishingStrengthDelta()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetFishingStrengthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GetFishingStrengthDeltaTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}

FGameplayTag UCatGE_MaxFightStaminaDelta::GetMaxFightStaminaDeltaTag()
{
	return TAG_Data_Growth_MaxFightStaminaDelta;
}

// 构造流程：Instant GE，把 SetByCaller 幅度以 Additive 方式作用到 MaxFightStamina；
// 当前体力的补满由 ASC 在同一次提交里按差值另行写入，属性集本身不会因为上限升高自动回满。
UCatGE_MaxFightStaminaDelta::UCatGE_MaxFightStaminaDelta()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GetMaxFightStaminaDeltaTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
