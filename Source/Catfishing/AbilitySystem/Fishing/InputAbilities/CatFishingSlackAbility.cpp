#include "AbilitySystem/Fishing/InputAbilities/CatFishingSlackAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
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
	bReleaseSubmitted = false;
	bPressSubmitted = false;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands || !Commands->SubmitSlackPressed().RequestId.IsValid())
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

void UCatGA_FishingSlack::HandleInputReleased(const float ServerHeldSeconds)
{
	// 松开流程：Task 已按同一 Spec 接收 Release；时长当前不参与放线数值，仍由 Task 统一结束按住生命周期。
	(void)ServerHeldSeconds;
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitSlackReleased();
		bReleaseSubmitted = true;
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
}

void UCatGA_FishingSlack::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：Task 正常松开已提交时不重复；输入重置、失焦或其他 Ability 取消时补发释放，保证 Commands 的连续放线状态随 GAS 生命周期清零。
	if (bPressSubmitted && !bReleaseSubmitted && CanSubmitLocalCommand(ActorInfo))
	{
		if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo))
		{
			Commands->SubmitSlackReleased();
			bReleaseSubmitted = true;
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
