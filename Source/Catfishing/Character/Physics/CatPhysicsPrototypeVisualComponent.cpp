#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimStateMachineTypes.h"
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
	constexpr float ReachBlendSpeed = 8.0f;
	constexpr int32 SolverIterations = 12;
	constexpr double ReachToleranceCentimeters = 0.15;
	constexpr float PoseBlendSeconds = 0.08f;
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
	static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpStartAsset(
		TEXT("/Game/Animalia/Cat/Animations/InPlace/JumpX_Start-IP.JumpX_Start-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpLoopAsset(
		TEXT("/Game/Animalia/Cat/Animations/InPlace/JumpX_Loop-IP.JumpX_Loop-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpEndAsset(
		TEXT("/Game/Animalia/Cat/Animations/InPlace/JumpX_End-IP.JumpX_End-IP"));
	CharacterMesh = MeshAsset.Object;
	IdleAnimation = IdleAsset.Object;
	WalkAnimation = WalkAsset.Object;
	JumpStartAnimation = JumpStartAsset.Object;
	JumpLoopAnimation = JumpLoopAsset.Object;
	JumpEndAnimation = JumpEndAsset.Object;
}

bool UCatPhysicsPrototypeVisualComponent::InitializeVisual(USceneComponent* InBodyRoot,
	UPrimitiveComponent* InLeftHand, UPrimitiveComponent* InRightHand, USkeletalMeshComponent* ExistingAnimationSource)
{
	AActor* Owner = GetOwner();
	if (ExistingAnimationSource) CharacterMesh = ExistingAnimationSource->GetSkeletalMeshAsset();
	if (VisualMesh)
	{
		return BodyRoot == InBodyRoot && LeftHand == InLeftHand && RightHand == InRightHand;
	}
	if (!Owner || !InBodyRoot || !InLeftHand || !InRightHand || InBodyRoot->GetOwner() != Owner
		|| InLeftHand->GetOwner() != Owner || InRightHand->GetOwner() != Owner
		|| !CharacterMesh || (!ExistingAnimationSource && (!IdleAnimation || !WalkAnimation || !JumpStartAnimation || !JumpLoopAnimation || !JumpEndAnimation)))
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=physics_prototype_visual_init_failed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=MissingOrMismatchedDependency"),
			*GetNameSafe(Owner), *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			Owner ? Owner->HasAuthority() : false, Owner ? static_cast<int32>(Owner->GetLocalRole()) : INDEX_NONE);
		return false;
	}

	const FReferenceSkeleton& Skeleton = CharacterMesh->GetRefSkeleton();
	const auto ResolveChain = [&Skeleton](const FCatCharacterLimbChain& Definition, TArray<int32>& Chain)
	{
		Chain.Reset();
		if (Definition.Bones.Num() < 3 || Definition.Bones.Num() > 16) return false;
		for (const FName Name : Definition.Bones)
		{
			const int32 Bone = Skeleton.FindBoneIndex(Name);
			if (Bone == INDEX_NONE || (!Chain.IsEmpty() && Skeleton.GetParentIndex(Bone) != Chain.Last())) return false;
			Chain.Add(Bone);
		}
		return true;
	};
	if (RigSettings.Feet.Num() != 4 || !ResolveChain(RigSettings.Feet[0], LeftChain)
		|| !ResolveChain(RigSettings.Feet[1], RightChain))
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=physics_prototype_visual_init_failed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d RigId=%s Mesh=%s Reason=InvalidConfiguredHandChain"),
			*Owner->GetName(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Owner->HasAuthority(),
			int32(Owner->GetLocalRole()), *RigSettings.RigId.ToString(), *CharacterMesh->GetPathName());
		return false;
	}
	JumpRootIndex = Skeleton.FindBoneIndex(RigSettings.JumpRootBone);
	Locomotion.ConfigureRig(RigSettings);

	BodyRoot = InBodyRoot;
	LeftHand = InLeftHand;
	RightHand = InRightHand;
	const FTransform MeshRelativeTransform = ExistingAnimationSource
		? ExistingAnimationSource->GetComponentTransform().GetRelativeTransform(InBodyRoot->GetComponentTransform())
		: FTransform(FRotator(0.0, -90.0, 0.0), FVector(0.0, 0.0, -20.0));
	bOwnsAnimationSource = ExistingAnimationSource == nullptr;
	if (bOwnsAnimationSource)
	{
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
	}
	else
	{
		AnimationSource=ExistingAnimationSource;
		AnimationSource->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AnimationSource->SetHiddenInGame(true,false);
		AnimationSource->SetVisibility(false,false);
		AnimationSource->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		AnimationSource->bOnlyAllowAutonomousTickPose=false;
		AnimationSource->SetComponentTickEnabled(true);
		PrimaryComponentTick.AddPrerequisite(AnimationSource,AnimationSource->PrimaryComponentTick);
	}

	VisualMesh = NewObject<UPoseableMeshComponent>(Owner, TEXT("PhysicsPrototypeVisualMesh"));
	Owner->AddInstanceComponent(VisualMesh);
	VisualMesh->SetupAttachment(InBodyRoot);
	VisualMesh->SetRelativeTransform(MeshRelativeTransform);
	VisualMesh->SetSkinnedAssetAndUpdate(CharacterMesh);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCastShadow(true);
	for (int32 MaterialIndex=0;MaterialIndex<AnimationSource->GetNumMaterials();++MaterialIndex)
		VisualMesh->SetMaterial(MaterialIndex,AnimationSource->GetMaterial(MaterialIndex));
	VisualMesh->SetOwnerNoSee(AnimationSource->bOwnerNoSee);
	VisualMesh->RegisterComponent();
	VisualMesh->SetComponentTickEnabled(false);
	ComponentPose.SetNum(Skeleton.GetNum());
	RefreshVisualPose(0.0f);
	SetComponentTickEnabled(GetWorld()->GetNetMode() != NM_DedicatedServer);
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=physics_prototype_visual_ready Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Mesh=%s BoneCount=%d RigId=%s LeftJoints=%d RightJoints=%d HandSolver=CCD"),
		*Owner->GetName(), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
		Owner->HasAuthority(), static_cast<int32>(Owner->GetLocalRole()), *GetNameSafe(CharacterMesh), Skeleton.GetNum(), *RigSettings.RigId.ToString(), LeftChain.Num(), RightChain.Num());
	return true;
}

void UCatPhysicsPrototypeVisualComponent::SetHandReachState(const bool bInLeftActive, const bool bInRightActive)
{
	bLeftActive = bInLeftActive;
	bRightActive = bInRightActive;
}

UAnimationAsset* UCatPhysicsPrototypeVisualComponent::ResolveAnimationAsset(UAnimationAsset* Source) const
{
	if (!Source) return nullptr;
	UAnimationAsset* Resolved = Source;
	if (const TObjectPtr<UAnimationAsset>* Override = AnimationOverrides.Find(FSoftObjectPath(Source))) Resolved = Override->Get();
	if (Resolved && CharacterMesh && Resolved->GetSkeleton() != CharacterMesh->GetSkeleton())
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=character_animation_rejected Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d RigId=%s Source=%s Resolved=%s Reason=SkeletonMismatch"),
			*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : INDEX_NONE,
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? int32(GetOwner()->GetLocalRole()) : INDEX_NONE,
			*RigSettings.RigId.ToString(), *GetPathNameSafe(Source), *GetPathNameSafe(Resolved));
		return nullptr;
	}
	if (Resolved != Source)
	{
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=character_animation_resolved Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d RigId=%s Source=%s Resolved=%s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : INDEX_NONE,
			GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? int32(GetOwner()->GetLocalRole()) : INDEX_NONE,
			*RigSettings.RigId.ToString(), *GetPathNameSafe(Source), *GetPathNameSafe(Resolved));
	}
	return Resolved;
}

FVector UCatPhysicsPrototypeVisualComponent::GetVisualHandWorldLocation(const bool bLeftHand) const
{
	const TArray<int32>& Chain = bLeftHand ? LeftChain : RightChain;
	return VisualMesh && !Chain.IsEmpty() ? VisualMesh->GetBoneLocationByName(
		CharacterMesh->GetRefSkeleton().GetBoneName(Chain.Last()), EBoneSpaces::WorldSpace) : FVector::ZeroVector;
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
	if (bOwnsAnimationSource)
	{
		UpdateBaseAnimation(SafeDelta);
		AnimationSource->TickAnimation(SafeDelta,false);
		AnimationSource->RefreshBoneTransforms();
	}
	VisualMesh->CopyPoseFromSkeletalComponent(AnimationSource);
	if (bOwnsAnimationSource && (AnimationState == EAnimationState::Takeoff || AnimationState == EAnimationState::Airborne
		|| AnimationState == EAnimationState::Landing))
	{
		// Authored jump clips can translate the configured root vertically. Match UE's RefPose root lock
		// in this private pose: the physical body owns the global transform, while the authored limbs keep animating.
		if (VisualMesh->BoneSpaceTransforms.IsValidIndex(JumpRootIndex))
			VisualMesh->BoneSpaceTransforms[JumpRootIndex] = CharacterMesh->GetRefSkeleton().GetRefBonePose()[JumpRootIndex];
	}
	TransitionSeconds += SafeDelta;
	if (TransitionSeconds < CatPhysicsPrototypeVisual::PoseBlendSeconds
		&& TransitionFromPose.Num() == VisualMesh->BoneSpaceTransforms.Num())
	{
		const float Alpha = TransitionSeconds / CatPhysicsPrototypeVisual::PoseBlendSeconds;
		for (int32 Index = 0; Index < TransitionFromPose.Num(); ++Index)
		{
			const FTransform Destination = VisualMesh->BoneSpaceTransforms[Index];
			VisualMesh->BoneSpaceTransforms[Index].Blend(TransitionFromPose[Index], Destination, Alpha);
		}
	}
	// Compensate configured root travel while authoritative movement already moves the body.
	// Correct only the visible vertical root, including Land blend-out; retain the
	// authored animation source, horizontal root, rotations and limb poses.
	const bool bCompensateJumpRoot = !bOwnsAnimationSource && IsFormalJumpPoseActive()
		&& VisualMesh->BoneSpaceTransforms.IsValidIndex(JumpRootIndex);
	if (bCompensateJumpRoot)
	{
		FVector RootTranslation = VisualMesh->BoneSpaceTransforms[JumpRootIndex].GetTranslation();
		RootTranslation.Z = CharacterMesh->GetRefSkeleton().GetRefBonePose()[JumpRootIndex].GetTranslation().Z;
		VisualMesh->BoneSpaceTransforms[JumpRootIndex].SetTranslation(RootTranslation);
	}
	if (bCompensateJumpRoot != bFormalJumpRootCompensationActive)
	{
		bFormalJumpRootCompensationActive = bCompensateJumpRoot;
		const auto* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=physics_visual_jump_root_compensation Actor=%s BodyId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Active=%d Result=VisibleVerticalRootOnly"),
			*GetNameSafe(GetOwner()), PhysicalBody ? *PhysicalBody->GetBodyId().ToString() : TEXT("None"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(),
			int32(GetOwner()->GetLocalRole()), bCompensateJumpRoot);
	}
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
	// Keep the displayed base pose before CCD, so a new jump can interrupt a partially blended landing smoothly.
	LastBasePose = VisualMesh->BoneSpaceTransforms;
	LeftReachAlpha = FMath::FInterpConstantTo(LeftReachAlpha, bLeftActive ? 1.0f : 0.0f,
		SafeDelta, CatPhysicsPrototypeVisual::ReachBlendSpeed);
	RightReachAlpha = FMath::FInterpConstantTo(RightReachAlpha, bRightActive ? 1.0f : 0.0f,
		SafeDelta, CatPhysicsPrototypeVisual::ReachBlendSpeed);
	Locomotion.Apply(AnimationSource, VisualMesh, GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(),
		LeftReachAlpha, RightReachAlpha, DeltaTime, LocomotionSettings);
	RebuildComponentPose();
	const auto* Grab = GetOwner()->FindComponentByClass<UCatPhysicsGrabComponent>();
	const auto HandTarget = [&](const bool bLeft, const UPrimitiveComponent* Hand)
	{
		return Grab && Grab->IsGripping(bLeft) && Grab->GetGripState(bLeft).bControlledHold
			? Grab->GetGripWorldLocation(bLeft) : Hand->GetComponentLocation();
	};
	SolveHandReach(true, HandTarget(true, LeftHand.Get()), LeftReachAlpha);
	SolveHandReach(false, HandTarget(false, RightHand.Get()), RightReachAlpha);
	VisualMesh->RefreshBoneTransforms();
}

bool UCatPhysicsPrototypeVisualComponent::IsFormalJumpPoseActive() const
{
	UAnimInstance* Animation = AnimationSource ? AnimationSource->GetAnimInstance() : nullptr;
	if (!Animation || Animation->IsAnyMontagePlaying()) return false;
	int32 MachineIndex = INDEX_NONE;
	const FBakedAnimationStateMachine* Description = nullptr;
	Animation->GetStateMachineIndexAndDescription(RigSettings.JumpStateMachine, MachineIndex, &Description);
	if (MachineIndex == INDEX_NONE || !Description) return false;
	for (const FName StateName : RigSettings.JumpStates)
	{
		const int32 StateIndex = Description->FindStateIndex(StateName);
		if (StateIndex != INDEX_NONE && Animation->GetInstanceStateWeight(MachineIndex, StateIndex) > UE_SMALL_NUMBER)
			return true;
	}
	return false;
}

void UCatPhysicsPrototypeVisualComponent::UpdateBaseAnimation(const float DeltaTime)
{
	const auto* Pawn = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	if (!Pawn) return;
	if (ObservedResetEpoch != Pawn->GetResetEpoch() || !Pawn->HasMovementSample())
	{
		ObservedResetEpoch = Pawn->GetResetEpoch();
		bHasMovementSample = false;
		TakeoffConfirmationSeconds = 0.0f;
		if (AnimationState != EAnimationState::Idle) PlayBaseAnimation(EAnimationState::Idle);
		AnimationStateSeconds = 0.0f;
		TransitionFromPose.Reset();
		return;
	}
	const bool bGrounded = Pawn->IsGrounded();
	const FVector Velocity = Pawn->GetVelocity(); // Server body or authoritative client snapshot, in cm/s.
	const float Speed = Velocity.Size2D();
	AnimationStateSeconds += DeltaTime;
	EAnimationState DesiredState = AnimationState;
	if (!bGrounded)
	{
		// Keep a short confirmation window around a support edge so network sampling can
		// confirm takeoff without replaying it on subsequent airborne snapshots.
		if (bHasMovementSample && bWasGrounded) TakeoffConfirmationSeconds = 0.12f;
		if (TakeoffConfirmationSeconds > 0.0f && Velocity.Z > 20.0)
		{
			DesiredState = EAnimationState::Takeoff;
			TakeoffConfirmationSeconds = 0.0f;
		}
		else
		{
			TakeoffConfirmationSeconds = Velocity.Z < -5.0 ? 0.0f : FMath::Max(0.0f, TakeoffConfirmationSeconds - DeltaTime);
			if (AnimationState != EAnimationState::Takeoff
				|| AnimationStateSeconds >= JumpStartAnimation->GetPlayLength() || Velocity.Z <= 0.0)
				DesiredState = EAnimationState::Airborne;
		}
	}
	else if (bHasMovementSample && !bWasGrounded)
	{
		DesiredState = EAnimationState::Landing;
	}
	else if (AnimationState != EAnimationState::Landing || AnimationStateSeconds >= JumpEndAnimation->GetPlayLength())
	{
		const bool bShouldWalk = FMath::IsFinite(Speed) && Speed > (AnimationState == EAnimationState::Walk ? 3.0f : 6.0f);
		DesiredState = bShouldWalk ? EAnimationState::Walk : EAnimationState::Idle;
	}
	PlayBaseAnimation(DesiredState);
	if (bGrounded) TakeoffConfirmationSeconds = 0.0f;
	bWasGrounded = bGrounded;
	bHasMovementSample = true;
	const double ReferenceSpeedCmS = Locomotion.GetReferenceSpeedMeshCmS(WalkAnimation, CharacterMesh)
		* AnimationSource->GetComponentScale().GetAbsMax();
	const float PlayRate = AnimationState == EAnimationState::Walk && ReferenceSpeedCmS > 1.0
		? FMath::Clamp(Speed / ReferenceSpeedCmS, 0.3, 3.5) : 1.0f;
	AnimationSource->SetPlayRate(PlayRate);
}

void UCatPhysicsPrototypeVisualComponent::PlayBaseAnimation(const EAnimationState NewState)
{
	if (NewState == AnimationState) return;
	TransitionFromPose = LastBasePose.Num() ? LastBasePose : AnimationSource->GetBoneSpaceTransforms();
	TransitionSeconds = 0.0f;
	const EAnimationState PreviousState = AnimationState;
	AnimationState = NewState;
	AnimationStateSeconds = 0.0f;
	UAnimSequence* Animation = IdleAnimation;
	switch (NewState)
	{
	case EAnimationState::Walk: Animation = WalkAnimation; break;
	case EAnimationState::Takeoff: Animation = JumpStartAnimation; break;
	case EAnimationState::Airborne: Animation = JumpLoopAnimation; break;
	case EAnimationState::Landing: Animation = JumpEndAnimation; break;
	default: break;
	}
	AnimationSource->PlayAnimation(Animation, NewState != EAnimationState::Takeoff && NewState != EAnimationState::Landing);
	static const TCHAR* Names[] = {TEXT("Idle"), TEXT("Walk"), TEXT("Takeoff"), TEXT("Airborne"), TEXT("Landing")};
	const auto* Pawn = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=physics_prototype_animation_changed Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d BodyId=%s From=%s To=%s Animation=%s Grounded=%d VelocityZCmS=%.2f ResetEpoch=%u"),
		*GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
		GetOwner()->HasAuthority(), static_cast<int32>(GetOwner()->GetLocalRole()),
		Pawn ? *Pawn->GetBodyId().ToString() : TEXT("None"), Names[static_cast<uint8>(PreviousState)], Names[static_cast<uint8>(NewState)],
		*GetNameSafe(Animation), Pawn && Pawn->IsGrounded(), GetOwner()->GetVelocity().Z,
		Pawn ? Pawn->GetResetEpoch() : 0);
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
	const TArray<int32>& Chain = bLeftHand ? LeftChain : RightChain;
	const int32 Last = Chain.Num() - 1;
	const FVector Shoulder = ComponentPose[Chain[0]].GetLocation();
	const FVector AnimatedHand = ComponentPose[Chain[Last]].GetLocation();
	const FVector Target = FMath::Lerp(AnimatedHand,
		VisualMesh->GetComponentTransform().InverseTransformPosition(TargetWorld), static_cast<double>(Alpha));
	double ChainLength = 0.0;
	for (int32 Link = 1; Link <= Last; ++Link)
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
		if (FVector::DistSquared(ComponentPose[Chain[Last]].GetLocation(), ReachTarget)
			<= FMath::Square(CatPhysicsPrototypeVisual::ReachToleranceCentimeters))
		{
			break;
		}
		for (int32 Link = Last - 1; Link >= 0; --Link)
		{
			const int32 Joint = Chain[Link];
			const FVector JointPosition = ComponentPose[Joint].GetLocation();
			const FVector ToEnd = ComponentPose[Chain[Last]].GetLocation() - JointPosition;
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
	Locomotion.Reset();
	SetComponentTickEnabled(false);
	for (UActorComponent* Component : { static_cast<UActorComponent*>(VisualMesh.Get()),
		static_cast<UActorComponent*>(AnimationSource.Get()) })
	{
		if (!IsValid(Component) || (!bOwnsAnimationSource && Component == AnimationSource)) continue;
		if (AActor* Owner = GetOwner()) Owner->RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	VisualMesh = nullptr;
	AnimationSource = nullptr;
	BodyRoot.Reset();
	LeftHand.Reset();
	RightHand.Reset();
	ComponentPose.Reset();
	TransitionFromPose.Reset();
	LastBasePose.Reset();
	bFormalJumpRootCompensationActive = false;
}

void UCatPhysicsPrototypeVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyVisualComponents();
	Super::EndPlay(EndPlayReason);
}
