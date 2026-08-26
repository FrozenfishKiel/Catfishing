#include "AbilitySystem/Fishing/InputAbilities/CatFishingScoopAbility.h"

#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingScoop::UCatGA_FishingScoop()
{
	// 构造流程：只写入抄鱼技能 Tag，让独立的挥网输入能被 AbilitySystem 精确授予和激活。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Scoop));
	CooldownGameplayEffectClass = UCatGE_FishingScoopCooldown::StaticClass();
}

void UCatGA_FishingScoop::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：先 Commit 预测/权威冷却，再过滤服务器远端镜像；本地端播放挥网表现并提交一次抢抄命令。
	(void)TriggerEventData;
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitScoop().RequestId.IsValid());
}

void UCatGA_FishingScoop::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	double CooldownSeconds = 0.0;
	if (!GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(CooldownSeconds))
	{
		return;
	}
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));
	if (Spec.IsValid())
	{
		Spec.Data->SetDuration(static_cast<float>(CooldownSeconds), true);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
}
