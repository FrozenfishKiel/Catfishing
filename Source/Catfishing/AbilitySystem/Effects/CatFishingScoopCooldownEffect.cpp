#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UCatGE_FishingScoopCooldown::UCatGE_FishingScoopCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.0f));

	FInheritedTagContainer CooldownTags;
	CooldownTags.AddTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("ScoopCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
