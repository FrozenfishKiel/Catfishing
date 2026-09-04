#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishingRodActor.generated.h"

class APlayerState;
class USceneComponent;
class UCharacterMovementComponent;

/** 高频复制的手持鱼线约束目标；不推进鱼竿业务 Revision，也不保存第二份搏斗终态。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingCarrierConstraintState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal PullDirection = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	float PullAccelerationCentimetersPerSecondSquared = 0.0f;
	/** 本约束步要求猫端达到的向鱼速度；本地移动帧平滑追赶该目标，不作为可累积冲量。 */
	UPROPERTY(BlueprintReadOnly)
	float TargetPullSpeedCentimetersPerSecond = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float MaximumAwaySpeedMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly)
	float NormalizedTension = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float ConstraintErrorCentimeters = 0.0f;
	/** 当前搏斗是否要求鱼竿使用受力后的实际姿态，而不是瞬时跟随控制器。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFightActive = false;
	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal RodPullAxis = FVector::ForwardVector;
	/** 垂直鱼线时的最大转矩；实际有向转矩随杆姿态连续计算。 */
	UPROPERTY(BlueprintReadOnly)
	float MaximumFishTorqueStrengthMeters = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float CatTorqueCapacityStrengthMeters = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;
};

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishingRodActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishingRodActor();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 初始化这根场景鱼竿的权威身份；ActorId 负责场景对象，ItemInstanceId 负责回到库存里的同一件物品。 */
	bool InitializeAuthoritativeIdentity(FGuid InRodActorId, FGuid InItemInstanceId, FName InRodDefinitionId,
		FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState, APlayerState* InOperatorPlayerState,
		bool bInDeployed, bool bInBroken);
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
	/** 所有玩家共用的 R 交互锚点；只决定能否加入，不随当前人数或下一个槽位变化。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetOperatorInteractionWorldTransform() const;
	/** 槽位 0 是右侧，1 是左侧；更高索引左右交替向外扩展。非法索引回退到原始 Stand 中心。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetOperatorStandWorldTransform(int32 SlotIndex) const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorCount() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorSlotIndex(APlayerState* PlayerState) const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") bool IsPrimaryOperator(APlayerState* PlayerState) const;
	int32 GetFirstFreeOperatorSlotIndex() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetGripWorldTransform() const;
	/** 服务器规范握持跟随：只读 PlayerController/Pawn 权威姿态，不信任客户端 Socket Transform。 */
	bool RefreshHeldTransformFromAuthority(double DeltaSeconds = 0.0);
	/** 最后一名操作者离开后把同一 Actor 放到服务器裁定的地面 Transform；不改会话或物品身份。 */
	bool PlaceOnGroundFromAuthority(const FTransform& GroundTransform);
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodForwardVector() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodTipVelocity() const
	{
		return AuthoritativeRodTipVelocity;
	}
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeHolderVelocity() const
	{
		return AuthoritativeHolderVelocity;
	}
	/** FightRunner 发布同一份双端求解目标；服务器与拥有客户端都在移动帧内平滑追赶，不直接写 Actor Transform。 */
	bool SetCarrierConstraintFromAuthority(const FVector& PullDirection,
		double PullAccelerationCentimetersPerSecondSquared, double TargetPullSpeedCentimetersPerSecond,
		double MaximumAwaySpeedMultiplier,
		double NormalizedTension, double ConstraintErrorCentimeters,
		bool bFightActive = false, double MaximumFishTorqueStrengthMeters = 0.0,
		double CatTorqueCapacityStrengthMeters = 0.0,
		const FVector& RodPullAxis = FVector::ForwardVector);
	void ClearCarrierConstraintFromAuthority();
	UFUNCTION(BlueprintPure, Category="Fishing|Rod")
	const FCatFishingCarrierConstraintState& GetCarrierConstraintState() const
	{
		return CarrierConstraintState;
	}
	APawn* GetHolderPawnFromAuthority() const;
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
	UFUNCTION()
	void OnRep_CarrierConstraintState();
	void QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	void ApplyCarrierConstraint(float DeltaSeconds);
	void ResetCarrierConstraintSmoothing();
	void UpdateCarrierConstraintTickDependency(UCharacterMovementComponent* Movement);
	bool CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next, int64 ExpectedRevision);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RodTipAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> StandAnchor;
	/** 当前产品左右两位的编辑器可见参考锚；第三位及以后也统一由编号公式计算，不增加专用锚点。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightStandAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftStandAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> GripAnchor;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingRodPresentationState PresentationState;
	UPROPERTY(ReplicatedUsing=OnRep_CarrierConstraintState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingCarrierConstraintState CarrierConstraintState;
	FTransform RodTipCanonicalLocalTransform = FTransform::Identity;
	FTransform StandCanonicalLocalTransform = FTransform::Identity;
	FTransform GripCanonicalLocalTransform = FTransform::Identity;
	FVector AuthoritativeRodTipVelocity = FVector::ZeroVector;
	FVector AuthoritativeHolderVelocity = FVector::ZeroVector;
	FRotator AuthoritativeHeldAimRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<APawn> AuthoritativeAimHolder;
	/** 20 Hz 权威目标在本机角色移动帧中的平滑速度；只属于瞬态表现/移动接缝，不复制。 */
	FVector SmoothedCarrierPullVelocity = FVector::ZeroVector;
	double SmoothedCarrierAwaySpeedMultiplier = 1.0;
	TWeakObjectPtr<APawn> SmoothedConstraintHolder;
	TWeakObjectPtr<UCharacterMovementComponent> CarrierConstraintTickDependency;
	double NextCarrierSmoothingDiagnosticWorldSeconds = 0.0;
	bool bLastCarrierSmoothingDiagnosticActive = false;
	double NextRodRotationResistanceDiagnosticWorldSeconds = 0.0;
	bool bLastRodTorqueBalanced = false;
	bool bHeldAimInitialized = false;
	FTransform ResolveOperatorStandLocalTransform(int32 SlotIndex) const;
	bool bIdentityInitialized = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishingRodPresentationState PendingPreviousPresentationState;
	FCatFishingRodPresentationState PendingCurrentPresentationState;
};
