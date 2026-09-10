#include "Character/Animation/CatQuadrupedLocomotion.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatLocomotion, Log, All);

namespace CatQuadruped
{
	double Smooth(double Current, double Target, double Seconds, double Delta)
	{
		return FMath::Lerp(Current, Target, 1.0 - FMath::Exp(-FMath::Max(0.0, Delta) / FMath::Max(0.01, Seconds)));
	}
	FTransform SupportTransform(const UPrimitiveComponent* Support, FName Bone)
	{
		return Bone.IsNone() ? Support->GetComponentTransform() : Support->GetSocketTransform(Bone, RTS_World);
	}
}

void FCatQuadrupedLocomotion::ClearPlants()
{
	for (FFoot& Foot : Feet)
	{
		Foot.bPlanted = false;
		Foot.bReleasedUntilSwing = false;
		Foot.Support.Reset();
		Foot.SupportBone = NAME_None;
		Foot.PlantOffsetWorld = FVector::ZeroVector;
	}
}

void FCatQuadrupedLocomotion::Reset()
{
	ClearPlants();
	for (FFoot& Foot : Feet) Foot.OffsetWorld = FVector::ZeroVector;
	Alpha = 0.0;
	StrideScale = 1.0;
	PelvisOffset = 0.0;
	bHasPreviousFrame = false;
	MotionSupport.Reset();
	MotionSupportBone = NAME_None;
	Observation = FCatQuadrupedLocomotionObservation();
}

void FCatQuadrupedLocomotion::ConfigureRig(const FCatCharacterRigSettings& Settings)
{
	Reset();
	RigSettings = Settings;
	Mesh.Reset();
	Profiles.Reset();
	bInitialized = false;
	bSkeletonChecked = false;
}

bool FCatQuadrupedLocomotion::Initialize(USkeletalMesh* InMesh)
{
	Reset();
	Mesh = InMesh;
	Profiles.Reset();
	bSkeletonChecked = true;
	bInitialized = false;
	if (!InMesh || RigSettings.Feet.Num() != FootCount || RigSettings.ForwardAxis.ContainsNaN()
		|| RigSettings.ForwardAxis.GetSafeNormal2D().IsNearlyZero()) return false;
	const FReferenceSkeleton& Skeleton = InMesh->GetRefSkeleton();
	PelvisIndex = Skeleton.FindBoneIndex(RigSettings.PelvisBone);
	if (PelvisIndex == INDEX_NONE) return false;
	for (int32 Foot = 0; Foot < FootCount; ++Foot)
	{
		const auto& Names = RigSettings.Feet[Foot].Bones;
		if (Names.Num() < 3 || Names.Num() > 16) return false;
		Feet[Foot].Chain.Reset();
		for (int32 Joint = 0; Joint < Names.Num(); ++Joint)
		{
			const int32 Bone = Skeleton.FindBoneIndex(Names[Joint]);
			if (Bone == INDEX_NONE || (Joint > 0 && Skeleton.GetParentIndex(Bone) != Feet[Foot].Chain.Last())) return false;
			Feet[Foot].Chain.Add(Bone);
		}
	}
	ComponentPose.SetNum(Skeleton.GetNum());
	RebuildPose(Skeleton.GetRefBonePose());
	for (FFoot& Foot : Feet)
		Foot.SoleNormalLocal = ComponentPose[Foot.Chain.Last()].GetRotation().Inverse().RotateVector(FVector::UpVector);
	bInitialized = true;
	return true;
}

void FCatQuadrupedLocomotion::RebuildPose(const TArray<FTransform>& LocalPose)
{
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	for (int32 Bone = 0; Bone < ComponentPose.Num(); ++Bone)
	{
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		ComponentPose[Bone] = Parent == INDEX_NONE ? LocalPose[Bone] : LocalPose[Bone] * ComponentPose[Parent];
		ComponentPose[Bone].NormalizeRotation();
	}
}

double FCatQuadrupedLocomotion::GetReferenceSpeedMeshCmS(UAnimSequence* Sequence, USkeletalMesh* InMesh)
{
	if (!bSkeletonChecked || Mesh.Get() != InMesh) Initialize(InMesh);
	if (!bInitialized) return 0.0;
	const FProfile& Profile = GetProfile(Sequence);
	return Profile.bValid ? Profile.SpeedCmS : 0.0;
}

const FCatQuadrupedLocomotion::FProfile& FCatQuadrupedLocomotion::GetProfile(UAnimSequence* Sequence)
{
	if (const FProfile* Found = Profiles.Find(Sequence)) return *Found;
	FProfile& Profile = Profiles.Add(Sequence);
	if (!Sequence || !Mesh.IsValid() || Sequence->GetSkeleton() != Mesh->GetSkeleton()) return Profile;
	const auto Matches = [Sequence](const TSoftObjectPtr<UAnimSequence>& Clip) { return Clip.ToSoftObjectPath() == FSoftObjectPath(Sequence); };
	Profile.bLocomotion = RigSettings.LocomotionAnimations.ContainsByPredicate(Matches);
	// Only this rig's explicitly configured clips participate; actions retain their authored poses.
	if (!Profile.bLocomotion && !RigSettings.StandingAnimations.ContainsByPredicate(Matches)) return Profile;
	Profile.Duration = Sequence->GetPlayLength();
	if (Profile.Duration <= UE_SMALL_NUMBER) return Profile;
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const FReferenceSkeleton& AnimSkeleton = Sequence->GetSkeleton()->GetReferenceSkeleton();
	TArray<int32> RequiredBones;
	TArray<int32> AnimationBoneIndices;
	for (const FFoot& Foot : Feet)
		for (int32 Bone = Foot.Chain.Last(); Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone)) RequiredBones.AddUnique(Bone);
	RequiredBones.Sort();
	for (const int32 Bone : RequiredBones)
	{
		const int32 AnimBone = AnimSkeleton.FindBoneIndex(Skeleton.GetBoneName(Bone));
		if (AnimBone == INDEX_NONE) return Profile;
		AnimationBoneIndices.Add(AnimBone);
	}
	TArray<FVector> Positions[FootCount];
	for (int32 Foot = 0; Foot < FootCount; ++Foot) Profile.FloorZ[Foot] = TNumericLimits<double>::Max();
	TArray<FTransform> Pose;
	Pose.SetNum(Skeleton.GetNum());
	for (int32 Sample = 0; Sample <= SampleCount; ++Sample)
	{
		const FAnimExtractContext Context(Profile.Duration * Sample / SampleCount, false);
		for (int32 Index = 0; Index < RequiredBones.Num(); ++Index)
		{
			const int32 Bone = RequiredBones[Index];
			FTransform Local;
			// Compressed runtime animation data: calibration is also available in a cooked Development game.
			Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(AnimationBoneIndices[Index]), Context, false);
			const int32 Parent = Skeleton.GetParentIndex(Bone);
			Pose[Bone] = Parent == INDEX_NONE ? Local : Local * Pose[Parent];
		}
		for (int32 Foot = 0; Foot < FootCount; ++Foot)
		{
			const FVector Position = Pose[Feet[Foot].Chain.Last()].GetLocation();
			if (Position.ContainsNaN()) return Profile;
			Positions[Foot].Add(Position);
			Profile.FloorZ[Foot] = FMath::Min(Profile.FloorZ[Foot], Position.Z);
		}
	}
	TArray<double> StanceSpeeds;
	const double SampleDelta = Profile.Duration / SampleCount;
	for (int32 Foot = 0; Foot < FootCount; ++Foot)
	{
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			const double Height = Positions[Foot][Sample].Z - Profile.FloorZ[Foot];
			const FVector Velocity = (Positions[Foot][Sample + 1] - Positions[Foot][Sample]) / SampleDelta;
			// Calibrate backward planted-paw travel in this rig's authored forward direction.
			const double Contact = Profile.bLocomotion ? 1.0 - FMath::SmoothStep(0.25, 1.1, Height) : 1.0;
			Profile.Contacts[Foot].Add(Contact);
			const double ForwardSpeed = FVector::DotProduct(Velocity, RigSettings.ForwardAxis.GetSafeNormal2D());
			if (Profile.bLocomotion && Contact > 0.65 && ForwardSpeed < -1.0) StanceSpeeds.Add(-ForwardSpeed);
		}
	}
	if (Profile.bLocomotion && StanceSpeeds.IsEmpty()) return Profile;
	if (!StanceSpeeds.IsEmpty())
	{
		StanceSpeeds.Sort();
		Profile.SpeedCmS = StanceSpeeds[StanceSpeeds.Num() / 2];
	}
	Profile.bValid = true;
	UE_LOG(LogCatLocomotion, Log,
		TEXT("Event=locomotion_clip_calibrated Animation=%s Mesh=%s ReferenceSpeedMeshCmS=%.3f DurationS=%.3f Samples=%d Moving=%d Result=CompressedStanceTrajectory"),
		*Sequence->GetPathName(), *Mesh->GetPathName(), Profile.SpeedCmS, Profile.Duration, SampleCount, Profile.bLocomotion);
	return Profile;
}

FCatQuadrupedLocomotion::FAnimationFrame FCatQuadrupedLocomotion::ReadAnimation(USkeletalMeshComponent* Source, float DeltaSeconds)
{
	FAnimationFrame Frame;
	UAnimInstance* Animation = Source ? Source->GetAnimInstance() : nullptr;
	if (!Animation || Animation->IsAnyMontagePlaying()) return Frame;
	const auto AddSequence = [&](UAnimSequence* Sequence, double Time, double Rate, double Weight)
	{
		if (Weight < 0.001 || !FMath::IsFinite(Time) || !FMath::IsFinite(Rate)) return;
		const FProfile& Profile = GetProfile(Sequence);
		if (!Profile.bValid) return;
		Frame.Weight += Weight;
		Frame.SpeedCmS += Profile.SpeedCmS * FMath::Abs(Rate) * Weight;
		const double Position = FMath::Fmod(FMath::Max(0.0, Time), Profile.Duration) / Profile.Duration * SampleCount;
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Position), 0, SampleCount - 1);
		for (int32 Foot = 0; Foot < FootCount; ++Foot)
		{
			Frame.FloorZ[Foot] += Profile.FloorZ[Foot] * Weight;
			Frame.Contact[Foot] += FMath::Lerp(Profile.Contacts[Foot][Index],
				Profile.Contacts[Foot][(Index + 1) % SampleCount], Position - Index) * Weight;
		}
	};
	const auto AddRecord = [&](const FAnimTickRecord& Record)
	{
		if (Record.bIsEvaluator || Record.EffectiveBlendWeight < 0.001f) return;
		if (Cast<UBlendSpace>(Record.SourceAsset))
		{
			if (!Record.BlendSpace.BlendSampleDataCache) return;
			for (const FBlendSampleData& Sample : *Record.BlendSpace.BlendSampleDataCache)
			{
				const double Rate = DeltaSeconds > UE_SMALL_NUMBER && Sample.DeltaTimeRecord.IsPreviousValid()
					? Sample.DeltaTimeRecord.Delta / DeltaSeconds : Record.PlayRateMultiplier * Sample.SamplePlayRate;
				AddSequence(Sample.Animation, Sample.Time, Rate, Record.EffectiveBlendWeight * Sample.GetClampedWeight());
			}
		}
		else if (UAnimSequence* Sequence = Cast<UAnimSequence>(Record.SourceAsset))
		{
			const double Rate = DeltaSeconds > UE_SMALL_NUMBER && Record.DeltaTimeRecord && Record.DeltaTimeRecord->IsPreviousValid()
				? Record.DeltaTimeRecord->Delta / DeltaSeconds : Record.PlayRateMultiplier * Sequence->RateScale;
			AddSequence(Sequence, Record.TimeAccumulator ? *Record.TimeAccumulator : 0.0, Rate, Record.EffectiveBlendWeight);
		}
	};
	for (const auto& Group : Animation->GetSyncGroupMapRead())
		for (const FAnimTickRecord& Record : Group.Value.ActivePlayers) AddRecord(Record);
	for (const FAnimTickRecord& Record : Animation->GetUngroupedActivePlayersRead()) AddRecord(Record);
	if (Frame.Weight > UE_SMALL_NUMBER)
	{
		Frame.SpeedCmS /= Frame.Weight;
		for (int32 Foot = 0; Foot < FootCount; ++Foot)
		{
			Frame.FloorZ[Foot] /= Frame.Weight;
			Frame.Contact[Foot] /= Frame.Weight;
		}
	}
	return Frame;
}

void FCatQuadrupedLocomotion::SolveFoot(TArray<FTransform>& LocalPose, int32 FootIndex,
	const FVector& TargetComponent, const FQuat& RotationComponent)
{
	const TArray<int32>& Chain = Feet[FootIndex].Chain;
	const int32 Last = Chain.Num() - 1;
	TArray<FVector, TInlineAllocator<16>> Points;
	TArray<double, TInlineAllocator<16>> Lengths;
	Points.SetNum(Chain.Num());
	Lengths.SetNum(Last);
	double TotalLength = 0.0;
	for (int32 Joint = 0; Joint <= Last; ++Joint) Points[Joint] = ComponentPose[Chain[Joint]].GetLocation();
	for (int32 Joint = 0; Joint < Last; ++Joint) { Lengths[Joint] = FVector::Distance(Points[Joint], Points[Joint + 1]); TotalLength += Lengths[Joint]; }
	const FVector Root = Points[0];
	const FVector Target = Root + (TargetComponent - Root).GetClampedToMaxSize(TotalLength * 0.9999);
	// Preserve the authored bend and every segment length for either skeleton.
	for (int32 Iteration = 0; Iteration < 16; ++Iteration)
	{
		Points[Last] = Target;
		for (int32 Joint = Last - 1; Joint >= 0; --Joint)
			Points[Joint] = Points[Joint + 1] + (Points[Joint] - Points[Joint + 1]).GetSafeNormal() * Lengths[Joint];
		Points[0] = Root;
		for (int32 Joint = 1; Joint <= Last; ++Joint)
			Points[Joint] = Points[Joint - 1] + (Points[Joint] - Points[Joint - 1]).GetSafeNormal() * Lengths[Joint - 1];
		if (FVector::DistSquared(Points[Last], Target) < FMath::Square(0.02)) break;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	for (int32 Joint = 0; Joint < Last; ++Joint)
	{
		const int32 Bone = Chain[Joint];
		const FVector CurrentDirection = (ComponentPose[Chain[Joint + 1]].GetLocation() - ComponentPose[Bone].GetLocation()).GetSafeNormal();
		const FVector WantedDirection = (Points[Joint + 1] - Points[Joint]).GetSafeNormal();
		const FQuat Rotation = (FQuat::FindBetweenNormals(CurrentDirection, WantedDirection) * ComponentPose[Bone].GetRotation()).GetNormalized();
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		LocalPose[Bone].SetRotation(Parent == INDEX_NONE ? Rotation : (ComponentPose[Parent].GetRotation().Inverse() * Rotation).GetNormalized());
		RebuildPose(LocalPose);
	}
	const int32 Parent = Skeleton.GetParentIndex(Chain[Last]);
	LocalPose[Chain[Last]].SetRotation((ComponentPose[Parent].GetRotation().Inverse() * RotationComponent).GetNormalized());
	RebuildPose(LocalPose);
}

void FCatQuadrupedLocomotion::Apply(USkeletalMeshComponent* Source, UPoseableMeshComponent* Visual,
	const UCatPhysicalBodyComponent* Body, float LeftReachAlpha, float RightReachAlpha,
	float DeltaSeconds, const FCatQuadrupedLocomotionSettings& Settings)
{
	if (!Source || !Visual || !Body || !Body->GetBody() || !Visual->GetWorld()) return;
	const FName PreviousMode = Observation.Mode;
	Observation = FCatQuadrupedLocomotionObservation();
	const bool bValidSettings = FMath::IsFinite(Settings.MinStrideScale) && Settings.MinStrideScale >= 0.1f && Settings.MinStrideScale <= 1.0f
		&& FMath::IsFinite(Settings.MaxStrideScale) && Settings.MaxStrideScale >= 1.0f && Settings.MaxStrideScale <= 2.0f
		&& FMath::IsFinite(Settings.MaxFootOffsetCm) && Settings.MaxFootOffsetCm >= 0.0f
		&& FMath::IsFinite(Settings.MaxPelvisOffsetCm) && Settings.MaxPelvisOffsetCm >= 0.0f
		&& FMath::IsFinite(Settings.MaxPlantDriftCm) && Settings.MaxPlantDriftCm >= 0.0f
		&& FMath::IsFinite(Settings.BlendSeconds) && Settings.BlendSeconds >= 0.01f;
	if (!bValidSettings)
	{
		Reset();
		Observation.Mode = TEXT("InvalidSettings");
		if (PreviousMode != Observation.Mode)
			UE_LOG(LogCatLocomotion, Warning, TEXT("Event=locomotion_settings_rejected Actor=%s BodyId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=InvalidRangeOrNonFinite"),
				*GetNameSafe(Body->GetOwner()), *Body->GetBodyId().ToString(), *GetNameSafe(Body->GetWorld()),
				int32(Body->GetOwner()->GetNetMode()), Body->GetOwner()->HasAuthority(), int32(Body->GetOwner()->GetLocalRole()));
		return;
	}
	USkeletalMesh* VisualAsset = Cast<USkeletalMesh>(Visual->GetSkinnedAsset());
	if (!bSkeletonChecked || Mesh.Get() != VisualAsset)
	{
		if (!Initialize(VisualAsset))
			UE_LOG(LogCatLocomotion, Warning, TEXT("Event=locomotion_skeleton_rejected Actor=%s BodyId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Mesh=%s Result=MissingCatBoneChain"),
				*GetNameSafe(Visual->GetOwner()), *Body->GetBodyId().ToString(), *GetNameSafe(Body->GetWorld()),
				int32(Body->GetOwner()->GetNetMode()), Body->GetOwner()->HasAuthority(), int32(Body->GetOwner()->GetLocalRole()), *GetNameSafe(VisualAsset));
	}
	if (!bInitialized || Visual->BoneSpaceTransforms.Num() != ComponentPose.Num()) { Observation.Mode = TEXT("InvalidSkeleton"); return; }
	const FTransform MeshWorld = Visual->GetComponentTransform();
	const FVector Scale3D = MeshWorld.GetScale3D();
	const double Scale = Scale3D.GetAbsMax();
	const bool bValidScale = Scale > UE_SMALL_NUMBER && Scale3D.GetMin() > 0.0 && Scale3D.GetMax() - Scale3D.GetMin() < Scale * 0.01;
	const double Delta = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.0f, DeltaSeconds) : 0.0;
	const FVector BodyLocation = Body->GetOwner()->GetActorLocation();
	const FQuat BodyRotation = Body->GetOwner()->GetActorQuat();
	const bool bDiscontinuity = !bHasPreviousFrame || ResetEpoch != Body->GetResetEpoch() || Delta > 0.2
		|| FVector::DistSquared(BodyLocation, PreviousBodyLocation) > FMath::Square(FMath::Max(100.0, Scale * 50.0))
		|| BodyRotation.AngularDistance(PreviousBodyRotation) > PI / 3.0;
	if (bDiscontinuity) Reset();
	ResetEpoch = Body->GetResetEpoch();
	PreviousBodyLocation = BodyLocation;
	PreviousBodyRotation = BodyRotation;
	bHasPreviousFrame = true;
	FAnimationFrame Animation = ReadAnimation(Source, DeltaSeconds);
	const ECollisionChannel SupportChannel = Body->GetBody()->GetCollisionObjectType();
	const FCollisionResponseParams SupportResponses(Body->GetBody()->GetCollisionResponseToChannels());
	FVector SupportVelocity = FVector::ZeroVector;
	FHitResult MotionHit;
	const FVector SupportPoint = Body->GetSupportFootPointWorld();
	FCollisionQueryParams MotionParams(SCENE_QUERY_STAT(CatLocomotionSupport), false, Visual->GetOwner());
	Body->AppendSupportQueryIgnores(MotionParams);
	// Sample platform motion independently of planting, so a moving platform cannot trap IK in Sliding mode.
	if (Body->IsGrounded() && Visual->GetWorld()->LineTraceSingleByChannel(MotionHit,
		SupportPoint + FVector::UpVector * 20.0 * Scale, SupportPoint - FVector::UpVector * 30.0 * Scale, SupportChannel, MotionParams, SupportResponses)
		&& MotionHit.GetComponent() && MotionHit.ImpactNormal.Z >= 0.65)
	{
		UPrimitiveComponent* Support = MotionHit.GetComponent();
		const FTransform Current = CatQuadruped::SupportTransform(Support, MotionHit.BoneName);
		if (Support->IsSimulatingPhysics(MotionHit.BoneName))
			SupportVelocity = Support->GetPhysicsLinearVelocityAtPoint(MotionHit.ImpactPoint, MotionHit.BoneName);
		else if (Delta > UE_SMALL_NUMBER && MotionSupport.Get() == Support && MotionSupportBone == MotionHit.BoneName)
			SupportVelocity = (MotionHit.ImpactPoint - PreviousMotionSupportWorld.TransformPosition(Current.InverseTransformPosition(MotionHit.ImpactPoint))) / Delta;
		MotionSupport = Support;
		MotionSupportBone = MotionHit.BoneName;
		PreviousMotionSupportWorld = Current;
	}
	else MotionSupport.Reset();
	const FVector RelativeVelocity = Body->GetVelocity() - SupportVelocity;
	Observation.BodySpeedCmS = Body->GetVelocity().Size2D();
	Observation.RelativeSpeedCmS = RelativeVelocity.Size2D();
	Observation.SupportSpeedCmS = SupportVelocity.Size2D();
	Observation.AnimationSpeedCmS = Animation.SpeedCmS * Scale;
	const bool bIntent = !Body->GetMoveIntent().IsNearlyZero(0.05);
	const bool bSliding = Observation.RelativeSpeedCmS > 8.0 * Scale && (!bIntent ||
		FVector::DotProduct(RelativeVelocity.GetSafeNormal2D(), Body->GetMoveIntent().GetSafeNormal2D()) < 0.0);
	const bool bEnabled = Settings.bEnabled && bValidScale && Body->HasMovementSample() && Body->IsLocomotionEnabled()
		&& Body->IsGrounded() && Body->GetOwner()->GetActorUpVector().Z > 0.65 && Animation.Weight > 0.8 && !bSliding;
	Observation.Mode = !Settings.bEnabled ? TEXT("Disabled") : !bValidScale ? TEXT("InvalidScale")
		: !Body->HasMovementSample() ? TEXT("AwaitingPhysics") : !Body->IsLocomotionEnabled() ? TEXT("BodyUnavailable")
		: !Body->IsGrounded() ? TEXT("Airborne") : Body->GetOwner()->GetActorUpVector().Z <= 0.65 ? TEXT("Tumbling")
		: Animation.Weight <= 0.8 ? TEXT("AuthoredAction") : bSliding ? TEXT("Sliding") : bIntent ? TEXT("Walking") : TEXT("Standing");
	Alpha = CatQuadruped::Smooth(Alpha, bEnabled ? 1.0 : 0.0, Settings.BlendSeconds, Delta);
	if (!bEnabled) ClearPlants();
	const double TargetStride = bEnabled && bIntent && Animation.SpeedCmS * Scale > 1.0
		? FMath::Clamp(Observation.RelativeSpeedCmS / (Animation.SpeedCmS * Scale), double(Settings.MinStrideScale),
			FMath::Max(double(Settings.MinStrideScale), double(Settings.MaxStrideScale))) : 1.0;
	StrideScale = CatQuadruped::Smooth(StrideScale, TargetStride, Settings.BlendSeconds, Delta);
	Observation.StrideScale = StrideScale;
	Observation.Alpha = Alpha;
	const double Now = Visual->GetWorld()->GetTimeSeconds();
	if (Alpha < 0.001 || !bValidScale)
	{
		LogObservation(Body, Now, PreviousMode != Observation.Mode);
		return;
	}
	RebuildPose(Visual->BoneSpaceTransforms);
	FVector BaseWorld[FootCount], Targets[FootCount];
	FQuat Rotations[FootCount];
	double Weights[FootCount];
	FVector Direction = RelativeVelocity.GetSafeNormal2D();
	if (Direction.IsNearlyZero()) Direction = Body->GetOwner()->GetActorForwardVector().GetSafeNormal2D();
	double PelvisSum = 0.0, PelvisWeight = 0.0;
	for (int32 FootIndex = 0; FootIndex < FootCount; ++FootIndex)
	{
		FFoot& Foot = Feet[FootIndex];
		const double Reach = FootIndex == 0 ? LeftReachAlpha : FootIndex == 1 ? RightReachAlpha : 0.0;
		Weights[FootIndex] = Alpha * (1.0 - FMath::Clamp(Reach, 0.0, 1.0));
		if (Reach > 0.001) Observation.ExcludedFootMask |= 1 << FootIndex;
		const FTransform Base = ComponentPose[Foot.Chain.Last()];
		BaseWorld[FootIndex] = MeshWorld.TransformPosition(Base.GetLocation());
		const FVector HipWorld = MeshWorld.TransformPosition(ComponentPose[Foot.Chain[0]].GetLocation());
		const FVector StrideOffset = Direction * FVector::DotProduct(BaseWorld[FootIndex] - HipWorld, Direction) * (StrideScale - 1.0);
		Targets[FootIndex] = BaseWorld[FootIndex] + StrideOffset;
		Rotations[FootIndex] = Base.GetRotation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CatLocomotionFoot), false, Visual->GetOwner());
		Body->AppendSupportQueryIgnores(Params);
		const FVector Start = Targets[FootIndex] + FVector::UpVector * Settings.MaxFootOffsetCm * Scale;
		const FVector End = Targets[FootIndex] - FVector::UpVector * (Settings.MaxFootOffsetCm + 12.0) * Scale;
		const bool bGround = bEnabled && Reach < 0.001 && Visual->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, SupportChannel, Params, SupportResponses)
			&& Hit.ImpactNormal.Z >= 0.65 && Hit.GetComponent();
		if (bGround)
		{
			Observation.GroundMask |= 1 << FootIndex;
			const double FloorZ = Animation.FloorZ[FootIndex];
			const double LiftCm = FMath::Max(0.0, Base.GetLocation().Z - FloorZ) * Scale;
			const FVector GroundTarget = Hit.ImpactPoint + FVector::UpVector * (FloorZ * Scale + LiftCm);
			const double GroundOffset = FMath::Clamp(GroundTarget.Z - Targets[FootIndex].Z,
				-Settings.MaxFootOffsetCm * Scale, Settings.MaxFootOffsetCm * Scale);
			Targets[FootIndex].Z += GroundOffset;
			PelvisSum += GroundOffset * Animation.Contact[FootIndex];
			PelvisWeight += Animation.Contact[FootIndex];
			const bool bSameSupport = Foot.Support.Get() == Hit.GetComponent() && Foot.SupportBone == Hit.BoneName;
			const bool bPlantPhase = Animation.Contact[FootIndex] > 0.7;
			if (Animation.Contact[FootIndex] < 0.3) { Foot.bPlanted = false; Foot.bReleasedUntilSwing = false; }
			if (!bSameSupport) { Foot.bPlanted = false; Foot.bReleasedUntilSwing = false; }
			Foot.Support = Hit.GetComponent();
			Foot.SupportBone = Hit.BoneName;
			const FTransform SupportWorld = CatQuadruped::SupportTransform(Hit.GetComponent(), Hit.BoneName);
			if (!Foot.bPlanted && bPlantPhase && !Foot.bReleasedUntilSwing)
			{
				// Keep any unfinished release correction when acquiring the next anchor.
				Foot.PlantLocal = SupportWorld.InverseTransformPosition(Targets[FootIndex] + Foot.PlantOffsetWorld);
				Foot.bPlanted = true;
			}
			if (Foot.bPlanted)
			{
				const FVector Plant = SupportWorld.TransformPosition(Foot.PlantLocal);
				const bool bDrift = FVector::DistSquared(Plant, Targets[FootIndex]) > FMath::Square(Settings.MaxPlantDriftCm * Scale);
				if (bDrift) { Foot.bPlanted = false; Foot.bReleasedUntilSwing = true; Observation.ReleasedPlantMask |= 1 << FootIndex; }
				else
				{
					Foot.PlantOffsetWorld = (Plant - Targets[FootIndex]) * Animation.Contact[FootIndex];
					Observation.PlantMask |= 1 << FootIndex;
				}
			}
			if (!Foot.bPlanted)
			{
				// The animation keeps advancing normally. Only the previous lock's correction
				// decays: clearing it in one frame popped the paw at drift limit and toe-off.
				Foot.PlantOffsetWorld *= FMath::Exp(-Delta / Settings.BlendSeconds);
				Observation.PlantReleaseOffsetCm[FootIndex] = Foot.PlantOffsetWorld.Size();
			}
			Targets[FootIndex] += Foot.PlantOffsetWorld;
			const FVector UpComponent = Base.GetRotation().RotateVector(Foot.SoleNormalLocal).GetSafeNormal();
			const FVector NormalComponent = MeshWorld.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
			const FQuat Tilt = FQuat::FindBetweenNormals(UpComponent, NormalComponent);
			Rotations[FootIndex] = (FQuat::Slerp(FQuat::Identity, Tilt, Weights[FootIndex] * Animation.Contact[FootIndex]) * Base.GetRotation()).GetNormalized();
		}
		else
		{
			Foot.bPlanted = false;
			Foot.bReleasedUntilSwing = false;
			Foot.Support.Reset();
			Foot.PlantOffsetWorld = FVector::ZeroVector;
			Targets[FootIndex] = BaseWorld[FootIndex];
		}
		// Never smooth the planted world point: doing so would reintroduce sliding as the body moves.
		const FVector DesiredOffset = (Targets[FootIndex] - BaseWorld[FootIndex]).GetClampedToMaxSize(Settings.MaxFootOffsetCm * Scale);
		if (bGround) Foot.OffsetWorld = DesiredOffset;
		else Foot.OffsetWorld *= FMath::Exp(-Delta / FMath::Max(0.01f, Settings.BlendSeconds));
		Targets[FootIndex] = BaseWorld[FootIndex] + Foot.OffsetWorld * Weights[FootIndex];
	}
	const double WantedPelvis = bEnabled && PelvisWeight > 0.1
		? FMath::Clamp(PelvisSum / PelvisWeight, -Settings.MaxPelvisOffsetCm * Scale, Settings.MaxPelvisOffsetCm * Scale) : 0.0;
	PelvisOffset = CatQuadruped::Smooth(PelvisOffset, WantedPelvis, Settings.BlendSeconds, Delta);
	Observation.PelvisOffsetCm = PelvisOffset * Alpha;
	const FVector OffsetComponent = MeshWorld.InverseTransformVector(FVector::UpVector * Observation.PelvisOffsetCm);
	const int32 PelvisParent = Mesh->GetRefSkeleton().GetParentIndex(PelvisIndex);
	Visual->BoneSpaceTransforms[PelvisIndex].AddToTranslation((PelvisParent == INDEX_NONE ? OffsetComponent : ComponentPose[PelvisParent].InverseTransformVector(OffsetComponent)));
	RebuildPose(Visual->BoneSpaceTransforms);
	for (int32 FootIndex = 0; FootIndex < FootCount; ++FootIndex)
	{
		Observation.TargetsWorld[FootIndex] = Targets[FootIndex];
		if (Weights[FootIndex] > 0.001)
			SolveFoot(Visual->BoneSpaceTransforms, FootIndex, MeshWorld.InverseTransformPosition(Targets[FootIndex]), Rotations[FootIndex]);
		Observation.FootErrorCm[FootIndex] = FVector::Distance(Targets[FootIndex], MeshWorld.TransformPosition(ComponentPose[Feet[FootIndex].Chain.Last()].GetLocation()));
	}
	LogObservation(Body, Now, PreviousMode != Observation.Mode);
}

void FCatQuadrupedLocomotion::LogObservation(const UCatPhysicalBodyComponent* Body, double Now, bool bForce)
{
	if (!bForce && (Now < NextLogSeconds || Observation.Alpha < 0.001)) return;
	NextLogSeconds = Now + 1.0;
	UE_LOG(LogCatLocomotion, Log,
		TEXT("Event=locomotion_pose_sample Actor=%s BodyId=%s World=%s NetMode=%d Authority=%d LocalRole=%d ResetEpoch=%u Mode=%s BodySpeedCmS=%.3f RelativeSpeedCmS=%.3f SupportSpeedCmS=%.3f AnimationSpeedCmS=%.3f StrideScale=%.3f Alpha=%.3f PelvisCm=%.3f GroundMask=%u PlantMask=%u ReleasedMask=%u ExcludedMask=%u ErrorCm=%.3f,%.3f,%.3f,%.3f ReleaseOffsetCm=%.3f,%.3f,%.3f,%.3f"),
		*GetNameSafe(Body->GetOwner()), *Body->GetBodyId().ToString(), *GetNameSafe(Body->GetWorld()),
		int32(Body->GetOwner()->GetNetMode()), Body->GetOwner()->HasAuthority(), int32(Body->GetOwner()->GetLocalRole()), Body->GetResetEpoch(),
		*Observation.Mode.ToString(), Observation.BodySpeedCmS, Observation.RelativeSpeedCmS, Observation.SupportSpeedCmS, Observation.AnimationSpeedCmS, Observation.StrideScale, Observation.Alpha,
		Observation.PelvisOffsetCm, Observation.GroundMask, Observation.PlantMask, Observation.ReleasedPlantMask, Observation.ExcludedFootMask,
		Observation.FootErrorCm[0], Observation.FootErrorCm[1], Observation.FootErrorCm[2], Observation.FootErrorCm[3],
		Observation.PlantReleaseOffsetCm[0], Observation.PlantReleaseOffsetCm[1], Observation.PlantReleaseOffsetCm[2], Observation.PlantReleaseOffsetCm[3]);
}
