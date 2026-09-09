#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatPhysicsPrototypeVisualComponent.generated.h"

class UAnimSequence;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/** 独立物理原型的只读身体表现：复制现有步态，再让前爪追随真实手部刚体；不写物理或抓握状态。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicsPrototypeVisualComponent : public UActorComponent
{

	GENERATED_BODY()

public:
	UCatPhysicsPrototypeVisualComponent();

	/** 为同一 Pawn 创建隐藏动画源与可见程序姿势；重复初始化相同组件时保持幂等。 */
	bool InitializeVisual(USceneComponent* InBodyRoot, UPrimitiveComponent* InLeftHand,
		UPrimitiveComponent* InRightHand);

	/** 只控制两只爪的表现混合；调用者仍负责真实抓握和手部目标。 */
	void SetHandReachState(bool bInLeftActive, bool bInRightActive);

	UPoseableMeshComponent* GetVisualMesh() const { return VisualMesh; }
	USkeletalMeshComponent* GetAnimationSource() const { return AnimationSource; }
	FVector GetVisualHandWorldLocation(bool bLeftHand) const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EAnimationState : uint8 { Idle, Walk, Takeoff, Airborne, Landing };
	void UpdateBaseAnimation(float DeltaTime);
	void PlayBaseAnimation(EAnimationState NewState);
	void RefreshVisualPose(float DeltaTime);
	void SolveHandReach(bool bLeftHand, const FVector& TargetWorld, float Alpha);
	void RebuildComponentPose();
	void DestroyVisualComponents();
	void LogReachLimitChange(bool bLeftHand, bool bClamped, double Distance, double ChainLength);

	/** 原型硬引用只随原型类 Cook，不替换正式 Character/ABP 资产。 */
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<UAnimSequence> IdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<UAnimSequence> WalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<UAnimSequence> JumpStartAnimation;
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<UAnimSequence> JumpLoopAnimation;
	UPROPERTY(EditDefaultsOnly, Category = "Catfishing|PhysicsPrototype|Visual")
	TObjectPtr<UAnimSequence> JumpEndAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> VisualMesh;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> AnimationSource;

	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> LeftHand;

	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> RightHand;

	TArray<FTransform> ComponentPose;
	TArray<FTransform> TransitionFromPose;
	TArray<FTransform> LastBasePose;
	EAnimationState AnimationState = EAnimationState::Idle;
	float AnimationStateSeconds = 0.0f;
	float TransitionSeconds = 1.0f;
	float TakeoffConfirmationSeconds = 0.0f;
	uint32 ObservedResetEpoch = 0;
	bool bHasMovementSample = false;
	bool bWasGrounded = false;
	int32 LeftChain[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	int32 RightChain[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	float LeftReachAlpha = 0.0f;
	float RightReachAlpha = 0.0f;
	bool bLeftActive = false;
	bool bRightActive = false;
	bool bLeftReachClamped = false;
	bool bRightReachClamped = false;
	bool bReportedInvalidPose = false;
};
