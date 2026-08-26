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
	bool ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip, const FTransform& InStand, const FTransform& InGrip);
	/** 兼容旧单操作手写口：传入玩家时重置为仅该玩家，传空时清空全部槽位。 */
	bool SetOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision);
	/** 把玩家追加到第一个空槽；OutSlotIndex 只有成功时有效。 */
	bool AddOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision, int32& OutSlotIndex);
	/** 移除玩家并压紧数组；主位离开时原 1 号位自动晋升为 0 号位。 */
	bool RemoveOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision,
		APlayerState*& OutPromotedPrimaryPlayerState);
	bool SetRodSkinFromAuthority(FName InRodSkinDefinitionId, int64 ExpectedRevision);
	bool SetBrokenFromAuthority(bool bInBroken, int64 ExpectedRevision);
	bool SetDeployedFromAuthority(bool bInDeployed, int64 ExpectedRevision);
	const FCatFishingRodPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetRodTipWorldTransform() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetStandWorldTransform() const;
	/** 槽位 0 是右侧，1 是左侧；更高索引左右交替向外扩展。非法索引回退到原始 Stand 中心。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetOperatorStandWorldTransform(int32 SlotIndex) const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorCount() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorSlotIndex(APlayerState* PlayerState) const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") bool IsPrimaryOperator(APlayerState* PlayerState) const;
	int32 GetFirstFreeOperatorSlotIndex() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetGripWorldTransform() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod")
	void BP_OnRodPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_ApplyRodSkin(FName RodSkinDefinitionId);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_PlayRodPresentationEvent(FGameplayTag EventTag);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingRodPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	bool CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next, int64 ExpectedRevision);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RodTipAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> StandAnchor;
	/** 当前产品左右两位的编辑器可见参考锚；权威站位仍由 GetOperatorStandWorldTransform 计算。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightStandAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftStandAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> GripAnchor;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingRodPresentationState PresentationState;
	FTransform RodTipCanonicalLocalTransform = FTransform::Identity;
	FTransform StandCanonicalLocalTransform = FTransform::Identity;
	FTransform GripCanonicalLocalTransform = FTransform::Identity;
	FTransform ResolveOperatorStandLocalTransform(int32 SlotIndex) const;
	bool bIdentityInitialized = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishingRodPresentationState PendingPreviousPresentationState;
	FCatFishingRodPresentationState PendingCurrentPresentationState;
};
