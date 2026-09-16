#include "AbilitySystem/Fishing/InputAbilities/CatFishingPrimaryActionAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
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
	bReleaseSubmitted = false;
	bPressSubmitted = false;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands || !Commands->SubmitPrimaryPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	bPressSubmitted = true;
	UAbilityTask_WaitInputRelease* WaitForRelease = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (!WaitForRelease)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	WaitForRelease->OnRelease.AddDynamic(this, &ThisClass::HandleInputReleased);
	WaitForRelease->ReadyForActivation();
}

void UCatGA_FishingPrimaryAction::HandleInputReleased(const float ServerHeldSeconds)
{
	// 松开流程：Task 已按同一 Spec 接收 Release；时长当前不参与 Primary 数值，仍显式接收以保持后续按住语义只扩展 Task 回调。
	(void)ServerHeldSeconds;
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitPrimaryReleased();
		bReleaseSubmitted = true;
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
}

void UCatGA_FishingPrimaryAction::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：Task 正常松开已提交时不重复；输入重置、失焦或其他 Ability 取消时补发释放，保证 Commands 的连续收线状态随 GAS 生命周期清零。
	if (bPressSubmitted && !bReleaseSubmitted && CanSubmitLocalCommand(ActorInfo))
	{
		if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo))
		{
			Commands->SubmitPrimaryReleased();
			bReleaseSubmitted = true;
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
