#pragma once

#include "CoreMinimal.h"
#include "CatQuadrupedLocomotion.generated.h"

class UAnimSequence;
class UCatPhysicalBodyComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/** Distances are in unscaled mesh centimetres; runtime applies the visible mesh scale once. */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatQuadrupedLocomotionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Locomotion") bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="0.1", ClampMax="1")) float MinStrideScale = 0.6f;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="1", ClampMax="2")) float MaxStrideScale = 1.6f;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="0")) float MaxFootOffsetCm = 8.0f;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="0")) float MaxPelvisOffsetCm = 3.0f;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="0")) float MaxPlantDriftCm = 5.0f;
	UPROPERTY(EditAnywhere, Category="Locomotion", meta=(ClampMin="0.01")) float BlendSeconds = 0.12f;
};

/** Read-only diagnostics of the final-pose operation, never gameplay or replicated state. */
struct FCatQuadrupedLocomotionObservation
{
	FName Mode = TEXT("Uninitialized");
	double BodySpeedCmS = 0.0;
	double RelativeSpeedCmS = 0.0;
	double SupportSpeedCmS = 0.0;
	double AnimationSpeedCmS = 0.0;
	double StrideScale = 1.0;
	double Alpha = 0.0;
	double PelvisOffsetCm = 0.0;
	uint8 GroundMask = 0;
	uint8 PlantMask = 0;
	uint8 ReleasedPlantMask = 0;
	uint8 ExcludedFootMask = 0;
	FVector TargetsWorld[4] = {};
	double FootErrorCm[4] = {};
};

/** One post-animation, pre-grip pose pass. It does not move components, integrate physics or advance animation. */
class CATFISHING_API FCatQuadrupedLocomotion
{
public:
	/** Calibration shared with the prototype's owned animation player; zero means unsupported asset/skeleton. */
	double GetReferenceSpeedMeshCmS(UAnimSequence* Sequence, USkeletalMesh* InMesh);
	void Apply(USkeletalMeshComponent* AnimationSource, UPoseableMeshComponent* Visual,
		const UCatPhysicalBodyComponent* Body, float LeftReachAlpha, float RightReachAlpha,
		float DeltaSeconds, const FCatQuadrupedLocomotionSettings& Settings);
	void Reset();
	const FCatQuadrupedLocomotionObservation& GetObservation() const { return Observation; }

private:
	static constexpr int32 FootCount = 4;
	static constexpr int32 SampleCount = 96;
	struct FProfile
	{
		bool bValid = false;
		bool bLocomotion = false;
		double SpeedCmS = 0.0;
		double Duration = 0.0;
		double FloorZ[FootCount] = {};
		TArray<float> Contacts[FootCount];
	};
	struct FFoot
	{
		int32 Chain[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
		FVector SoleNormalLocal = FVector::UpVector;
		FVector PlantLocal = FVector::ZeroVector;
		TWeakObjectPtr<UPrimitiveComponent> Support;
		FName SupportBone;
		FVector OffsetWorld = FVector::ZeroVector;
		bool bPlanted = false;
		bool bReleasedUntilSwing = false;
	};
	struct FAnimationFrame
	{
		double Weight = 0.0;
		double SpeedCmS = 0.0;
		double FloorZ[FootCount] = {};
		double Contact[FootCount] = {};
	};
	bool Initialize(USkeletalMesh* Mesh);
	const FProfile& GetProfile(UAnimSequence* Sequence);
	FAnimationFrame ReadAnimation(USkeletalMeshComponent* Source, float DeltaSeconds);
	void RebuildPose(const TArray<FTransform>& LocalPose);
	void SolveFoot(TArray<FTransform>& LocalPose, int32 FootIndex, const FVector& TargetComponent,
		const FQuat& RotationComponent);
	void ClearPlants();
	void LogObservation(const UCatPhysicalBodyComponent* Body, double Now, bool bForce);

	TWeakObjectPtr<USkeletalMesh> Mesh;
	TMap<TWeakObjectPtr<UAnimSequence>, FProfile> Profiles;
	FFoot Feet[FootCount];
	TArray<FTransform> ComponentPose;
	int32 PelvisIndex = INDEX_NONE;
	bool bInitialized = false;
	bool bSkeletonChecked = false;
	bool bHasPreviousFrame = false;
	uint32 ResetEpoch = 0;
	FVector PreviousBodyLocation = FVector::ZeroVector;
	FQuat PreviousBodyRotation = FQuat::Identity;
	TWeakObjectPtr<UPrimitiveComponent> MotionSupport;
	FName MotionSupportBone;
	FTransform PreviousMotionSupportWorld = FTransform::Identity;
	double Alpha = 0.0;
	double StrideScale = 1.0;
	double PelvisOffset = 0.0;
	double NextLogSeconds = 0.0;
	FCatQuadrupedLocomotionObservation Observation;
};
