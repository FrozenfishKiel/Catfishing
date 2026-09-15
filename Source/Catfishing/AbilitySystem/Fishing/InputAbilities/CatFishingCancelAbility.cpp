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
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo)) return;
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands || !Commands->SubmitCancel().RequestId.IsValid())
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
}

void UCatGA_FishingCancel::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	BP_OnLocalInputReleased();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
void UCatGA_FishingCancel::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 松手、硬直取消、菜单／失焦清理都提交释放，客户端不自行确认 1.5 秒。
	if (CanSubmitLocalCommand(ActorInfo))
		if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo)) Commands->SubmitCancelReleased();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
