#include "AbilitySystem/CatAbilitySystemComponent.h"

#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "AbilitySystem/CatFishingStaminaEffect.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Abilities/GameplayAbility.h"

namespace
{
	ECatAbilityActivationPolicy ResolveActivationPolicy(const FGameplayTagContainer& Tags)
	{
		if (Tags.HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_WhileInputActive))
		{
			return ECatAbilityActivationPolicy::WhileInputActive;
		}
		if (Tags.HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_OnGranted))
		{
			return ECatAbilityActivationPolicy::OnGranted;
		}
		return ECatAbilityActivationPolicy::OnInputTriggered;
	}
}

void UCatAbilitySystemComponent::RegisterAbilityInput(const FGameplayAbilitySpecHandle Handle,
	const FGameplayTag InputTag, const ECatAbilityActivationPolicy ActivationPolicy)
{
	if (!Handle.IsValid())
	{
		return;
	}
	ActivationPolicyByHandle.Add(Handle, ActivationPolicy);
	if (InputTag.IsValid())
	{
		SpecHandlesByInputTag.FindOrAdd(InputTag).AddUnique(Handle);
	}
}

void UCatAbilitySystemComponent::UnregisterAbilityInput(const FGameplayAbilitySpecHandle Handle)
{
	ActivationPolicyByHandle.Remove(Handle);
	for (auto It = SpecHandlesByInputTag.CreateIterator(); It; ++It)
	{
		It.Value().RemoveSingleSwap(Handle);
		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
	InputPressedSpecHandles.RemoveSingleSwap(Handle);
	InputReleasedSpecHandles.RemoveSingleSwap(Handle);
	InputHeldSpecHandles.RemoveSingleSwap(Handle);
}

void UCatAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	const TArray<FGameplayAbilitySpecHandle>* Handles = SpecHandlesByInputTag.Find(InputTag);
	if (!Handles)
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle Handle : *Handles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = true;
			InputPressedSpecHandles.AddUnique(Handle);
			InputHeldSpecHandles.AddUnique(Handle);
		}
	}
}

void UCatAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	const TArray<FGameplayAbilitySpecHandle>* Handles = SpecHandlesByInputTag.Find(InputTag);
	if (!Handles)
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle Handle : *Handles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = false;
			InputReleasedSpecHandles.AddUnique(Handle);
			InputHeldSpecHandles.RemoveSingleSwap(Handle);
		}
	}
}

void UCatAbilitySystemComponent::ProcessAbilityInput(const float DeltaTime, const bool bGamePaused)
{
	(void)DeltaTime;
	if (bGamePaused)
	{
		return;
	}
	if (bPendingFishingStaminaReset)
	{
		RequestFishingStaminaReset();
		if (bPendingFishingStaminaReset)
		{
			ResetAbilityInput();
			return;
		}
	}

	TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
	for (const FGameplayAbilitySpecHandle Handle : InputHeldSpecHandles)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		const ECatAbilityActivationPolicy* Policy = ActivationPolicyByHandle.Find(Handle);
		if (Spec && Policy && *Policy == ECatAbilityActivationPolicy::WhileInputActive && !Spec->IsActive())
		{
			AbilitiesToActivate.AddUnique(Handle);
		}
	}
	for (const FGameplayAbilitySpecHandle Handle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			if (Spec->IsActive())
			{
				AbilitySpecInputPressed(*Spec);
				InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle,
					Spec->ActivationInfo.GetActivationPredictionKey());
			}
			else if (ActivationPolicyByHandle.FindRef(Handle) != ECatAbilityActivationPolicy::OnGranted)
			{
				AbilitiesToActivate.AddUnique(Handle);
			}
		}
	}
	for (const FGameplayAbilitySpecHandle Handle : AbilitiesToActivate)
	{
		TryActivateAbility(Handle);
	}
	for (const FGameplayAbilitySpecHandle Handle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle); Spec && Spec->IsActive())
		{
			AbilitySpecInputReleased(*Spec);
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle,
				Spec->ActivationInfo.GetActivationPredictionKey());
		}
	}
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UCatAbilitySystemComponent::ResetAbilityInput()
{
	for (const FGameplayAbilitySpecHandle Handle : InputHeldSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = false;
		}
	}
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

bool UCatAbilitySystemComponent::ApplyFishingStaminaDelta(const float Delta)
{
	if (!FMath::IsFinite(Delta) || FMath::IsNearlyZero(Delta) || !GetOwnerActor() || !GetAvatarActor()
		|| !IsOwnerActorAuthoritative())
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UCatGE_FishingStaminaDelta::StaticClass(), 1.0f, MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(CatFishingAbilityTags::Data_Fishing_FightStaminaDelta, Delta);
	ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	return true;
}

bool UCatAbilitySystemComponent::InitializeFishingStaminaForSession()
{
	// 基线按 Avatar 的猫种类解析，与搏斗装配的 CatStaminaMaximum 保持同源；非 CatCharacter Avatar 走全局值。
	const ACatCharacter* Character = Cast<ACatCharacter>(GetAvatarActor());
	const FName CatDefinitionId = Character ? Character->GetCatDefinitionId() : NAME_None;
	float Baseline = 0.0f;
	if (!GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(CatDefinitionId, Baseline))
	{
		return false;
	}
	const float Current = GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	return FMath::IsNearlyEqual(Current, Baseline) || ApplyFishingStaminaDelta(Baseline - Current);
}

bool UCatAbilitySystemComponent::RequestFishingStaminaReset()
{
	bPendingFishingStaminaReset = true;
	if (!GetOwnerActor() || !GetAvatarActor())
	{
		return true;
	}
	if (InitializeFishingStaminaForSession())
	{
		bPendingFishingStaminaReset = false;
	}
	return !bPendingFishingStaminaReset;
}

bool UCatAbilitySystemComponent::EnsureFishingStaminaReadyForNewSession()
{
	if (bPendingFishingStaminaReset)
	{
		RequestFishingStaminaReset();
		if (bPendingFishingStaminaReset)
		{
			return false;
		}
	}
	return GetOwnerActor() && GetAvatarActor()
		&& GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) > 0.0f;
}

void UCatAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	if (bPendingFishingStaminaReset)
	{
		RequestFishingStaminaReset();
	}
}

void UCatAbilitySystemComponent::ClearActorInfo()
{
	ResetAbilityInput();
	Super::ClearActorInfo();
}

void UCatAbilitySystemComponent::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);
	const FGameplayTag InputRoot = FGameplayTag::RequestGameplayTag(FName(TEXT("Cat.Input")));
	const FGameplayTagContainer& Tags = AbilitySpec.GetDynamicSpecSourceTags();
	for (const FGameplayTag Tag : Tags)
	{
		if (Tag.MatchesTag(InputRoot) && Tag != InputRoot)
		{
			RegisterAbilityInput(AbilitySpec.Handle, Tag, ResolveActivationPolicy(Tags));
		}
	}
}

void UCatAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	UnregisterAbilityInput(AbilitySpec.Handle);
	Super::OnRemoveAbility(AbilitySpec);
}
