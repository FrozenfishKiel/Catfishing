#include "Character/CatCharacterMovementComponent.h"

#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

namespace
{
// Actual grounded CMC and frozen endpoint prediction consume the same finite force law.
FVector IntegrateGroundVelocity(FCatBodyDriveSample& Drive, const FVector& Position, FVector Velocity,
    FVector ExternalForce, double Mass, double ResistanceNewtons, double Dt)
{
    ExternalForce.Z = 0;
    const bool bSupportOnly = Drive.bLocomotion && Drive.bFishing && Drive.MoveIntent.IsNearlyZero();
    if (!bSupportOnly) ExternalForce += UCatPhysicalBodyComponent::ComputeDriveForce(Drive,Position,Velocity,Mass,Dt);
    if (!Drive.bLocomotion) ExternalForce -= FVector(Velocity.X,Velocity.Y,0)*Mass*FMath::Min(8.0,1.0/Dt);
    Velocity += ExternalForce*(Dt/Mass);
    // 4c5e8cd: passive stance can stop at zero, but can never spring toward an old position.
    const double Speed = Velocity.Size2D();
    const double Support = bSupportOnly ? Drive.MaxForce : 0;
    const double Reduction = FMath::Min(Speed,(ResistanceNewtons*100+Support)*Dt/Mass);
    if (Speed > UE_DOUBLE_SMALL_NUMBER) { Velocity.X*=1-Reduction/Speed; Velocity.Y*=1-Reduction/Speed; }
    return Velocity;
}
}

UCatCharacterMovementComponent::UCatCharacterMovementComponent()
{
	bRunPhysicsWithNoController = true;
	bEnablePhysicsInteraction = false;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	Mass = 4.0f;
	MaxAcceleration = BrakingDecelerationWalking = 6000.0f;
}

void UCatCharacterMovementComponent::AdvanceFromAuthority(float DeltaSeconds)
{
	auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || !Cat->HasAuthority() || DeltaSeconds <= 0) return;
	MaxWalkSpeed = Body->MaxMovementSpeedCmS;
	JumpZVelocity = Body->JumpSpeedCmS;
	GravityScale = Body->GravityScale;
	Acceleration = Body->GetMoveIntent() * GetMaxAcceleration();
	// 4c5e8cd: continuous traction uses the same small steps even on slow frames.
    MovementExternalForce = QueuedExternalImpulse / DeltaSeconds;
    QueuedExternalImpulse = FVector::ZeroVector;
    MovementExternalForce.Z += Body->GetVerticalGripForceFromAuthority();
    if (IsMovingOnGround() && MovementExternalForce.Z > -GetGravityZ()*FMath::Max(1.0f,Mass))
    {
        SetMovementMode(MOVE_Falling);
        Body->NotifyGripLiftFromAuthority();
    }
    const bool bTraction = Body->HasFishingMotor() || Body->CaptureDriveSample().bConnected || !MovementExternalForce.IsNearlyZero();
    const float Step = bTraction ? FMath::Min(MaxSimulationTimeStep, 1.0f / 120.0f) : MaxSimulationTimeStep;
    TGuardValue<float> StepGuard(MaxSimulationTimeStep, Step);
    TGuardValue<int32> IterationGuard(MaxSimulationIterations, bTraction
        ? FMath::Max(MaxSimulationIterations, FMath::CeilToInt(FMath::Min(DeltaSeconds, .25f) / Step) + 1) : MaxSimulationIterations);
    Super::PerformMovement(DeltaSeconds);
    MovementExternalForce = FVector::ZeroVector;
}

void UCatCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || DeltaTime <= 0) return;
	// Grounded voluntary braking and external traction share one finite force budget.
	// Vertical force is integrated only by NewFallVelocity; PhysFalling restores CalcVelocity's Z.
	FVector Force = Body->GetExternalForceFromAuthority();
	Force += MovementExternalForce;
	Force.Z = 0;
    if (IsMovingOnGround())
    {
        auto Drive = Body->CaptureDriveSample();
        const double Resistance = FMath::IsFinite(GroundResistanceNewtons) ? FMath::Max(0.0f,GroundResistanceNewtons) : .8;
        Velocity = IntegrateGroundVelocity(Drive,CharacterOwner->GetActorLocation(),Velocity,Force,FMath::Max(1.0f,Mass),Resistance,DeltaTime);
    }
    else Velocity += Force*(DeltaTime/FMath::Max(1.0f,Mass));
}

FVector UCatCharacterMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
    return Super::NewFallVelocity(InitialVelocity, Gravity + FVector(0,0,MovementExternalForce.Z/FMath::Max(1.0f,Mass)), DeltaTime);
}

void UCatCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	const auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || !UpdatedComponent) return;
	const double Yaw = FMath::FixedTurn(UpdatedComponent->GetComponentRotation().Yaw,
		Body->GetFacingYawDegrees(), 720.0 * FMath::Max(0.0f, DeltaTime));
	MoveUpdatedComponent(FVector::ZeroVector, FRotator(0,Yaw,0), true);
}

bool UCatCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (Cast<ACatCharacter>(Hit.GetActor()) || UCatLightPropComponent::FindFor(Hit.GetComponent())) return false;
	return Super::IsWalkable(Hit);
}

void UCatCharacterMovementComponent::InitCollisionParams(FCollisionQueryParams& OutParams, FCollisionResponseParams& OutResponseParam) const
{
	Super::InitCollisionParams(OutParams, OutResponseParam);
	if (const auto* Cat = Cast<ACatCharacter>(CharacterOwner)) Cat->GetPhysicalBodyComponent()->AppendSupportQueryIgnores(OutParams);
}

void UCatCharacterMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	ClearQueuedExternalImpulse();
	if (auto* Cat = Cast<ACatCharacter>(CharacterOwner)) Cat->GetPhysicalBodyComponent()->ClearControlIntent(TEXT("MovementStopped"));
}

void UCatCharacterMovementComponent::UpdatePeerPushContacts()
{
	auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	if (!Cat || !Cat->HasAuthority()) return;
	auto* Body = Cat->GetPhysicalBodyComponent();
	const auto* Capsule = Cat->GetCapsuleComponent();
	for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
	{
		auto* Other = *It;
		if (Other == Cat || Other->GetUniqueID() < Cat->GetUniqueID()) continue;
		auto* OtherBody = Other->GetPhysicalBodyComponent();
		auto* OtherMovement = Cast<UCatCharacterMovementComponent>(Other->GetCharacterMovement());
		Body->ClearExternalForce(OtherMovement);
		OtherBody->ClearExternalForce(this);
		if (!OtherBody->GetBody()) continue;
		const FVector Difference = Other->GetActorLocation() - Cat->GetActorLocation();
		const double Radius = Capsule->GetScaledCapsuleRadius() + Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (Difference.Size2D() > Radius + 3.0 || FMath::Abs(Difference.Z) >
			Capsule->GetScaledCapsuleHalfHeight() + Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Radius) continue;
		const FVector Normal = Difference.GetSafeNormal2D();
		const FVector RelativeIntent = Body->GetMoveIntent() * Body->MaxMovementSpeedCmS
			- OtherBody->GetMoveIntent() * OtherBody->MaxMovementSpeedCmS;
		const double ClosingSpeed = FVector::DotProduct(RelativeIntent, Normal);
		const double Penetration = FMath::Max(0.0, Radius - Difference.Size2D());
		const FVector Force = Normal * FMath::Clamp(ClosingSpeed * 12.0 + Penetration * 650.0, 0.0, 3000.0);
		if (Force.IsNearlyZero()) continue;
		Body->SetExternalForceFromAuthority(OtherMovement, -Force);
		OtherBody->SetExternalForceFromAuthority(this, Force);
	}
}

void UCatCharacterMovementComponent::ObserveSnapshot(const FVector& ObservedVelocity, const FVector& ObservedIntent)
{
	if (!CharacterOwner || CharacterOwner->HasAuthority()) return;
	Velocity = ObservedVelocity;
	Acceleration = ObservedIntent * GetMaxAcceleration();
}

bool UCatCharacterMovementComponent::IsFalling() const
{
	const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	if (Cat && !Cat->HasAuthority())
	{
		const auto* Body = Cat->GetPhysicalBodyComponent();
		return Body->HasMovementSample() && !Body->IsGrounded();
	}
	return Super::IsFalling();
}

bool UCatCharacterMovementComponent::IsMovingOnGround() const
{
	const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	return Cat && !Cat->HasAuthority() ? Cat->GetPhysicalBodyComponent()->IsGrounded() : Super::IsMovingOnGround();
}

FCatCMCMotionPrediction UCatCharacterMovementComponent::CaptureMotionPrediction()
{
    auto* Body = CastChecked<ACatCharacter>(CharacterOwner)->GetPhysicalBodyComponent();
    FCatCMCMotionPrediction Sample;
    Sample.Drive = Body->CaptureDriveSample();
    Sample.Position = CharacterOwner->GetActorLocation(); Sample.Velocity = Velocity;
    Sample.ExternalForce = Body->GetExternalForceFromAuthority(); Sample.ExternalForce.Z = Body->GetVerticalGripForceFromAuthority();
    Sample.MassKg = FMath::Max(1.0f, Mass);
    Sample.GroundResistanceNewtons = FMath::IsFinite(GroundResistanceNewtons) ? FMath::Max(0.0f, GroundResistanceNewtons) : .8;
    Sample.bGrounded = IsMovingOnGround(); Sample.GravityZ = GetGravityZ();
    Sample.bAcceptVerticalLineForce = !Sample.bGrounded;
    return Sample;
}

void UCatCharacterMovementComponent::AdvanceMotionPrediction(FCatCMCMotionPrediction& Sample, const FVector& LineForceNewtons, double Seconds)
{
    for (double Remaining = Seconds; Remaining > UE_DOUBLE_SMALL_NUMBER; )
    {
        const double H = FMath::Min(Remaining, 1.0 / 120.0);
        FVector Force = Sample.ExternalForce + LineForceNewtons * 100.0;
        if (!Sample.bAcceptVerticalLineForce) Force.Z = Sample.ExternalForce.Z;
        if (Sample.bGrounded && Force.Z > -Sample.GravityZ*Sample.MassKg) Sample.bGrounded = false;
        const FVector OldVelocity = Sample.Velocity;
        if (Sample.bGrounded)
            Sample.Velocity = IntegrateGroundVelocity(Sample.Drive,Sample.Position,Sample.Velocity,Force,Sample.MassKg,Sample.GroundResistanceNewtons,H);
        else
        {
            Force.Z += Sample.GravityZ*Sample.MassKg;
            Sample.Velocity += Force*(H/Sample.MassKg);
        }
        Sample.Position += (Sample.bGrounded ? Sample.Velocity : (OldVelocity+Sample.Velocity)*.5) * H;
        Remaining -= H;
    }
}

double UCatCharacterMovementComponent::GetExternalTractionTravelLimit(const FVector& Direction, double MaximumDistance) const
{
    if (!HasValidData() || !UpdatedPrimitive || !GetWorld() || MovementMode == MOVE_None
        || !FMath::IsFinite(MaximumDistance) || MaximumDistance <= 0) return 0;
    const FVector Axis = ConstrainDirectionToPlane(Direction.GetSafeNormal2D());
    if (Axis.IsNearlyZero()) return 0;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CatCMCTractionPrediction),false,CharacterOwner);
    FCollisionResponseParams Responses;
    InitCollisionParams(Params,Responses);
    Params.AddIgnoredActors(UpdatedPrimitive->GetMoveIgnoreActors());
    Params.AddIgnoredComponents(UpdatedPrimitive->GetMoveIgnoreComponents());
    FHitResult Hit;
    const bool Blocked = GetWorld()->SweepSingleByChannel(Hit, UpdatedComponent->GetComponentLocation(),
        UpdatedComponent->GetComponentLocation()+Axis*MaximumDistance, UpdatedComponent->GetComponentQuat(),
        UpdatedPrimitive->GetCollisionObjectType(), UpdatedPrimitive->GetCollisionShape(), Params, Responses);
    // Only a read-only feasibility bound. Actual CMC movement still owns collisions and stepping.
    return Blocked && !IsWalkable(Hit) && FVector::DotProduct(Hit.Normal,Axis)<-.001
        ? FMath::Max(0.0,MaximumDistance*Hit.Time-.1) : MaximumDistance;
}
