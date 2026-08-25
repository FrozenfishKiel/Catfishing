#include "AbilitySystem/Fishing/InputAbilities/CatFishingCancelAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingCancel::UCatGA_FishingCancel()
{
	// 构造流程：只写入取消技能 Tag，让 Cancel 输入和 AbilitySet 授予规则共享同一个稳定身份。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Cancel));
}

void UCatGA_FishingCancel::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：丢弃事件负载，跳过服务器远端镜像，在本地播放取消表现，然后提交一次权威取消命令。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitCancel().RequestId.IsValid());
}
