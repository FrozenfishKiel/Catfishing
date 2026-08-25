#include "AbilitySystem/Fishing/InputAbilities/CatFishingScoopAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingScoop::UCatGA_FishingScoop()
{
	// 构造流程：只写入抄鱼技能 Tag，让独立的挥网输入能被 AbilitySystem 精确授予和激活。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Scoop));
}

void UCatGA_FishingScoop::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：丢弃事件负载，跳过服务器远端镜像，先播本地挥网表现，再提交一次抢抄命令并立即收尾。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitScoop().RequestId.IsValid());
}
