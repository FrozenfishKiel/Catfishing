#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/DataAsset.h"
#include "GameplayEffectTypes.h"
#include "CatAbilitySet.generated.h"

class UCatAbilitySystemComponent;
class UGameplayEffect;

UENUM(BlueprintType)
enum class ECatAbilityActivationPolicy : uint8
{
	OnInputTriggered,
	WhileInputActive,
	OnGranted
};

USTRUCT(BlueprintType)
struct FCatAbilitySetAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TSubclassOf<UGameplayAbility> Ability;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, Category="Ability", meta=(ClampMin="1"))
	int32 Level = 1;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	ECatAbilityActivationPolicy ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TSubclassOf<UGameplayEffect> InitialEffect;
};

USTRUCT(BlueprintType)
struct FCatGrantedAbilitySetHandles
{
	GENERATED_BODY()

public:
	const TArray<FGameplayAbilitySpecHandle>& GetAbilitySpecHandles() const { return AbilitySpecHandles; }
	void TakeFromAbilitySystem(UCatAbilitySystemComponent* AbilitySystem);

private:
	friend class UCatAbilitySet;

	UPROPERTY(Transient)
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	UPROPERTY(Transient)
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;
};

UCLASS(BlueprintType, Const)
class CATFISHING_API UCatAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	bool IsRuntimeReady() const;
	bool GiveToAbilitySystem(UCatAbilitySystemComponent* AbilitySystem,
		FCatGrantedAbilitySetHandles& OutGrantedHandles) const;

	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<FCatAbilitySetAbility> GrantedAbilities;
};
