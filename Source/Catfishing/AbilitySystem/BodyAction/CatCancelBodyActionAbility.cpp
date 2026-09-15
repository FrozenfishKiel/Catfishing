#include "AbilitySystem/BodyAction/CatCancelBodyActionAbility.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"

UCatGA_CancelBodyAction::UCatGA_CancelBodyAction()
{
	// 构造流程：取消只由服务器执行；不添加 BodyAction 标签，确保 CancelBodyActionAbilities 不会递归取消本 Ability。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UCatGA_CancelBodyAction::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：只读取当前项目 ASC 并取消已激活的 BodyAction；无 ASC 时保持无副作用，最后结束本次一次性输入能力。
	(void)TriggerEventData;
	if (UCatAbilitySystemComponent* ASC = Cast<UCatAbilitySystemComponent>(ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr))
	{
		ASC->CancelBodyActionAbilitiesFromAuthority();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
