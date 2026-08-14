#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishingRodActor.generated.h"

class APlayerState;
class USceneComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishingRodActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishingRodActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InRodActorId, FName InRodDefinitionId, FName InRodSkinDefinitionId,
		APlayerState* InOwnerPlayerState, APlayerState* InOperatorPlayerState, bool bInDeployed, bool bInBroken);
	const FCatFishingRodPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetRodTipWorldTransform() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetStandWorldTransform() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetGripWorldTransform() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod")
	void BP_OnRodPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_ApplyRodSkin(FName RodSkinDefinitionId);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_PlayRodPresentationEvent(FGameplayTag EventTag);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingRodPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RodTipAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> StandAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> GripAnchor;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingRodPresentationState PresentationState;
	FTransform RodTipCanonicalLocalTransform = FTransform::Identity;
	FTransform StandCanonicalLocalTransform = FTransform::Identity;
	FTransform GripCanonicalLocalTransform = FTransform::Identity;
	bool bIdentityInitialized = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishingRodPresentationState PendingPreviousPresentationState;
	FCatFishingRodPresentationState PendingCurrentPresentationState;
};
