#include "AbilitySystem/Config/CatAbilitySet.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

bool UCatAbilitySet::IsRuntimeReady() const
{
	// 默认 AbilitySet 是 InputConfig 之后的第二道入口门禁：六个正式 Fishing 输入 Tag 必须都有可授予能力，
	// 非 Fishing 身体动作还必须有无输入的 BodyAction 事件网关，避免 UI/RPC 绕过 GAS 直接撞领域写口。
	if (GrantedAbilities.Num() < 7)
	{
		return false;
	}
	// 两个集合只在就绪门禁内去重：能力类不能重复授予，同一输入 Tag 也不能映射到多个技能。
	TSet<TSubclassOf<UGameplayAbility>> SeenAbilities;
	TSet<FGameplayTag> SeenInputTags;
	bool bHasBodyActionAbility = false;
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		if (!Entry.Ability || Entry.Level < 1 || SeenAbilities.Contains(Entry.Ability)
			|| (Entry.InputTag.IsValid() && SeenInputTags.Contains(Entry.InputTag)))
		{
			return false;
		}
		SeenAbilities.Add(Entry.Ability);
		if (Entry.Ability == UCatGA_BodyActionCommand::StaticClass())
		{
			// BodyAction 是 GameplayEvent 网关，不能占用 EnhancedInput Tag；这里保留默认触发策略只为了让授予记录可读、可校验。
			if (Entry.InputTag.IsValid() || Entry.ActivationPolicy != ECatAbilityActivationPolicy::OnInputTriggered)
			{
				return false;
			}
			bHasBodyActionAbility = true;
		}
		if (Entry.InputTag.IsValid())
		{
			// 按住型输入（收线 / 松开线杯 / 打窝蓄力）必须 WhileInputActive，其余离散输入必须 OnInputTriggered。
			const bool bHeldInput = Entry.InputTag == CatFishingAbilityTags::Input_Fishing_Primary
				|| Entry.InputTag == CatFishingAbilityTags::Input_Fishing_Slack
				|| Entry.InputTag == CatFishingAbilityTags::Input_Fishing_Chum;
			const ECatAbilityActivationPolicy ExpectedPolicy = bHeldInput
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
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Slack)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Cancel)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Scoop)
		&& SeenInputTags.Contains(CatFishingAbilityTags::Input_Fishing_Chum)
		&& bHasBodyActionAbility;
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
