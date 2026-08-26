#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingChum::UCatGA_FishingChum()
{
	// 构造流程：只写入打窝技能 Tag，让 Q 键输入和 AbilitySet 授予规则共享同一个能力身份。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Chum));
}

void UCatGA_FishingChum::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：过滤服务器镜像，播放本地蓄力开始表现，提交 ChumPressed；成功后保持实例等待松开。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitChumPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}

void UCatGA_FishingChum::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 松开流程：播放本地收势；若命令通道仍有效，则让服务器按按住时长裁决打窝投放结果。
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitChumReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
