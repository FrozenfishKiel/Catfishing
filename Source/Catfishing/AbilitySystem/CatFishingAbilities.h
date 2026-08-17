#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatFishingAbilities.generated.h"

class UCatFishingCommandComponent;

UCLASS(Abstract)
class CATFISHING_API UCatFishingGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCatFishingGameplayAbility();
	static bool ShouldWaitForRemoteClient(bool bIsNetAuthority, bool bIsLocallyControlled);

protected:
	UCatFishingCommandComponent* ResolveCommandComponent(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const;
	void FinishOneShot(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bSubmitted);
};

UCLASS()
class CATFISHING_API UCatGA_FishingRodInteract : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingRodInteract();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingPrimaryAction : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingPrimaryAction();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingCancel : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingCancel();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingScoop : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingScoop();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class CATFISHING_API UCatGA_FishingChum : public UCatFishingGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingChum();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS(Abstract)
class CATFISHING_API UCatGA_FishingOutcomeBase : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UCatGA_FishingOutcomeBase();
};
