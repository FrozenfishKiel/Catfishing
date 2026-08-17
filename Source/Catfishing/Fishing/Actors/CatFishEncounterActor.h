#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishEncounterActor.generated.h"

class USceneComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishEncounterActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishEncounterActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId, FName InFishDefinitionId, double InInitialLineLength);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	bool ApplyFightStepFromAuthority(ECatFishMotionIntent MotionIntent, double CurrentLineLength,
		const FVector& FishWorldPosition);
	const FCatFishEncounterPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish")
	void BP_OnFishPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish") void BP_PlayFishPresentationEvent(FGameplayTag EventTag);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishEncounterPresentationState PresentationState;
	bool bIdentityInitialized = false;
	bool bPresentationDeferred = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishEncounterPresentationState PendingPreviousPresentationState;
	FCatFishEncounterPresentationState PendingCurrentPresentationState;
};
