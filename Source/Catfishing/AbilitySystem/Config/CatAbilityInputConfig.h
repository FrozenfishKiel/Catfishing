#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CatAbilityInputConfig.generated.h"

class UInputAction;

USTRUCT(BlueprintType)
struct FCatAbilityInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<const UInputAction> InputAction;

	UPROPERTY(EditDefaultsOnly, Category="Input")
	FGameplayTag InputTag;
};

UCLASS(BlueprintType, Const)
class CATFISHING_API UCatAbilityInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	bool IsRuntimeReady() const;

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TArray<FCatAbilityInputAction> AbilityInputActions;
};
