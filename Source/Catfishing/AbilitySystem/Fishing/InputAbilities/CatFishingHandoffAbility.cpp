#include "AbilitySystem/Fishing/InputAbilities/CatFishingHandoffAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingHandoff::UCatGA_FishingHandoff()
{
	// 构造流程：只写入该技能对应的 GameplayTag，供 AbilitySet 授予后由输入层按 Tag 查找并激活。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Handoff));
}

void UCatGA_FishingHandoff::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：丢弃无用事件负载，跳过服务器远端镜像，在本地播放互动表现，再提交一次换人命令并按结果收尾。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		// 命令由 owning client 的 CommandComponent RPC 单独提交；服务器镜像只负责配对本次预测生命周期。
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitFishingHandoff().RequestId.IsValid());
}
