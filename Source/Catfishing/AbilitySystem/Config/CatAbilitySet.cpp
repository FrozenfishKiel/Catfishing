#include "AbilitySystem/Config/CatAbilitySet.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

bool UCatAbilitySet::IsRuntimeReady() const
{
	// 通用门禁流程：只检查配置能否授予；角色和装备共用同一规则，不限制具体能力类或集合数量。
	if (GrantedAbilities.IsEmpty() && GrantedEffects.IsEmpty())
	{
		return false;
	}
	// 集合配置只校验能否授予，不规定能力数量、具体类或输入标签的唯一性；组合准入由 GA 的 Tag 条件决定。
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		if (!Entry.Ability || Entry.Level < 1) return false;
	}
	for (const FCatAbilitySetGameplayEffect& Entry : GrantedEffects)
	{
		// 来源句柄只能安全回收 Duration/Infinite 效果；Instant 不产生可回收状态，拒绝它避免部分提交无法回滚。
		const UGameplayEffect* Effect = Entry.GameplayEffect ? Entry.GameplayEffect->GetDefaultObject<UGameplayEffect>() : nullptr;
		if (!Effect || Entry.Level < 1 || Effect->DurationPolicy == EGameplayEffectDurationType::Instant
			|| Effect->StackingType != EGameplayEffectStackingType::None) return false;
	}
	return true;
}

bool UCatAbilitySet::GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
	FCatGrantedAbilitySetHandles& OutGrantedHandles, UObject* SourceObject) const
{
	// 授予流程：先确认服务器 ASC、本集合配置完整且输出句柄为空；随后逐条创建带来源的 Spec、登记输入标签并施加独立效果。
	// 任一 Ability 或 Effect 授予失败都会撤销本轮已写入句柄，避免角色留下半套输入能力或保留 BodyAction 事件入口。
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative() || !IsRuntimeReady()
		|| OutGrantedHandles.HasAnyGrantedHandle())
	{
		return false;
	}

	bool bGrantedAny = false;
	TArray<FGameplayAbilitySpecHandle> OnGrantedHandles;
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		FGameplayAbilitySpec Spec(Entry.Ability, Entry.Level);
		Spec.SourceObject = SourceObject;
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
		bGrantedAny = true;


		if (Entry.ActivationPolicy == ECatAbilityActivationPolicy::OnGranted) OnGrantedHandles.Add(Handle);
	}
	for (const FCatAbilitySetGameplayEffect& Entry : GrantedEffects)
	{
		if (!Entry.GameplayEffect || Entry.Level < 1) { OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem); return false; }
		FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
		Context.AddSourceObject(SourceObject);
		const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(Entry.GameplayEffect, Entry.Level, Context);
		if (!Spec.IsValid()) { OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem); return false; }
		const FActiveGameplayEffectHandle Handle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		if (!Handle.IsValid()) { OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem); return false; }
		OutGrantedHandles.GameplayEffectHandles.Add(Handle);
		bGrantedAny = true;
	}
	// 所有可回收写入成功后才启动被动 Ability；后项失败时不会留下已经执行外部副作用的半套集合。
	for (const FGameplayAbilitySpecHandle Handle : OnGrantedHandles)
	{
		if (!AbilitySystem->TryActivateAbility(Handle)) { OutGrantedHandles.TakeFromAbilitySystem(AbilitySystem); return false; }
	}
	return bGrantedAny;
}

// 合并流程：将本次已成功授予的句柄转移给来源持有者；移动后临时集合不再拥有回收责任。
void FCatGrantedAbilitySetHandles::Append(FCatGrantedAbilitySetHandles&& Other)
{
	AbilitySpecHandles.Append(MoveTemp(Other.AbilitySpecHandles));
	GameplayEffectHandles.Append(MoveTemp(Other.GameplayEffectHandles));
}

void FCatGrantedAbilitySetHandles::TakeFromAbilitySystem(UCatAbilitySystemComponent* AbilitySystem)
{
	// 撤销流程：只在权威 ASC 上处理来源句柄；先转移所有权，再清除能力和效果；输入边沿由 ASC 的 OnRemoveAbility 回收。
	// ClearAbility 会同步结束能力并可能重入物品清理；回调此时看到空集合，不会修改本次遍历或重复撤销。
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return;
	}
	const TArray<FGameplayAbilitySpecHandle> AbilitiesToRemove = MoveTemp(AbilitySpecHandles);
	const TArray<FActiveGameplayEffectHandle> EffectsToRemove = MoveTemp(GameplayEffectHandles);
	for (const FGameplayAbilitySpecHandle Handle : AbilitiesToRemove)
	{
		AbilitySystem->ClearAbility(Handle);
	}
	for (const FActiveGameplayEffectHandle Handle : EffectsToRemove)
	{
		// 授予前要求 StackingType=None，每个句柄都是本来源独立效果，回收不会影响其他装备。
		AbilitySystem->RemoveActiveGameplayEffect(Handle);
	}
}
