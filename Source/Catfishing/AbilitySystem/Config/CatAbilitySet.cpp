#include "AbilitySystem/Config/CatAbilitySet.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionAbilities.h"
#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionAbilities.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

bool UCatAbilitySet::IsRuntimeReady() const
{
	// 默认 AbilitySet 门禁流程：六个 Fishing 输入 Ability 与六个保留 BodyAction Ability 必须完整出现；
	// BodyAction 只承担 Camp/Social 表现和可取消前摇，库存、供品结算、偷鱼事务和 Wet 反馈不能通过 Ability 授予进入运行时。
	if (GrantedAbilities.Num() != 12)
	{
		return false;
	}
	// 这些集合只在就绪门禁内做局部校验：能力类和输入 Tag 去重，BodyAction 还要精确命中六个专用事件 Ability。
	TSet<TSubclassOf<UGameplayAbility>> SeenAbilities;
	TSet<FGameplayTag> SeenInputTags;
	TSet<TSubclassOf<UGameplayAbility>> ExpectedBodyActionAbilities = {
		UCatGA_BodyActionCampRest::StaticClass(), UCatGA_BodyActionCampfirePlayback::StaticClass(),
		UCatGA_BodyActionRescueCharacterToCamp::StaticClass(),
		UCatGA_BodyActionRequestManualHelp::StaticClass(), UCatGA_BodyActionRequestMischief::StaticClass(),
		UCatGA_BodyActionPlaceProtectionSign::StaticClass() };
	TSet<TSubclassOf<UGameplayAbility>> SeenBodyActionAbilities;
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		if (!Entry.Ability || Entry.Level < 1 || SeenAbilities.Contains(Entry.Ability)
			|| (Entry.InputTag.IsValid() && SeenInputTags.Contains(Entry.InputTag)))
		{
			return false;
		}
		SeenAbilities.Add(Entry.Ability);
		if (ExpectedBodyActionAbilities.Contains(Entry.Ability))
		{
			// 每个 BodyAction 条目是事件专用 Ability，不能占用 EnhancedInput Tag；保留离散策略只让授予记录与输入组件契约一致。
			if (Entry.InputTag.IsValid() || Entry.ActivationPolicy != ECatAbilityActivationPolicy::OnInputTriggered)
			{
				return false;
			}
			SeenBodyActionAbilities.Add(Entry.Ability);
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
		&& SeenBodyActionAbilities.Num() == ExpectedBodyActionAbilities.Num();
}

bool UCatAbilitySet::GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
	FCatGrantedAbilitySetHandles& OutGrantedHandles) const
{
	// 授予流程：先确认服务器 ASC、默认配置完整且输出句柄为空；随后逐条创建 Spec、登记输入标签和可选初始效果。
	// 任一 Ability 或 Effect 授予失败都会撤销本轮已写入句柄，避免角色留下半套输入能力或保留 BodyAction 事件入口。
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
	// 撤销流程：只在服务器 ASC 上按记录句柄反注册输入、清除 Ability 和初始效果；完成后清空本集合，重复调用不会再触碰失效句柄。
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
