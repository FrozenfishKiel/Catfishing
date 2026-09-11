#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/CatCharacterMovementComponent.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Interaction/Grab/CatLightPropSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "GameFramework/PlayerController.h"

void FCatPhysicalBodyPostPhysicsTick::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
{
	FActorComponentTickFunction::ExecuteTickHelper(Target, false, DeltaTime, TickType,
		[this](float Delta) { Target->PublishPostPhysicsSnapshot(Delta); });
}
FString FCatPhysicalBodyPostPhysicsTick::DiagnosticMessage()
{
	return GetNameSafe(Target) + FString(TEXT("[PhysicalBodyPostPhysics]"));
}
void UCatPhysicalBodyComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&PostPhysicsTick))
		{
			PostPhysicsTick.Target = this;
			PostPhysicsTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}
	else if (PostPhysicsTick.IsTickFunctionRegistered()) PostPhysicsTick.UnRegisterTickFunction();
}
void UCatPhysicalBodyComponent::PublishPostPhysicsSnapshot(float DeltaSeconds)
{
	if (!HasAuthority() || !Body || !Grab) return;
	if (CharacterMovement)
	{
		const auto* Settings = UPhysicsSettings::Get();
		const double Scale = GetWorld()->GetPhysicsScene() ? GetWorld()->GetPhysicsScene()->GetNetworkDeltaTimeScale() : 1.0;
		const double Limit = Settings->bSubstepping ? Settings->MaxSubsteps * double(Settings->MaxSubstepDeltaTime) : double(Settings->MaxPhysicsDeltaTime);
		CharacterMovement->AdvanceFromAuthority(Limit > 0 ? FMath::Min(DeltaSeconds * Scale, Limit) : DeltaSeconds * Scale);
		bGrounded = CharacterMovement->IsMovingOnGround();
		bSupportSampleReady = true;
		Body->ComponentVelocity = CharacterMovement->Velocity;
		Grab->RefreshKinematicHands();
	}
	// Model poses are finalized later this frame. That consumer publishes the same snapshot once.
	if (!CharacterMovement || !UCatModelContactComponent::UsesModelContacts(GetOwner())) PublishCompletedSnapshot();
}

void UCatPhysicalBodyComponent::FinalizeModelContactFromAuthority()
{
	if (!HasAuthority() || !CharacterMovement || !Body || !Grab) return;
	CharacterMovement->ResolveModelPeerPenetration();
	bGrounded = CharacterMovement->IsMovingOnGround();
	Grab->RefreshKinematicHands();
	PublishCompletedSnapshot();
}

void UCatPhysicalBodyComponent::PublishCompletedSnapshot()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (bPublishJumpAfterPhysics || bPublishMovementAfterPhysics || Now - LastSnapshotSeconds >= 1.0 / 30.0)
	{
		LastSnapshotSeconds = Now;
		CaptureSnapshot();
		if (bPublishMovementAfterPhysics)
		{
			bPublishMovementAfterPhysics = false;
			GetOwner()->ForceNetUpdate();
			LogState(TEXT("physics_body_movement_snapshot"), TEXT("PostPhysicsObserved"));
		}
		if (bPublishJumpAfterPhysics)
		{
			bPublishJumpAfterPhysics = false;
			GetOwner()->ForceNetUpdate();
			LogState(TEXT("physics_body_jump_snapshot"), TEXT("PostPhysicsObserved"));
		}
	}
}

UCatPhysicalBodyComponent::UCatPhysicalBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PostPhysicsTick.TickGroup = TG_PostPhysics;
	PostPhysicsTick.bCanEverTick = true;
	PostPhysicsTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}
bool UCatPhysicalBodyComponent::HasAuthority() const { return GetOwner() && GetOwner()->HasAuthority(); }
bool UCatPhysicalBodyComponent::IsLocallyControlled() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled();
}
void UCatPhysicalBodyComponent::ConfigureGeometry(UBoxComponent* ConfiguredBody, USphereComponent* ConfiguredLeft, USphereComponent* ConfiguredRight)
{
	ConfiguredBody->InitBoxExtent(FVector(13.0, 5.0, 7.0));
	ConfiguredBody->SetCollisionProfileName(TEXT("PhysicsActor"));
	ConfiguredBody->SetCollisionResponseToAllChannels(ECR_Block);
	ConfiguredBody->BodyInstance.SetMassOverride(4.0f, true);
	ConfiguredBody->SetLinearDamping(0.15f);
	ConfiguredBody->SetAngularDamping(0.8f);
	ConfiguredBody->BodyInstance.bUseCCD = true;
	for (USphereComponent* Hand : {ConfiguredLeft, ConfiguredRight})
	{
		Hand->InitSphereRadius(UCatPhysicsGrabComponent::HandRadiusCm);
		Hand->SetCollisionProfileName(TEXT("PhysicsActor"));
		Hand->SetCollisionResponseToAllChannels(ECR_Block);
		Hand->BodyInstance.SetMassOverride(0.12f, true);
		Hand->SetLinearDamping(0.3f);
		Hand->SetAngularDamping(1.0f);
		Hand->BodyInstance.bUseCCD = true;
	}
	ConfiguredLeft->SetRelativeLocation(UCatPhysicsGrabComponent::RestHandLocal(true));
	ConfiguredRight->SetRelativeLocation(UCatPhysicsGrabComponent::RestHandLocal(false));
}
void UCatPhysicalBodyComponent::Initialize(UBoxComponent* InBody, USphereComponent* InLeft, USphereComponent* InRight,
	UPhysicsConstraintComponent* InLeftArm,UPhysicsConstraintComponent* InRightArm,UCatPhysicsGrabComponent* InGrab, double InGeometryScale)
{
	if (Body || !InBody || !InLeft || !InRight || !InLeftArm || !InRightArm || !InGrab) return;
	Body=InBody; LeftHand=InLeft; RightHand=InRight; LeftArm=InLeftArm; RightArm=InRightArm; Grab=InGrab;
	GeometryScale = FMath::IsFinite(InGeometryScale) && InGeometryScale > UE_DOUBLE_SMALL_NUMBER ? InGeometryScale : 1.0;
	Body->SetBoxExtent(FVector(13.0, 5.0, 7.0) * GeometryScale);
	for (const bool bLeft : {true, false})
	{
		GetHand(bLeft)->SetSphereRadius(UCatPhysicsGrabComponent::HandRadiusCm * GeometryScale);
		GetHand(bLeft)->SetRelativeLocation(GetRestHandLocalPoint(bLeft));
	}

	ViewInput=FRotator(-15,GetOwner()->GetActorRotation().Yaw,0);
	FacingYawDegrees = ViewInput.Yaw;
	GetOwner()->SetReplicateMovement(false);
	if (HasAuthority())
	{
		if (!BodyId.IsValid()) BodyId=FGuid::NewGuid();
		if (!CharacterMovement)
		{
			Body->SetSimulatePhysics(true); LeftHand->SetSimulatePhysics(true); RightHand->SetSimulatePhysics(true);
			ConfigureArm(true); ConfigureArm(false);
		}
		else
		{
			Body->SetSimulatePhysics(false);
			Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Body->CanCharacterStepUpOn = ECB_No;
			for (auto* Hand : {LeftHand.Get(), RightHand.Get()})
			{
				Hand->SetSimulatePhysics(false);
				Hand->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Hand->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				Hand->CanCharacterStepUpOn = ECB_No;
			}
		}
	}
	else
	{
		LeftHand->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		RightHand->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		LeftHand->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RightHand->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	Grab->InitializeHands(Body,LeftHand,RightHand,LeftArm,RightArm,GeometryScale);
	if (auto* Policy = GetWorld()->GetSubsystem<UCatLightPropSubsystem>())
	{
		Policy->RegisterCatPart(Body);
		if (CharacterMovement) Policy->RegisterCatPart(CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent());
		Policy->RegisterCatPart(LeftHand);
		Policy->RegisterCatPart(RightHand);
	}
	Grab->PrimaryComponentTick.AddPrerequisite(this,PrimaryComponentTick);
	LastInputSeconds=GetWorld()->GetTimeSeconds();
	if (HasAuthority()) CaptureSnapshot();
	else if (bReceivedSnapshot) { bReceivedSnapshot=false; OnRep_PhysicsSnapshot(); }
	LogState(TEXT("physics_body_started"), CharacterMovement ? TEXT("UprightCMCServerSnapshots") : TEXT("PrototypeChaosServerSnapshots"));
	UE_LOG(LogCatPhysicsGrab, Log,
		TEXT("Event=physics_body_support_query World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s TraceChannel=%d Result=BodyCollisionResponses"),
		*GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), HasAuthority(), int32(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), *BodyId.ToString(), int32(Body->GetCollisionObjectType()));
	const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_body_geometry World=%s NetMode=%d Authority=%d Actor=%s BodyId=%s GeometryScale=%.3f BoxExtentCm=%s StandHeightCm=%.3f LeftShoulderLocal=%s LeftHandLocal=%s HandRadiusCm=%.3f MassKg=%.3f Substepping=%d MaxSubstepDeltaTimeSeconds=%.6f MaxSubsteps=%d"),
		*GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), HasAuthority(), *GetNameSafe(GetOwner()), *BodyId.ToString(), GeometryScale,
		*Body->GetScaledBoxExtent().ToCompactString(), GetStandRootHeightCm(), *GetShoulderLocalPoint(true).ToCompactString(),
		*GetRestHandLocalPoint(true).ToCompactString(), LeftHand->GetScaledSphereRadius(),
		HasAuthority() && !CharacterMovement ? Body->GetMass() : Body->BodyInstance.GetMassOverride(), PhysicsSettings->bSubstepping,
		PhysicsSettings->MaxSubstepDeltaTime, PhysicsSettings->MaxSubsteps);

}
void UCatPhysicalBodyComponent::ConfigureArm(const bool bLeft)
{
	UPhysicsConstraintComponent* Arm = bLeft ? LeftArm.Get() : RightArm.Get();
	USphereComponent* Hand = bLeft ? LeftHand.Get() : RightHand.Get();
	Arm->BreakConstraint();
	Arm->SetWorldLocation(Body->GetComponentTransform().TransformPosition(GetShoulderLocalPoint(bLeft)));
	Arm->SetWorldRotation(GetOwner()->GetActorRotation());
	Arm->SetLinearXLimit(LCM_Limited, 19.3f * GeometryScale);
	Arm->SetLinearYLimit(LCM_Limited, 19.3f * GeometryScale);
	Arm->SetLinearZLimit(LCM_Limited, 19.3f * GeometryScale);
	Arm->SetAngularSwing1Limit(ACM_Free, 0.0f);
	Arm->SetAngularSwing2Limit(ACM_Free, 0.0f);
	Arm->SetAngularTwistLimit(ACM_Free, 0.0f);
	Arm->SetDisableCollision(true);
	Arm->SetLinearPositionDrive(true, true, true);
	Arm->SetLinearDriveAccelerationMode(false);
	Arm->SetLinearVelocityDrive(true, true, true);
	Arm->SetLinearVelocityTarget(FVector::ZeroVector);
	Arm->SetLinearDriveParams(140.0f, 10.0f, 1200.0f);
	// Chaos treats the SECOND component as the drive parent. Targets are expressed in the body/shoulder frame,
	// not in the freely rotating hand frame (which would reverse and rotate the intended reach).
	Arm->SetConstrainedComponents(Hand, NAME_None, Body, NAME_None);
	Arm->SetConstraintReferenceFrame(EConstraintFrame::Frame1, FTransform::Identity);
	Arm->SetConstraintReferenceFrame(EConstraintFrame::Frame2,
		FTransform(FQuat::Identity, GetShoulderLocalPoint(bLeft)));
	Arm->SetLinearPositionTarget(GetRestHandLocalPoint(bLeft)
		- GetShoulderLocalPoint(bLeft));
}
void UCatPhysicalBodyComponent::CaptureSnapshot()
{
	Snapshot.BodyLocation = Body->GetComponentLocation();
	Snapshot.BodyRotation = Body->GetComponentRotation();
	Snapshot.Velocity = CharacterMovement ? CharacterMovement->Velocity : Body->GetPhysicsLinearVelocity();
	Snapshot.MoveIntent = MoveInput;
	Snapshot.LeftHandLocation = LeftHand->GetComponentLocation();
	Snapshot.RightHandLocation = RightHand->GetComponentLocation();
	Snapshot.bGrounded = bGrounded;
	Snapshot.bSupportSampleReady = bSupportSampleReady;
	++Snapshot.Revision;
}
void UCatPhysicalBodyComponent::UpdatePhysicalMovement(const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f) return;
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	const double Mass = Body->GetMass();
	const double EffectiveGravityScale = FMath::IsFinite(GravityScale) ? FMath::Max(0.0, GravityScale) : 1.0;
	const double Gravity = FMath::Abs(GetWorld()->GetGravityZ()) * EffectiveGravityScale;
	// Force APIs integrate dt in Chaos. Correct built-in gravity uniformly for all three bodies.
	for (UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(Body.Get()), static_cast<UPrimitiveComponent*>(LeftHand.Get()), static_cast<UPrimitiveComponent*>(RightHand.Get())})
		Part->AddForce(FVector(0,0,GetWorld()->GetGravityZ()*(EffectiveGravityScale-1.0))*Part->GetMass());
	for (auto It=ExternalForces.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
		Body->AddForce(It.Value().Force);
	}
	if (bJumpSeparating && !bPublishJumpAfterPhysics && GetWorld()->GetTimeSeconds() >= SupportDisabledUntilSeconds && Velocity.Z <= 0) bJumpSeparating=false;
	const FVector BodyUp = Body->GetUpVector();
	const bool bSupportEnabled = bLocomotionEnabled && !bJumpSeparating && GetWorld()->GetTimeSeconds() >= SupportDisabledUntilSeconds;
	const bool bCanSupport = bSupportEnabled && BodyUp.Z > 0.35;
	// Ground support follows the body's collision contract. Visibility also hits query-only
	// interaction volumes (shops, containers and pickups), which must never lift the cat.
	const ECollisionChannel SupportChannel = Body->GetCollisionObjectType();
	const FCollisionResponseParams SupportResponses(Body->GetCollisionResponseToChannels());
	// A side/back contact is not a foot plant. Observe only an upward-facing surface within the
	// actual rotated box's vertical extent; a nearby wall or a hand holding a wall is insufficient.
	bool bNewGroundContactRecovery = false;
	double RecoveryContactDistanceCm = -1.0;
	if (bSupportEnabled && BodyUp.Z <= 0.35)
	{
		const FVector Extent = Body->GetScaledBoxExtent();
		const double VerticalExtentCm = FMath::Abs(Body->GetForwardVector().Z) * Extent.X
			+ FMath::Abs(Body->GetRightVector().Z) * Extent.Y + FMath::Abs(BodyUp.Z) * Extent.Z;
		const FVector Origin = Body->GetComponentLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsGroundRecovery), false, GetOwner());
		AppendSupportQueryIgnores(Params);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Origin,
			Origin - FVector(0.0, 0.0, VerticalExtentCm + 1.0), SupportChannel, Params, SupportResponses)
			&& Hit.ImpactNormal.Z >= 0.55)
		{
			bNewGroundContactRecovery = true;
			RecoveryContactDistanceCm = Hit.Distance;
		}
	}
	if (bNewGroundContactRecovery != bGroundContactRecoveryActive)
	{
		bGroundContactRecoveryActive = bNewGroundContactRecovery;
		UE_LOG(LogCatPhysicsGrab, Log,
			TEXT("Event=physics_body_ground_recovery_changed World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Active=%d UpZ=%.4f ContactDistanceCm=%.3f LeftGrip=%s RightGrip=%s Result=PhysicalTorqueOnly"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetOwner()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()),
			*BodyId.ToString(), bGroundContactRecoveryActive, BodyUp.Z, RecoveryContactDistanceCm,
			*Grab->GetGripState(true).GripId.ToString(), *Grab->GetGripState(false).GripId.ToString());
	}
	bool bNewGrounded = false;
	if (bCanSupport)
	{
		for (const FVector FootLocal : {FVector(8.0, -3.0, 0.0), FVector(8.0, 3.0, 0.0),
			FVector(-8.0, -3.0, 0.0), FVector(-8.0, 3.0, 0.0)})
		{
			const FVector Origin = Body->GetComponentTransform().TransformPosition(FootLocal * GeometryScale);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsFootSupport), false, GetOwner());
			AppendSupportQueryIgnores(Params);
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin - FVector(0.0, 0.0, GetStandRootHeightCm() * 1.3), SupportChannel, Params, SupportResponses)
				|| Hit.ImpactNormal.Z < 0.55) continue;
			bNewGrounded = true;
			UPrimitiveComponent* Support = Hit.GetComponent();
			const double BaseVelocity = Support && Support->IsSimulatingPhysics(Hit.BoneName)
				? Support->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint, Hit.BoneName).Z : 0.0;
			const double VerticalVelocity = Body->GetPhysicsLinearVelocityAtPoint(Origin).Z - BaseVelocity;
			const double ForceZ = Mass * FMath::Clamp(Gravity + (GetStandRootHeightCm() - Hit.Distance) * 140.0 - VerticalVelocity * 18.0,
				0.0, Gravity * 2.5) * 0.25;
			Body->AddForceAtLocation(FVector(0.0, 0.0, ForceZ), Origin);
			if (Support && Support->IsSimulatingPhysics(Hit.BoneName))
				Support->AddForceAtLocation(FVector(0.0, 0.0, -ForceZ), Hit.ImpactPoint, Hit.BoneName);
		}
	}
	if (bNewGrounded != bGrounded)
	{
		bGrounded = bNewGrounded;
		UE_LOG(LogCatPhysicsGrab, Log,
			TEXT("Event=physics_body_support_changed World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Grounded=%d"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetOwner()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()), *BodyId.ToString(), bGrounded);
	}
	const bool bFishingMotor = FishingMotorSource.IsValid();
	const bool bFacingAim = bFishingMotor || Grab->IsReaching(true) || Grab->IsReaching(false);
	if (bFacingAim) FacingYawDegrees = ViewInput.Yaw;
	else if (!MoveInput.IsNearlyZero()) FacingYawDegrees = MoveInput.Rotation().Yaw;
	const auto* Settings = UPhysicsSettings::Get();
	const double Scale = GetWorld()->GetPhysicsScene() ? GetWorld()->GetPhysicsScene()->GetNetworkDeltaTimeScale() : 1.0;
	const double Limit = Settings->bSubstepping ? Settings->MaxSubsteps * double(Settings->MaxSubstepDeltaTime) : double(Settings->MaxPhysicsDeltaTime);
	if (bGrounded) Body->AddForce(ComputeHorizontalDriveForce(Velocity, Mass, Limit > 0 ? FMath::Min(DeltaSeconds * Scale, Limit) : DeltaSeconds * Scale));
	else bFishingHoldActive = false;
	// Bounded upright motor, deliberately weaker in the air so a held body still swings under gravity.
	const FVector UpError = FVector::CrossProduct(BodyUp, FVector::UpVector);
	// A signed angle also turns an exactly backward input; cross(forward, target) is zero at 180 degrees.
	const double YawError = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Body->GetComponentRotation().Yaw, FacingYawDegrees));
	const FVector AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
	FVector AngularAcceleration = UpError * (bGrounded ? 55.0 : 6.0) - AngularVelocity * (bGrounded ? 9.0 : 0.7);
	const double YawGain = bGrounded ? (bFacingAim ? 24.0 : 80.0) : 2.0;
	AngularAcceleration.Z = YawError * YawGain - AngularVelocity.Z * (bGrounded ? (bFacingAim ? 9.0 : 18.0) : 0.7);
	AngularAcceleration = AngularAcceleration.GetClampedToMaxSize(100.0);
	if (bGroundContactRecoveryActive)
	{
		// cross(up, world-up) vanishes at exactly 180 degrees. Select a deterministic roll axis
		// at that singularity, then drive the real body through contact instead of assigning a pose.
		const FVector RecoveryAxis = UpError.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER,
			Body->GetForwardVector().GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector));
		const double TiltAngleRadians = FMath::Acos(FMath::Clamp(BodyUp.Z, -1.0, 1.0));
		// A side-lying 4 kg box must roll over its lower edge against gravity. The ordinary
		// 100 rad/s^2 walking/air bound cannot supply that torque; this bound applies only at ground contact.
		AngularAcceleration = (RecoveryAxis * TiltAngleRadians * 300.0 - AngularVelocity * 50.0)
			.GetClampedToMaxSize(650.0);
	}
	if (bLocomotionEnabled) Body->AddTorqueInRadians(AngularAcceleration, NAME_None, true);
	bSupportSampleReady = true;
}

FCatBodyDriveSample UCatPhysicalBodyComponent::CaptureDriveSample()
{
    FCatBodyDriveSample Sample;
    Sample.MoveIntent = MoveInput;
    Sample.HoldLocation = FishingHoldLocation;
    Sample.bHoldActive = bFishingHoldActive;
    Sample.bFishing = FishingMotorSource.IsValid();
    Sample.bLocomotion = bLocomotionEnabled;
    Sample.bUnderLoad = HasExternalLoadFromAuthority();
    const bool bGripped = HasPhysicalGrabConnection();
    Sample.bConnected = bGripped || Sample.bUnderLoad;
    bool bHasBodyContact = false, bHasOtherLoad = false;
    for (const auto& Entry : ExternalForces)
        if (Entry.Key.IsValid())
        {
            bHasBodyContact |= Entry.Value.bBodyContact;
            bHasOtherLoad |= !Entry.Value.bBodyContact;
            if (Entry.Value.bBodyContact)
                if (const auto* SourceMovement = Cast<UCatCharacterMovementComponent>(Entry.Key.Get()))
                    if (const auto* PeerBody = SourceMovement->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>())
                    {
                        Sample.bBodyContactDriven |= !PeerBody->GetMoveIntent().IsNearlyZero()
                            || PeerBody->HasFishingMotor() || PeerBody->HasPhysicalGrabConnection();
                    }
        }
    Sample.bPassiveBodyContact = bHasBodyContact && !bHasOtherLoad && !bGripped && !Sample.bFishing
        && (!CharacterMovement || !CharacterMovement->HasExternalLoad());
    // Contact remains real at zero load and when opposite forces cancel. Neither condition
    // may restore the unlimited free-walking motor between two contact frames.
    if (!Sample.bConnected)
        for (const auto& Entry : ExternalForces)
            if (Entry.Key.IsValid()) { Sample.bConnected = true; break; }
    Sample.MaxSpeed = Sample.bFishing ? FishingMotorMaxSpeed : MaxMovementSpeedCmS;
    Sample.MaxForce = FishingMotorMaxForce;
    if (!Sample.bFishing && Sample.bConnected && CharacterMovement)
        if (const auto* Effort = GetOwner()->FindComponentByClass<UCatPhysicalEffortComponent>())
        {
            Sample.bCooperative = true;
            Sample.MaxForce = Effort->GetMaximumForceKgCmS2();
        }
    return Sample;
}

FVector UCatPhysicalBodyComponent::ComputeDriveForce(FCatBodyDriveSample& Sample, const FVector& Position,
    const FVector& Velocity, double Mass, double StepSeconds)
{
    if (!Sample.bLocomotion) { Sample.bHoldActive = false; return FVector::ZeroVector; }
    const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0);
    if (Sample.bPassiveBodyContact && Sample.MoveIntent.IsNearlyZero())
    {
        Sample.bHoldActive = false;
        return FVector::ZeroVector;
    }
    if ((Sample.bFishing || Sample.bCooperative) && Sample.MoveIntent.IsNearlyZero())
    {
        if (!Sample.bHoldActive) { Sample.HoldLocation = Position; Sample.bHoldActive = true; }
        FVector Error = Sample.HoldLocation - Position; Error.Z = 0;
        // A finite local stance yields once overpowered; it never springs toward a remote old position.
        constexpr double ElasticRangeCm = 10.0;
        Error = Error.GetClampedToMaxSize(ElasticRangeCm);
        Sample.HoldLocation = Position + Error;
        const double Stiffness = Sample.MaxForce / ElasticRangeCm;
        const double Damping = 2.0 * FMath::Sqrt(Stiffness * Mass);
        return (Error * Stiffness - HorizontalVelocity * Damping).GetClampedToMaxSize(Sample.MaxForce);
    }
    Sample.bHoldActive = false;
    const FVector Error = Sample.MoveIntent * Sample.MaxSpeed - HorizontalVelocity;
    if (!Sample.bFishing && !Sample.bCooperative && Sample.bConnected) return (Error / .22).GetClampedToMaxSize(450.0) * Mass;
    const double Limit = Sample.bFishing || Sample.bCooperative ? Sample.MaxForce : 6000.0 * Mass;
    return (Error * (Mass / FMath::Max(UE_DOUBLE_SMALL_NUMBER, StepSeconds))).GetClampedToMaxSize(Limit);
}

FVector UCatPhysicalBodyComponent::ComputeHorizontalDriveForce(const FVector& Velocity, double Mass, double StepSeconds)
{
    FCatBodyDriveSample Sample = CaptureDriveSample();
    const FVector Force = ComputeDriveForce(Sample, Body->GetComponentLocation(), Velocity, Mass, StepSeconds);
    const double YieldDistance = Sample.bHoldActive && bFishingHoldActive ? FVector::Dist2D(Sample.HoldLocation, FishingHoldLocation) : 0;
    FishingHoldLocation = Sample.HoldLocation; bFishingHoldActive = Sample.bHoldActive;
    if (YieldDistance > UE_DOUBLE_SMALL_NUMBER && GetWorld()->GetTimeSeconds() >= NextHoldYieldLogSeconds)
    {
        NextHoldYieldLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
        UE_LOG(LogCatPhysicsGrab, Log,
            TEXT("Event=physics_body_hold_anchor_yielded World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s MotorSource=%s YieldDistanceCm=%.3f Anchor=%s ForceBudgetUE=%.3f Result=SupportPointYielded"),
            *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()),
            *GetNameSafe(GetOwner()), *BodyId.ToString(), *GetNameSafe(FishingMotorSource.Get()), YieldDistance,
            *FishingHoldLocation.ToCompactString(), FishingMotorMaxForce);
    }
    return Force;
}

FVector UCatPhysicalBodyComponent::GetExternalForceFromAuthority()
{
	FVector Sum = FVector::ZeroVector;
	for (auto It = ExternalForces.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
		Sum += It.Value().Force;
	}
	return Sum;
}

bool UCatPhysicalBodyComponent::HasExternalLoadFromAuthority() const
{
    for (const auto& Entry : ExternalForces)
        if (Entry.Key.IsValid() && !Entry.Value.Force.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER)) return true;
    return CharacterMovement && CharacterMovement->HasExternalLoad();
}

double UCatPhysicalBodyComponent::GetVerticalGripForceFromAuthority() const
{
    double Sum = 0;
    for (const auto& Entry : ExternalForces)
        if (Entry.Key.IsValid() && Entry.Value.bVerticalGripTraction) Sum += Entry.Value.Force.Z;
    return Sum;
}

double UCatPhysicalBodyComponent::GetJumpTractionWeight() const
{
    return HasAuthority() && bLocomotionEnabled && GetWorld()
        ? FMath::Clamp((JumpTractionUntilSeconds-GetWorld()->GetTimeSeconds())/.08,0.0,1.0) : 0.0;
}

void UCatPhysicalBodyComponent::NotifyGripLiftFromAuthority()
{
    if (!HasAuthority()) return;
    bGrounded = false;
    bPublishJumpAfterPhysics = true;
    // External lift never opens another jump window or propagates a hanging chain.
    LogState(TEXT("physics_body_grip_lift"),TEXT("GripForceExceedsWeight"));
}

void UCatPhysicalBodyComponent::AddExternalImpulseFromAuthority(FVector ImpulseKgCmS)
{
	if (!HasAuthority() || ImpulseKgCmS.ContainsNaN()) return;
	if (CharacterMovement)
	{
		if (CharacterMovement->IsMovingOnGround()) ImpulseKgCmS.Z = 0;
		CharacterMovement->QueueExternalImpulse(ImpulseKgCmS);
	}
	else if (Body) Body->AddImpulse(ImpulseKgCmS);
}

bool UCatPhysicalBodyComponent::HasPhysicalGrabConnection() const
{
	for (const bool bLeft : {true, false})
		if (Grab->IsGripping(bLeft) && !Grab->GetGripState(bLeft).bControlledHold) return true;
	// Being grabbed constrains this body just as much as its own outgoing grip.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (*It != GetOwner())
			if (const auto* OtherGrab = It->FindComponentByClass<UCatPhysicsGrabComponent>())
				for (const bool bLeft : {true, false})
					if (OtherGrab->GetGripTarget(bLeft) == GetOwner()) return true;
	return false;
}

void UCatPhysicalBodyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCatPhysicalBodyComponent, Snapshot);
	DOREPLIFETIME(UCatPhysicalBodyComponent, BodyId);
	DOREPLIFETIME(UCatPhysicalBodyComponent, ControlEpoch);
	DOREPLIFETIME(UCatPhysicalBodyComponent, bLocomotionEnabled);
}
FVector UCatPhysicalBodyComponent::GetVelocity() const { return HasAuthority() && Body ? (CharacterMovement ? CharacterMovement->Velocity : Body->GetPhysicsLinearVelocity()) : Snapshot.Velocity; }
FVector UCatPhysicalBodyComponent::GetMoveIntent() const { return HasAuthority() ? MoveInput : Snapshot.MoveIntent; }
bool UCatPhysicalBodyComponent::IsGrounded() const { return HasAuthority() ? bGrounded : Snapshot.bGrounded; }
bool UCatPhysicalBodyComponent::HasMovementSample() const { return HasAuthority() ? bSupportSampleReady : bReceivedSnapshot && Snapshot.bSupportSampleReady; }
double UCatPhysicalBodyComponent::GetStandRootHeightCm() const
{
	return 20.0 * GeometryScale * (Body ? FMath::Abs(Body->GetComponentScale().Z) : 1.0);
}
FVector UCatPhysicalBodyComponent::GetShoulderLocalPoint(bool bLeft) const
{
	return UCatPhysicsGrabComponent::ShoulderLocal(bLeft) * GeometryScale;
}
FVector UCatPhysicalBodyComponent::GetRestHandLocalPoint(bool bLeft) const
{
	return UCatPhysicsGrabComponent::RestHandLocal(bLeft) * GeometryScale;
}
FVector UCatPhysicalBodyComponent::GetSupportFootPointWorld() const
{
	if (CharacterMovement) return GetOwner()->GetActorLocation() - FVector(0,0,CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (!Body) return GetOwner()->GetActorLocation() - FVector(0,0,GetStandRootHeightCm());
	const FVector Extent=Body->GetScaledBoxExtent();
	const double BoxDepth=FMath::Abs(Body->GetForwardVector().Z)*Extent.X+FMath::Abs(Body->GetRightVector().Z)*Extent.Y+FMath::Abs(Body->GetUpVector().Z)*Extent.Z;
	const double StandingFootDepth = bLocomotionEnabled ? GetStandRootHeightCm() * FMath::Max(0.0, Body->GetUpVector().Z) : 0.0;
	const double FootDepth = FMath::Max(BoxDepth, StandingFootDepth);
	return Body->GetComponentLocation()-FVector(0,0,FootDepth);
}
void UCatPhysicalBodyComponent::SetMoveIntent(FVector WorldDirection)
{
	if (WorldDirection.ContainsNaN()) return;
	WorldDirection.Z = 0;
	const FVector NextInput = bLocomotionEnabled ? WorldDirection.GetClampedToMaxSize(1) : FVector::ZeroVector;
	const bool bStartOrStop = MoveInput.IsNearlyZero() != NextInput.IsNearlyZero();
	const bool bChanged = !MoveInput.Equals(NextInput, .05);
	MoveInput = NextInput;
	LastInputSeconds = GetWorld()->GetTimeSeconds();
	if ((bStartOrStop || bChanged) && Body && IsLocallyControlled() && !HasAuthority()) SendLocalInput();
	if (bStartOrStop)
	{
		if (HasAuthority()) bPublishMovementAfterPhysics = true;
		LogState(HasAuthority() ? TEXT("physics_body_movement_accepted") : TEXT("physics_body_movement_requested"),
			MoveInput.IsNearlyZero() ? TEXT("Stop") : TEXT("Start"));
	}
}

void UCatPhysicalBodyComponent::SendLocalInput()
{
	LastSendSeconds = GetWorld()->GetTimeSeconds();
	ServerSetInput(MoveInput, ViewInput, ControlEpoch, ++LocalInputSequence);
}
void UCatPhysicalBodyComponent::SetViewIntent(FRotator View)
{
	if (View.ContainsNaN()) return;
	View.Pitch = FMath::ClampAngle(View.Pitch, -85, 75);
	View.Yaw = FRotator::NormalizeAxis(View.Yaw);
	View.Roll = 0;
	ViewInput = View;
}
void UCatPhysicalBodyComponent::ConfigureMovementDefaults(double JumpSpeed, double InGravityScale, double WalkSpeed)
{
	if (FMath::IsFinite(JumpSpeed) && JumpSpeed >= 0.0) JumpSpeedCmS = JumpSpeed;
	if (FMath::IsFinite(InGravityScale) && InGravityScale >= 0.0) GravityScale = InGravityScale;
	SetMovementSpeed(WalkSpeed);
}
void UCatPhysicalBodyComponent::SetMovementSpeed(double SpeedCmS)
{
	if (FMath::IsFinite(SpeedCmS) && SpeedCmS >= 0) MaxMovementSpeedCmS = SpeedCmS;
}
void UCatPhysicalBodyComponent::ServerSetInput_Implementation(FVector Move, FRotator View, uint32 Epoch, uint32 Sequence)
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController() || Epoch != ControlEpoch || Sequence == 0 || Move.ContainsNaN() || View.ContainsNaN()
		|| static_cast<int32>(Sequence - AcceptedInputSequence) <= 0)
	{
		if (GetWorld()->GetTimeSeconds() >= NextInputRejectLogSeconds)
		{
			NextInputRejectLogSeconds = GetWorld()->GetTimeSeconds() + 1;
			LogState(TEXT("physics_body_input_rejected"), TEXT("InvalidOrStale"));
		}
		return;
	}
	AcceptedInputSequence = Sequence;
	SetMoveIntent(Move);
	SetViewIntent(View);
	// CMC ServerMove used to deliver this view. Keep server traces/casts and pawn aim on the
	// same validated view now that only the physical input channel advances movement.
	OwnerPawn->GetController()->SetControlRotation(ViewInput);
}
void UCatPhysicalBodyComponent::RequestJump()
{
	if (!HasAuthority()) { if (IsLocallyControlled()) ServerRequestJump(ControlEpoch); return; }
	if (!Body || !bLocomotionEnabled || !bGrounded || bJumpSeparating || GetWorld()->GetTimeSeconds() < SupportDisabledUntilSeconds)
	{
		LogState(TEXT("physics_body_jump_rejected"), TEXT("NoGroundSupport"));
		return;
	}
	if (CharacterMovement)
	{
		CharacterMovement->JumpZVelocity = JumpSpeedCmS;
		if (CharacterMovement->DoJump(false, 0.0f))
		{
			bGrounded = false;
			bPublishJumpAfterPhysics = true;
			JumpTractionUntilSeconds = GetWorld()->GetTimeSeconds() + .35;
			LogState(TEXT("physics_body_jump"), TEXT("CMCJumpWithGripTraction"));
		}
		return;
	}
	const double DeltaSpeed = FMath::Max(0.0, JumpSpeedCmS - Body->GetPhysicsLinearVelocity().Z);
	if (!FMath::IsFinite(DeltaSpeed)) return;
	for (UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(Body.Get()), static_cast<UPrimitiveComponent*>(LeftHand.Get()), static_cast<UPrimitiveComponent*>(RightHand.Get())})
		Part->AddImpulse(FVector(0, 0, DeltaSpeed * Part->GetMass()));
	bGrounded = false;
	bJumpSeparating = true;
	SupportDisabledUntilSeconds = GetWorld()->GetTimeSeconds() + 0.025;
	// AddImpulse is consumed by Chaos later this frame. Publish grounded/velocity together after physics.
	bPublishJumpAfterPhysics = true;
	LogState(TEXT("physics_body_jump"), TEXT("AppliedToAllBodies"));
}
void UCatPhysicalBodyComponent::ServerRequestJump_Implementation(uint32 Epoch)
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->GetController() && Epoch == ControlEpoch) RequestJump();
	else LogState(TEXT("physics_body_jump_rejected"), TEXT("StaleOrUnpossessed"));
}
void UCatPhysicalBodyComponent::ClearControlIntent(FName Reason)
{
	JumpTractionUntilSeconds = 0;
	if (HasAuthority() && !MoveInput.IsNearlyZero()) bPublishMovementAfterPhysics = true;
	MoveInput = FVector::ZeroVector;
	if (Grab)
	{
		if (HasAuthority()) Grab->ReleaseAllFromAuthority(Reason);
		else { Grab->SetGrabInput(true, false); Grab->SetGrabInput(false, false); }
	}
	if (!HasAuthority() && IsLocallyControlled()) ServerClearControlIntent(ControlEpoch, ++LocalInputSequence);
}
void UCatPhysicalBodyComponent::ServerClearControlIntent_Implementation(uint32 Epoch, uint32 Sequence)
{
	if (Epoch == ControlEpoch && Sequence != 0 && static_cast<int32>(Sequence-AcceptedInputSequence)>0)
	{
		AcceptedInputSequence=Sequence;
		ClearControlIntent(TEXT("InputCleared"));
	}
}
void UCatPhysicalBodyComponent::BeginControlEpochFromAuthority()
{
	if (!HasAuthority()) return;
	ClearControlIntent(TEXT("ControlChanged"));
	++ControlEpoch;
	if (!ControlEpoch) ControlEpoch = 1;
	AcceptedInputSequence = 0;
	LastInputSeconds = GetWorld()->GetTimeSeconds();
	if (Grab) Grab->BeginInputEpochFromAuthority();
	GetOwner()->ForceNetUpdate();
	LogState(TEXT("physics_body_control_changed"), TEXT("NewEpoch"));
}
void UCatPhysicalBodyComponent::ReleaseConnectionsFromAuthority(FName Reason)
{
	if (!HasAuthority()) return;
	JumpTractionUntilSeconds = 0;
	if (Grab) Grab->ReleaseAllFromAuthority(Reason);
	if (!GetWorld()) return;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (*It != GetOwner())
			if (UCatPhysicsGrabComponent* Other = It->FindComponentByClass<UCatPhysicsGrabComponent>())
				Other->ReleaseTargetFromAuthority(GetOwner(), Reason);
}
void UCatPhysicalBodyComponent::SetLocomotionEnabledFromAuthority(bool bEnabled, FName Reason)
{
	if (!HasAuthority() || bLocomotionEnabled == bEnabled) return;
	bLocomotionEnabled = bEnabled;
	if (!bEnabled) { ClearControlIntent(Reason); FishingMotorSource.Reset(); bFishingHoldActive=false; }
	GetOwner()->ForceNetUpdate();
	LogState(TEXT("physics_body_locomotion_changed"), Reason);
}
void UCatPhysicalBodyComponent::SetExternalForceFromAuthority(const UObject* Source, FVector ForceKgCmS2, bool bVerticalGripTraction, bool bBodyContact)
{
	if (!HasAuthority() || !IsValid(Source) || ForceKgCmS2.ContainsNaN()) return;
	auto& Entry = ExternalForces.FindOrAdd(TWeakObjectPtr<const UObject>(Source));
	Entry.Force = ForceKgCmS2;
	Entry.bVerticalGripTraction = bVerticalGripTraction;
	Entry.bBodyContact = bBodyContact;
}
void UCatPhysicalBodyComponent::ClearExternalForce(const UObject* Source)
{
	if (HasAuthority() && Source) ExternalForces.Remove(TWeakObjectPtr<const UObject>(Source));
}
void UCatPhysicalBodyComponent::SetFishingMotorBudget(const UObject* Source, double MaxForceKgCmS2, double MaxSpeedCmS)
{
	if (!HasAuthority() || !IsValid(Source) || !FMath::IsFinite(MaxForceKgCmS2) || MaxForceKgCmS2 < 0
		|| !FMath::IsFinite(MaxSpeedCmS) || MaxSpeedCmS < 0) return;
	if (FishingMotorSource.IsValid() && FishingMotorSource.Get() != Source)
	{
		if (GetWorld()->GetTimeSeconds() >= NextBudgetRejectLogSeconds)
		{
			NextBudgetRejectLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
			LogState(TEXT("physics_motor_budget_rejected"), TEXT("DifferentSource"));
		}
		return;
	}
	const bool bNewSource = !FishingMotorSource.IsValid();
	if (bNewSource) bFishingHoldActive=false;
	FishingMotorSource = Source;
	FishingMotorMaxForce = MaxForceKgCmS2;
	FishingMotorMaxSpeed = MaxSpeedCmS;
	if (bNewSource) LogState(TEXT("physics_motor_budget_changed"), TEXT("Fishing"));
}
void UCatPhysicalBodyComponent::ClearFishingMotorBudget(const UObject* Source)
{
	if (HasAuthority() && Source && FishingMotorSource.HasSameIndexAndSerialNumber(TWeakObjectPtr<const UObject>(Source)))
	{
		FishingMotorSource.Reset(); bFishingHoldActive=false;
		LogState(TEXT("physics_motor_budget_changed"), TEXT("Walking"));
	}
}
bool UCatPhysicalBodyComponent::TeleportBodyFromAuthority(const FTransform& Transform, FName Reason)
{
	if (!HasAuthority() || !Body || Transform.ContainsNaN()) return false;
	BeginControlEpochFromAuthority();
	ReleaseConnectionsFromAuthority(Reason);
	ExternalForces.Reset();
	FishingMotorSource.Reset(); bFishingHoldActive=false;
	bSupportSampleReady = false;
	bGroundContactRecoveryActive = false;
	bGrounded = false;
	bJumpSeparating = false;
	bPublishJumpAfterPhysics = false;
	bPublishMovementAfterPhysics = false;
	FacingYawDegrees = Transform.Rotator().Yaw;
	LeftArm->BreakConstraint();
	RightArm->BreakConstraint();
	GetOwner()->SetActorTransform(Transform, false, nullptr, ETeleportType::ResetPhysics);
	if (CharacterMovement)
	{
		GetOwner()->SetActorRotation(FRotator(0,Transform.Rotator().Yaw,0));
		CharacterMovement->Velocity = FVector::ZeroVector;
		CharacterMovement->ClearAccumulatedForces();
		CharacterMovement->ClearQueuedExternalImpulse();
		CharacterMovement->SetMovementMode(MOVE_Falling);
		CharacterMovement->bForceNextFloorCheck = true;
	}
	else
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		USphereComponent* Hand = GetHand(Index == 0);
		Hand->SetWorldLocationAndRotation(Transform.TransformPosition(GetRestHandLocalPoint(Index == 0)),
			Transform.GetRotation(), false, nullptr, ETeleportType::ResetPhysics);
		if (!CharacterMovement)
		{
			Hand->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Hand->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			ConfigureArm(Index == 0);
		}
	}
	MoveInput = FVector::ZeroVector;
	SupportDisabledUntilSeconds = 0;
	++Snapshot.ResetEpoch;
	CaptureSnapshot();
	GetOwner()->ForceNetUpdate();
	LogState(TEXT("physics_body_reset"), Reason);
	return true;
}
void UCatPhysicalBodyComponent::OnRep_PhysicsSnapshot()
{
	if (!Body) { bReceivedSnapshot = true; return; }
	if (!bReceivedSnapshot || ClientResetEpoch != Snapshot.ResetEpoch)
	{
		GetOwner()->SetActorLocationAndRotation(Snapshot.BodyLocation, Snapshot.BodyRotation, false, nullptr, ETeleportType::TeleportPhysics);
		LeftHand->SetWorldLocation(Snapshot.LeftHandLocation);
		RightHand->SetWorldLocation(Snapshot.RightHandLocation);
		ClientResetEpoch = Snapshot.ResetEpoch;
	}
	bReceivedSnapshot = true;
}
void UCatPhysicalBodyComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	if (!Body || !Grab) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (IsLocallyControlled() && !HasAuthority() && Now - LastSendSeconds >= 1.0 / 30.0)
	{
		SendLocalInput();
	}
	if (HasAuthority())
	{
		if (OwnerPawn && OwnerPawn->GetController() && !IsLocallyControlled() && Now - LastInputSeconds > 0.5)
		{
			if (!MoveInput.IsNearlyZero() || Grab->IsReaching(true) || Grab->IsReaching(false))
			{
				ClearControlIntent(TEXT("InputTimeout"));
				LogState(TEXT("physics_body_input_timeout"), TEXT("Released"));
			}
		}
		if (CharacterMovement)
		{
			const bool bFacingAim = FishingMotorSource.IsValid() || Grab->IsReaching(true) || Grab->IsReaching(false);
			if (bFacingAim) FacingYawDegrees = ViewInput.Yaw;
			else if (!MoveInput.IsNearlyZero()) FacingYawDegrees = MoveInput.Rotation().Yaw;
			CharacterMovement->UpdatePeerPushContacts();
		}
		else UpdatePhysicalMovement(DeltaSeconds);
	}
	else if (bReceivedSnapshot)
	{
		if (CharacterMovement) CharacterMovement->ObserveSnapshot(Snapshot.Velocity, Snapshot.MoveIntent);
		const double Alpha = 1.0 - FMath::Exp(-60.0 * FMath::Max(0.0f, DeltaSeconds));
		GetOwner()->SetActorLocationAndRotation(FMath::Lerp(GetOwner()->GetActorLocation(), Snapshot.BodyLocation, Alpha),
			FQuat::Slerp(GetOwner()->GetActorQuat(), Snapshot.BodyRotation.Quaternion(), Alpha), false, nullptr, ETeleportType::TeleportPhysics);
		LeftHand->SetWorldLocation(FMath::Lerp(LeftHand->GetComponentLocation(), Snapshot.LeftHandLocation, Alpha));
		RightHand->SetWorldLocation(FMath::Lerp(RightHand->GetComponentLocation(), Snapshot.RightHandLocation, Alpha));
	}
	if (Now >= NextMotionLogSeconds && (!GetVelocity().IsNearlyZero(3) || Grab->IsGripping(true) || Grab->IsGripping(false)))
	{
		NextMotionLogSeconds = Now + 1;
		LogState(HasAuthority() ? TEXT("physics_body_motion") : TEXT("physics_body_snapshot_observed"), TEXT("Observed"));
	}
}
void UCatPhysicalBodyComponent::LogState(FName Event, FName Reason) const
{
	const FString Record = FString::Printf(
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u ResetEpoch=%u Revision=%u Location=%s Velocity=%s Grounded=%d Locomotion=%d MoveIntent=%s MotorSource=%s MotorBudgetUE=%.3f MaxSpeedCmS=%.3f JumpSpeedCmS=%.3f GravityScale=%.3f ViewYaw=%.3f BodyYaw=%.3f FacingYaw=%.3f InputSequence=%u JumpTractionWeight=%.3f VerticalGripForceUE=%.3f Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), GetOwner() ? int32(GetOwner()->GetNetMode()) : -1, HasAuthority(),
		GetOwner() ? int32(GetOwner()->GetLocalRole()) : -1, *GetNameSafe(GetOwner()), *BodyId.ToString(), ControlEpoch,
		Snapshot.ResetEpoch, Snapshot.Revision, *GetOwner()->GetActorLocation().ToCompactString(), *GetVelocity().ToCompactString(),
		IsGrounded(), bLocomotionEnabled, *GetMoveIntent().ToCompactString(), *GetNameSafe(FishingMotorSource.Get()),
		FishingMotorSource.IsValid() ? FishingMotorMaxForce : -1.0, MaxMovementSpeedCmS, JumpSpeedCmS, GravityScale,
		ViewInput.Yaw, Body ? Body->GetComponentRotation().Yaw : 0.0, FacingYawDegrees,
		HasAuthority() ? AcceptedInputSequence : LocalInputSequence, GetJumpTractionWeight(), GetVerticalGripForceFromAuthority(), *Reason.ToString());
	if (Event.ToString().EndsWith(TEXT("_rejected")))
	{
		UE_LOG(LogCatPhysicsGrab, Warning, TEXT("%s"), *Record);
	}
	else
	{
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("%s"), *Record);
	}
}
void UCatPhysicalBodyComponent::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	ReleaseConnectionsFromAuthority(TEXT("EndPlay"));
	if (auto* Policy = GetWorld() ? GetWorld()->GetSubsystem<UCatLightPropSubsystem>() : nullptr)
	{
		Policy->UnregisterBody(Body);
		if (CharacterMovement) Policy->UnregisterBody(CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent());
		Policy->UnregisterBody(LeftHand);
		Policy->UnregisterBody(RightHand);
	}
	ExternalForces.Reset();
	FishingMotorSource.Reset(); bFishingHoldActive=false;
	if (LeftArm) LeftArm->BreakConstraint();
	if (RightArm) RightArm->BreakConstraint();
	Super::EndPlay(EndPlayReason);
}

void UCatPhysicalBodyComponent::AppendSupportQueryIgnores(FCollisionQueryParams& Params) const
{
	if (auto* Policy = GetWorld()->GetSubsystem<UCatLightPropSubsystem>()) Policy->AppendSupportQueryIgnores(Params);
}
