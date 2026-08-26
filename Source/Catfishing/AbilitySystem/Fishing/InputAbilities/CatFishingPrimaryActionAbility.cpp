#include "AbilitySystem/Fishing/InputAbilities/CatFishingPrimaryActionAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingPrimaryAction::UCatGA_FishingPrimaryAction()
{
	// 构造流程：只写入 Primary 技能 Tag，让 AbilitySet、输入配置和运行时 AbilitySpec 使用同一个身份标记。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Primary));
}

void UCatGA_FishingPrimaryAction::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：Primary 按下只提交 Pressed 边沿并保持实例存活；具体语义由服务器按当前钓鱼阶段裁决。
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitPrimaryPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}

void UCatGA_FishingPrimaryAction::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 松开流程：先播放本地收势，再把 Released 边沿交给服务器换算按住时长，最后结束本 Ability 生命周期。
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitPrimaryReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
