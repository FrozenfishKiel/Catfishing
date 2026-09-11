#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatConditionPresentationComponent.generated.h"

class UAnimSequence;
class UAnimMontage;
class UCatConditionComponent;

/** Presentation consumer of Condition's replicated downed state; never owns locomotion or recovery. */
UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatConditionPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatConditionPresentationComponent();
	FName GetObservedPosePhase() const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	void RefreshCondition();
	void PlayPhase(int32 NewPhase);
	UPROPERTY() TObjectPtr<UCatConditionComponent> Condition;
	UPROPERTY() TObjectPtr<UAnimMontage> ActiveMontage;
	/** Stand-to-sit, sit-to-lie, lying loop, lie-to-sit, sit-to-stand; override per character skeleton. */
	UPROPERTY(EditDefaultsOnly, Category="Catfishing|Animation") TArray<TObjectPtr<UAnimSequence>> PoseClips;
	int32 Phase = INDEX_NONE;
	double PhaseEndsAt = 0;
	bool bObservedDowned = false;
	bool bInitialized = false;
};
