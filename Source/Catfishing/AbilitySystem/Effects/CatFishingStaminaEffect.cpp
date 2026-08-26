#include "AbilitySystem/Effects/CatFishingStaminaEffect.h"

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
}
