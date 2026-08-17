#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySet.h"
#include "CatAbilitySystemComponent.generated.h"

UCLASS()
class CATFISHING_API UCatAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	void RegisterAbilityInput(FGameplayAbilitySpecHandle Handle, FGameplayTag InputTag,
		ECatAbilityActivationPolicy ActivationPolicy);
	void UnregisterAbilityInput(FGameplayAbilitySpecHandle Handle);
	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);
	void ResetAbilityInput();

	int32 GetPressedInputCount() const { return InputPressedSpecHandles.Num(); }
	int32 GetReleasedInputCount() const { return InputReleasedSpecHandles.Num(); }
	int32 GetHeldInputCount() const { return InputHeldSpecHandles.Num(); }

	bool ApplyFishingStaminaDelta(float Delta);
	bool InitializeFishingStaminaForSession();
	bool RequestFishingStaminaReset();
	bool EnsureFishingStaminaReadyForNewSession();
	bool HasPendingFishingStaminaReset() const { return bPendingFishingStaminaReset; }

	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void ClearActorInfo() override;

protected:
	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;

private:
	TMap<FGameplayTag, TArray<FGameplayAbilitySpecHandle>> SpecHandlesByInputTag;
	TMap<FGameplayAbilitySpecHandle, ECatAbilityActivationPolicy> ActivationPolicyByHandle;
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;
	bool bPendingFishingStaminaReset = false;
};
