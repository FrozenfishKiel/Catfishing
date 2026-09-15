#include "AbilitySystem/Effects/CatFishingStaminaEffect.h"

#include "NativeGameplayTags.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"

UCatGE_FishingStaminaDelta::UCatGE_FishingStaminaDelta()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetFightStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = CatFishingAbilityTags::Data_Fishing_FightStaminaDelta;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	FGameplayModifierInfo& Yellow = Modifiers.AddDefaulted_GetRef();
	Yellow.Attribute = UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute();
	Yellow.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat YellowCaller;
	YellowCaller.DataTag = GetYellowDeltaTag();
	Yellow.ModifierMagnitude = FGameplayEffectModifierMagnitude(YellowCaller);
}

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_YellowFightStaminaDelta, "Cat.Data.Fishing.YellowFightStaminaDelta");

FGameplayTag UCatGE_FishingStaminaDelta::GetYellowDeltaTag()
{
	return TAG_Data_YellowFightStaminaDelta;
}

UCatGE_YellowFightStaminaDelta::UCatGE_YellowFightStaminaDelta()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = UCatGE_FishingStaminaDelta::GetYellowDeltaTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
