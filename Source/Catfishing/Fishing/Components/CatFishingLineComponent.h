#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatFishingLineComponent.generated.h"

class USceneComponent;

/** Presentation-only line endpoints. Fishing authority never reads this component. */
UCLASS(ClassGroup=(Fishing), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingLineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatFishingLineComponent();
	UFUNCTION(BlueprintCallable, Category="Fishing|Presentation")
	void SetPresentationEndpoints(USceneComponent* InRodTip, USceneComponent* InHookAnchor);
	UFUNCTION(BlueprintPure, Category="Fishing|Presentation") FVector GetPresentationStart() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Presentation") FVector GetPresentationEnd() const;

private:
	UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> RodTip;
	UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> HookAnchor;
};
