#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishingHookActor.generated.h"

class USceneComponent;
class UProjectileMovementComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishingHookActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishingHookActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId);
	bool BeginAuthoritativeFlight(const FVector& InitialVelocity, const FVector& ExpectedLandingWorldPoint);
	bool FinalizeAuthoritativeLandingOnce(bool bSucceeded, const FVector& LandingWorldPoint);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	const FCatFishingHookPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Hook")
	void BP_OnHookPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Hook") void BP_PlayHookPresentationEvent(FGameplayTag EventTag);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingHookPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> HookVisualAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BobberVisualAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BaitVisualAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingHookPresentationState PresentationState;
	bool bIdentityInitialized = false;
	bool bLandingFinalized = false;
	bool bPresentationDeferred = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishingHookPresentationState PendingPreviousPresentationState;
	FCatFishingHookPresentationState PendingCurrentPresentationState;
};
