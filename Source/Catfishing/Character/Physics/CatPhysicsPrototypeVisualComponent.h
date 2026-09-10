#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Character/Animation/CatQuadrupedLocomotion.h"
#include "CatPhysicsPrototypeVisualComponent.generated.h"

class UAnimSequence;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/** 共享物理身体的只读表现：正式角色复制现有 ABP/Montage，原型使用基础步态；前爪追随真实刚体。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicsPrototypeVisualComponent : public UActorComponent
{

	GENERATED_BODY()

public:
	UCatPhysicsPrototypeVisualComponent();

	/** 使用正式角色提供的动画源，或为原型创建基础动画源；可见姿势在动画后叠加物理爪 IK。 */
	bool InitializeVisual(USceneComponent* InBodyRoot, UPrimitiveComponent* InLeftHand,
		UPrimitiveComponent* InRightHand, USkeletalMeshComponent* ExistingAnimationSource = nullptr);

	/** 只控制两只爪的表现混合；调用者仍负责真实抓握和手部目标。 */
	void SetHandReachState(bool bInLeftActive, bool bInRightActive);

	UPoseableMeshComponent* GetVisualMesh() const { return VisualMesh; }
	USkeletalMeshComponent* GetAnimationSource() const { return AnimationSource; }
	FVector GetVisualHandWorldLocation(bool bLeftHand) const;
	const FCatQuadrupedLocomotionObservation& GetLocomotionObservation() const { return Locomotion.GetObservation(); }

	UPROPERTY(EditAnywhere, Category="Catfishing|Locomotion")
	FCatQuadrupedLocomotionSettings LocomotionSettings;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EAnimationState : uint8 { Idle, Walk, Takeoff, Airborne, Landing };
	void UpdateBaseAnimation(float DeltaTime);
	void PlayBaseAnimation(EAnimationState NewState);
	void RefreshVisualPose(float DeltaTime);
	bool IsFormalJumpPoseActive() const;
	void SolveHandReach(bool bLeftHand, const FVector& TargetWorld, float Alpha);
	void RebuildComponentPose();
	void DestroyVisualComponents();
	void LogReachLimitChange(bool bLeftHand, bool bClamped, double Distance, double ChainLength);

	/** 原型的基础素材；正式初始化使用 Character 原有 Mesh/AnimClass，不替换正式 ABP。 */
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
	FCatQuadrupedLocomotion Locomotion;
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
	bool bOwnsAnimationSource = true;
	bool bFormalJumpRootCompensationActive = false;
};
