#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"

#include "Animation/AnimSequence.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Logging/CatLog.h"
#include "UObject/ConstructorHelpers.h"

namespace CatPhysicsPrototypeVisual
{
	const FName LeftBones[] = { TEXT("RigLFLeg1"), TEXT("RigLFLeg2"), TEXT("RigLFLeg3"), TEXT("RigLFLegAnkle") };
	const FName RightBones[] = { TEXT("RigRFLeg1"), TEXT("RigRFLeg2"), TEXT("RigRFLeg3"), TEXT("RigRFLegAnkle") };
	constexpr float WalkReferenceSpeed = 100.0f;
	constexpr float ReachBlendSpeed = 8.0f;
	constexpr int32 SolverIterations = 12;
	constexpr double ReachToleranceCentimeters = 0.15;
}

UCatPhysicsPrototypeVisualComponent::UCatPhysicsPrototypeVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Animalia/Cat/Meshes/Cat.Cat"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAsset(
		TEXT("/Game/Animalia/Cat/Animations/InPlace/Stand_00-IP.Stand_00-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAsset(
		TEXT("/Game/Animalia/Cat/Animations/InPlace/Loco_Walk-IP.Loco_Walk-IP"));
	CharacterMesh = MeshAsset.Object;
	IdleAnimation = IdleAsset.Object;
	WalkAnimation = WalkAsset.Object;
}

bool UCatPhysicsPrototypeVisualComponent::InitializeVisual(USceneComponent* InBodyRoot,
	UPrimitiveComponent* InLeftHand, UPrimitiveComponent* InRightHand)
{
	AActor* Owner = GetOwner();
	if (VisualMesh)
	{
		return BodyRoot == InBodyRoot && LeftHand == InLeftHand && RightHand == InRightHand;
	}
	if (!Owner || !InBodyRoot || !InLeftHand || !InRightHand || InBodyRoot->GetOwner() != Owner
		|| InLeftHand->GetOwner() != Owner || InRightHand->GetOwner() != Owner
		|| !CharacterMesh || !IdleAnimation || !WalkAnimation)
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=physics_prototype_visual_init_failed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=MissingOrMismatchedDependency"),
			*GetNameSafe(Owner), *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			Owner ? Owner->HasAuthority() : false, Owner ? static_cast<int32>(Owner->GetLocalRole()) : INDEX_NONE);
		return false;
	}

	const FReferenceSkeleton& Skeleton = CharacterMesh->GetRefSkeleton();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		LeftChain[Index] = Skeleton.FindBoneIndex(CatPhysicsPrototypeVisual::LeftBones[Index]);
		RightChain[Index] = Skeleton.FindBoneIndex(CatPhysicsPrototypeVisual::RightBones[Index]);
		if (LeftChain[Index] == INDEX_NONE || RightChain[Index] == INDEX_NONE
			|| (Index > 0 && (Skeleton.GetParentIndex(LeftChain[Index]) != LeftChain[Index - 1]
				|| Skeleton.GetParentIndex(RightChain[Index]) != RightChain[Index - 1])))
		{
			UE_LOG(LogCatCharacter, Warning,
				TEXT("Event=physics_prototype_visual_init_failed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=MissingHandBoneChain BoneIndex=%d"),
				*Owner->GetName(), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
				Owner->HasAuthority(), static_cast<int32>(Owner->GetLocalRole()), Index);
			return false;
		}
	}

	BodyRoot = InBodyRoot;
	LeftHand = InLeftHand;
	RightHand = InRightHand;
	const FTransform MeshRelativeTransform(FRotator(0.0, -90.0, 0.0), FVector(0.0, 0.0, -20.0));
	AnimationSource = NewObject<USkeletalMeshComponent>(Owner, TEXT("PhysicsPrototypeAnimationSource"));
	Owner->AddInstanceComponent(AnimationSource);
	AnimationSource->SetupAttachment(InBodyRoot);
	AnimationSource->SetRelativeTransform(MeshRelativeTransform);
	AnimationSource->SetSkinnedAssetAndUpdate(CharacterMesh);
	AnimationSource->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AnimationSource->SetGenerateOverlapEvents(false);
	AnimationSource->SetCastShadow(false);
	AnimationSource->SetHiddenInGame(true);
	AnimationSource->SetVisibility(false);
	AnimationSource->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AnimationSource->bEnableUpdateRateOptimizations = false;
	AnimationSource->RegisterComponent();
	AnimationSource->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	AnimationSource->PlayAnimation(IdleAnimation, true);
	// 本组件在物理步结束后唯一推进动画；隐藏源不再独立 Tick，避免一帧推进两次。
	AnimationSource->SetComponentTickEnabled(false);

	VisualMesh = NewObject<UPoseableMeshComponent>(Owner, TEXT("PhysicsPrototypeVisualMesh"));
	Owner->AddInstanceComponent(VisualMesh);
	VisualMesh->SetupAttachment(InBodyRoot);
	VisualMesh->SetRelativeTransform(MeshRelativeTransform);
	VisualMesh->SetSkinnedAssetAndUpdate(CharacterMesh);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCastShadow(true);
	VisualMesh->RegisterComponent();
	VisualMesh->SetComponentTickEnabled(false);
	ComponentPose.SetNum(Skeleton.GetNum());
	RefreshVisualPose(0.0f);
	SetComponentTickEnabled(GetWorld()->GetNetMode() != NM_DedicatedServer);
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=physics_prototype_visual_ready Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Mesh=%s BoneCount=%d HandSolver=CCD"),
		*Owner->GetName(), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
		Owner->HasAuthority(), static_cast<int32>(Owner->GetLocalRole()), *GetNameSafe(CharacterMesh), Skeleton.GetNum());
	return true;
}

void UCatPhysicsPrototypeVisualComponent::SetHandReachState(const bool bInLeftActive, const bool bInRightActive)
{
	bLeftActive = bInLeftActive;
	bRightActive = bInRightActive;
}

FVector UCatPhysicsPrototypeVisualComponent::GetVisualHandWorldLocation(const bool bLeftHand) const
{
	return VisualMesh ? VisualMesh->GetBoneLocationByName(bLeftHand
		? CatPhysicsPrototypeVisual::LeftBones[3] : CatPhysicsPrototypeVisual::RightBones[3], EBoneSpaces::WorldSpace)
		: FVector::ZeroVector;
}

void UCatPhysicsPrototypeVisualComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshVisualPose(DeltaTime);
}

void UCatPhysicsPrototypeVisualComponent::RefreshVisualPose(const float DeltaTime)
{
	if (!AnimationSource || !VisualMesh || !BodyRoot.IsValid() || !LeftHand.IsValid() || !RightHand.IsValid())
	{
		return;
	}
	const float SafeDelta = FMath::IsFinite(DeltaTime) ? FMath::Clamp(DeltaTime, 0.0f, 0.1f) : 0.0f;
	// Pawn 的 GetVelocity 在服务器读刚体、在客户端读快照；插值写 Transform 的组件速度不能代表复制步态。
	const float Speed = GetOwner()->GetVelocity().Size2D();
	const bool bShouldWalk = FMath::IsFinite(Speed) && Speed > (bUsingWalkAnimation ? 3.0f : 6.0f);
	if (bShouldWalk != bUsingWalkAnimation)
	{
		bUsingWalkAnimation = bShouldWalk;
		AnimationSource->PlayAnimation(bUsingWalkAnimation ? WalkAnimation : IdleAnimation, true);
	}
	AnimationSource->SetPlayRate(bUsingWalkAnimation
		? FMath::Clamp(Speed / CatPhysicsPrototypeVisual::WalkReferenceSpeed, 0.3f, 2.0f) : 1.0f);
	AnimationSource->TickAnimation(SafeDelta, false);
	AnimationSource->RefreshBoneTransforms();
	VisualMesh->CopyPoseFromSkeletalComponent(AnimationSource);
	if (VisualMesh->BoneSpaceTransforms.Num() != ComponentPose.Num())
	{
		if (!bReportedInvalidPose)
		{
			UE_LOG(LogCatCharacter, Warning,
				TEXT("Event=physics_prototype_visual_pose_rejected Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=BoneCountMismatch"),
				*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
				GetOwner()->HasAuthority(), static_cast<int32>(GetOwner()->GetLocalRole()));
			bReportedInvalidPose = true;
		}
		return;
	}
	bReportedInvalidPose = false;
	LeftReachAlpha = FMath::FInterpConstantTo(LeftReachAlpha, bLeftActive ? 1.0f : 0.0f,
		SafeDelta, CatPhysicsPrototypeVisual::ReachBlendSpeed);
	RightReachAlpha = FMath::FInterpConstantTo(RightReachAlpha, bRightActive ? 1.0f : 0.0f,
		SafeDelta, CatPhysicsPrototypeVisual::ReachBlendSpeed);
	RebuildComponentPose();
	SolveHandReach(true, LeftHand->GetComponentLocation(), LeftReachAlpha);
	SolveHandReach(false, RightHand->GetComponentLocation(), RightReachAlpha);
	VisualMesh->RefreshBoneTransforms();
}

void UCatPhysicsPrototypeVisualComponent::RebuildComponentPose()
{
	const FReferenceSkeleton& Skeleton = CharacterMesh->GetRefSkeleton();
	for (int32 Index = 0; Index < ComponentPose.Num(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		ComponentPose[Index] = Parent == INDEX_NONE ? VisualMesh->BoneSpaceTransforms[Index]
			: VisualMesh->BoneSpaceTransforms[Index] * ComponentPose[Parent];
		ComponentPose[Index].NormalizeRotation();
	}
}

void UCatPhysicsPrototypeVisualComponent::SolveHandReach(const bool bLeftHand, const FVector& TargetWorld,
	const float Alpha)
{
	if (Alpha <= KINDA_SMALL_NUMBER || TargetWorld.ContainsNaN())
	{
		LogReachLimitChange(bLeftHand, false, 0.0, 0.0);
		return;
	}
	const int32* Chain = bLeftHand ? LeftChain : RightChain;
	const FVector Shoulder = ComponentPose[Chain[0]].GetLocation();
	const FVector AnimatedHand = ComponentPose[Chain[3]].GetLocation();
	const FVector Target = FMath::Lerp(AnimatedHand,
		VisualMesh->GetComponentTransform().InverseTransformPosition(TargetWorld), static_cast<double>(Alpha));
	double ChainLength = 0.0;
	for (int32 Link = 1; Link < 4; ++Link)
	{
		ChainLength += FVector::Distance(ComponentPose[Chain[Link - 1]].GetLocation(), ComponentPose[Chain[Link]].GetLocation());
	}
	const FVector ShoulderToTarget = Target - Shoulder;
	const double TargetDistance = ShoulderToTarget.Size();
	const double MaximumReach = ChainLength * 0.995;
	const FVector ReachTarget = TargetDistance > MaximumReach
		? Shoulder + ShoulderToTarget.GetSafeNormal() * MaximumReach : Target;
	// 只旋转局部骨骼，不改平移/缩放；物理目标超出模型臂展时仍保持骨长。
	LogReachLimitChange(bLeftHand, TargetDistance > ChainLength + 0.5, TargetDistance, ChainLength);
	const FReferenceSkeleton& Skeleton = CharacterMesh->GetRefSkeleton();
	for (int32 Iteration = 0; Iteration < CatPhysicsPrototypeVisual::SolverIterations; ++Iteration)
	{
		if (FVector::DistSquared(ComponentPose[Chain[3]].GetLocation(), ReachTarget)
			<= FMath::Square(CatPhysicsPrototypeVisual::ReachToleranceCentimeters))
		{
			break;
		}
		for (int32 Link = 2; Link >= 0; --Link)
		{
			const int32 Joint = Chain[Link];
			const FVector JointPosition = ComponentPose[Joint].GetLocation();
			const FVector ToEnd = ComponentPose[Chain[3]].GetLocation() - JointPosition;
			const FVector ToTarget = ReachTarget - JointPosition;
			if (ToEnd.IsNearlyZero() || ToTarget.IsNearlyZero()) continue;
			const FQuat RotationDelta = FQuat::FindBetweenNormals(ToEnd.GetSafeNormal(), ToTarget.GetSafeNormal());
			const FQuat TargetRotation = (RotationDelta * ComponentPose[Joint].GetRotation()).GetNormalized();
			const int32 Parent = Skeleton.GetParentIndex(Joint);
			const FQuat LocalRotation = Parent == INDEX_NONE ? TargetRotation
				: (ComponentPose[Parent].GetRotation().Inverse() * TargetRotation).GetNormalized();
			VisualMesh->BoneSpaceTransforms[Joint].SetRotation(LocalRotation);
			RebuildComponentPose();
		}
	}
}

void UCatPhysicsPrototypeVisualComponent::LogReachLimitChange(const bool bLeftHand, const bool bClamped,
	const double Distance, const double ChainLength)
{
	bool& bPrevious = bLeftHand ? bLeftReachClamped : bRightReachClamped;
	if (bPrevious == bClamped) return;
	bPrevious = bClamped;
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=physics_prototype_visual_reach_limit_changed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Hand=%s Clamped=%d TargetDistanceCm=%.2f ChainLengthCm=%.2f"),
		*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
		GetOwner()->HasAuthority(), static_cast<int32>(GetOwner()->GetLocalRole()), bLeftHand ? TEXT("Left") : TEXT("Right"),
		bClamped, Distance, ChainLength);
}

void UCatPhysicsPrototypeVisualComponent::DestroyVisualComponents()
{
	SetComponentTickEnabled(false);
	for (UActorComponent* Component : { static_cast<UActorComponent*>(VisualMesh.Get()),
		static_cast<UActorComponent*>(AnimationSource.Get()) })
	{
		if (!IsValid(Component)) continue;
		if (AActor* Owner = GetOwner()) Owner->RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	VisualMesh = nullptr;
	AnimationSource = nullptr;
	BodyRoot.Reset();
	LeftHand.Reset();
	RightHand.Reset();
	ComponentPose.Reset();
}

void UCatPhysicsPrototypeVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyVisualComponents();
	Super::EndPlay(EndPlayReason);
}
