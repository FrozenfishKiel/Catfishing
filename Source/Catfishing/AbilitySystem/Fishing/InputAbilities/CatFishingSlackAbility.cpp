#include "AbilitySystem/Fishing/InputAbilities/CatFishingSlackAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingSlack::UCatGA_FishingSlack()
{
	// 构造流程：只写入放线技能 Tag，让右键输入可以通过 AbilitySystem 找到这一个按住型 Ability。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Slack));
}

void UCatGA_FishingSlack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：忽略触发负载，过滤服务器镜像，播放本地按下表现，再提交 SlackPressed 边沿并等待松开。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitSlackPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}

void UCatGA_FishingSlack::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 松开流程：本地表现先收势；若仍能提交命令，则发出 SlackReleased 让服务器清除持续放线输入。
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitSlackReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
