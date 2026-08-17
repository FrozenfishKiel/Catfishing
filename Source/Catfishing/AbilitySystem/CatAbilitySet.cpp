#include "AbilitySystem/CatAbilitySet.h"

#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

bool UCatAbilitySet::IsRuntimeReady() const
{
	if (GrantedAbilities.Num() < 5)
	{
		return false;
	}
	TSet<TSubclassOf<UGameplayAbility>> SeenAbilities;
	TSet<FGameplayTag> SeenInputTags;
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		if (!Entry.Ability || Entry.Level < 1 || SeenAbilities.Contains(Entry.Ability)
			|| (Entry.InputTag.IsValid() && SeenInputTags.Contains(Entry.InputTag)))
		{
			return false;
		}
		SeenAbilities.Add(Entry.Ability);
		if (Entry.InputTag.IsValid())
		{
			const ECatAbilityActivationPolicy ExpectedPolicy = Entry.InputTag == CatFishingAbilityTags::Input_Fishing_Primary
				? ECatAbilityActivationPolicy::WhileInputActive : ECatAbilityActivationPolicy::OnInputTriggered;
			if (Entry.ActivationPolicy != ExpectedPolicy)
			{
				return false;
			}
			SeenInputTags.Add(Entry.InputTag);
		}
	}
	return SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_RodInteract)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Primary)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Cancel)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Scoop)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Chum);
}

bool UCatAbilitySet::GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
	FCatGrantedAbilitySetHandles& OutGrantedHandles) const
{
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative() || !IsRuntimeReady()
		|| !OutGrantedHandles.AbilitySpecHandles.IsEmpty())
	{
		return false;
	}

	bool bGrantedAny = false;
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		FGameplayAbilitySpec Spec(Entry.Ability, Entry.Level);
		if (Entry.InputTag.IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
		}
		switch (Entry.ActivationPolicy)
		{
		case ECatAbilityActivationPolicy::WhileInputActive:
			Spec.GetDynamicSpecSourceTags().AddTag(CatFishingAbilityTags::Ability_ActivationPolicy_WhileInputActive);
			break;
		case ECatAbilityActivationPolicy::OnGranted:
			Spec.GetDynamicSpecSourceTags().AddTag(CatFishingAbilityTags::Ability_ActivationPolicy_OnGranted);
			break;
		default:
			Spec.GetDynamicSpecSourceTags().AddTag(CatFishingAbilityTags::Ability_ActivationPolicy_OnInputTriggered);
			break;
		}
		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(Spec);
		if (!Handle.IsValid())
		{
			OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem);
			return false;
		}
		OutGrantedHandles.AbilitySpecHandles.Add(Handle);
		AbilitySystem->RegisterAbilityInput(Handle, Entry.InputTag, Entry.ActivationPolicy);
		bGrantedAny = true;

		if (Entry.InitialEffect)
		{
			const FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
			const FGameplayEffectSpecHandle EffectSpec = AbilitySystem->MakeOutgoingSpec(Entry.InitialEffect, Entry.Level, Context);
			if (EffectSpec.IsValid())
			{
				const FActiveGameplayEffectHandle EffectHandle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*EffectSpec.Data.Get());
				if (EffectHandle.IsValid())
				{
					OutGrantedHandles.GameplayEffectHandles.Add(EffectHandle);
				}
			}
			else
			{
				OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem);
				return false;
			}
		}

		if (Entry.ActivationPolicy == ECatAbilityActivationPolicy::OnGranted)
		{
			AbilitySystem->TryActivateAbility(Handle);
		}
	}
	return bGrantedAny;
}

void FCatGrantedAbilitySetHandles::TakeFromAbilitySystem(UCatAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle Handle : AbilitySpecHandles)
	{
		AbilitySystem->UnregisterAbilityInput(Handle);
		AbilitySystem->ClearAbility(Handle);
	}
	for (const FActiveGameplayEffectHandle Handle : GameplayEffectHandles)
	{
		AbilitySystem->RemoveActiveGameplayEffect(Handle);
	}
	AbilitySpecHandles.Reset();
	GameplayEffectHandles.Reset();
}
