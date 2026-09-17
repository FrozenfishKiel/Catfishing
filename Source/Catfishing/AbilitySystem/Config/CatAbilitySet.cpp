#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/Items/Abilities/CatGA_ConsumeFish.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionAbilities.h"
#include "AbilitySystem/BodyAction/CatCancelBodyActionAbility.h"
#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionAbilities.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

bool UCatAbilitySet::IsRuntimeReady() const
{
	// 通用门禁流程：装备操作集只要求条目自身可授予，角色默认集合的内容由专用校验另行约束。
	if (GrantedAbilities.IsEmpty() && GrantedEffects.IsEmpty())
	{
		return false;
	}
	// 这些临时集合只检查本配置内的能力类和输入标签是否重复，不要求装备集合包含角色默认身体动作。
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
		if (Entry.InputTag.IsValid()) SeenInputTags.Add(Entry.InputTag);
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

bool UCatAbilitySet::IsDefaultCharacterAbilitySetReady() const
{
	// 默认集合校验流程：角色保留四项 BodyAction、无竿也可用的 X 取消及共享来源进食；钓竿操作能力必须移出此集合。
	if (!IsRuntimeReady() || GrantedAbilities.Num() != 6) return false;
	TSet<TSubclassOf<UGameplayAbility>> Expected = { UCatGA_BodyActionCampfirePlayback::StaticClass(),
		UCatGA_BodyActionRequestManualHelp::StaticClass(), UCatGA_BodyActionRequestMischief::StaticClass(), UCatGA_BodyActionPlaceProtectionSign::StaticClass(), UCatGA_CancelBodyAction::StaticClass(), UCatGA_ConsumeFish::StaticClass() };
	for (const FCatAbilitySetAbility& Entry : GrantedAbilities)
	{
		const bool bCancel = Entry.Ability == UCatGA_CancelBodyAction::StaticClass();
		if (!Expected.Contains(Entry.Ability) || Entry.ActivationPolicy != ECatAbilityActivationPolicy::OnInputTriggered
			|| (bCancel ? Entry.InputTag != CatFishingAbilityTags::Input_Fishing_Cancel : Entry.InputTag.IsValid())) return false;
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
		AbilitySystem->RegisterAbilityInput(Handle, Entry.InputTag, Entry.ActivationPolicy);
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
	// 撤销流程：只在权威 ASC 上处理来源句柄；先转移所有权，再反注册输入、清除能力和效果。
	// ClearAbility 会同步结束能力并可能重入物品清理；回调此时看到空集合，不会修改本次遍历或重复撤销。
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return;
	}
	const TArray<FGameplayAbilitySpecHandle> AbilitiesToRemove = MoveTemp(AbilitySpecHandles);
	const TArray<FActiveGameplayEffectHandle> EffectsToRemove = MoveTemp(GameplayEffectHandles);
	for (const FGameplayAbilitySpecHandle Handle : AbilitiesToRemove)
	{
		AbilitySystem->UnregisterAbilityInput(Handle);
		AbilitySystem->ClearAbility(Handle);
	}
	for (const FActiveGameplayEffectHandle Handle : EffectsToRemove)
	{
		// 授予前要求 StackingType=None，每个句柄都是本来源独立效果，回收不会影响其他装备。
		AbilitySystem->RemoveActiveGameplayEffect(Handle);
	}
}
