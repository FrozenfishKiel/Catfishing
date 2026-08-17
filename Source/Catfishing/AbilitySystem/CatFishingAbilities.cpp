#include "AbilitySystem/CatFishingAbilities.h"

#include "AbilitySystem/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"

UCatFishingGameplayAbility::UCatFishingGameplayAbility()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UCatFishingGameplayAbility::ShouldWaitForRemoteClient(const bool bIsNetAuthority,
	const bool bIsLocallyControlled)
{
	return bIsNetAuthority && !bIsLocallyControlled;
}

UCatFishingCommandComponent* UCatFishingGameplayAbility::ResolveCommandComponent(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	const ACatfishingPlayerController* Controller = ActorInfo
		? Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get()) : nullptr;
	return Controller ? Controller->GetFishingCommandComponent() : nullptr;
}

bool UCatFishingGameplayAbility::CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const
{
	return ActorInfo && ActorInfo->IsLocallyControlled() && ResolveCommandComponent(ActorInfo);
}

bool UCatFishingGameplayAbility::IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const
{
	return ActorInfo && ShouldWaitForRemoteClient(ActorInfo->IsNetAuthority(), ActorInfo->IsLocallyControlled());
}

void UCatFishingGameplayAbility::FinishOneShot(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bSubmitted)
{
	if (bSubmitted)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
	else
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}

UCatGA_FishingRodInteract::UCatGA_FishingRodInteract()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_RodInteract));
}
void UCatGA_FishingRodInteract::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitRodInteract().RequestId.IsValid());
}

UCatGA_FishingPrimaryAction::UCatGA_FishingPrimaryAction()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Primary));
}
void UCatGA_FishingPrimaryAction::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
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
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		Commands->SubmitPrimaryReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCatGA_FishingCancel::UCatGA_FishingCancel()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Cancel));
}
void UCatGA_FishingCancel::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitCancel().RequestId.IsValid());
}

UCatGA_FishingScoop::UCatGA_FishingScoop()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Scoop));
}
void UCatGA_FishingScoop::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitScoop().RequestId.IsValid());
}

UCatGA_FishingChum::UCatGA_FishingChum()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Chum));
}
void UCatGA_FishingChum::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitChum().RequestId.IsValid());
}

UCatGA_FishingOutcomeBase::UCatGA_FishingOutcomeBase()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
