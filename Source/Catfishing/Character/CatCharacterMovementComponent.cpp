#include "Character/CatCharacterMovementComponent.h"
#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"

#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "EngineUtils.h"

namespace
{
// Actual grounded CMC and frozen endpoint prediction consume the same finite force law.
FVector IntegrateGroundVelocity(FCatBodyDriveSample& Drive, const FVector& Position, FVector Velocity,
    FVector ExternalForce, double Mass, double ResistanceNewtons, double Dt)
{
    ExternalForce.Z = 0;
    const bool bSupportOnly = Drive.bLocomotion && (Drive.bFishing || Drive.bCooperative) && Drive.MoveIntent.IsNearlyZero();
    if (!bSupportOnly) ExternalForce += UCatPhysicalBodyComponent::ComputeDriveForce(Drive,Position,Velocity,Mass,Dt);
    if (!Drive.bLocomotion) ExternalForce -= FVector(Velocity.X,Velocity.Y,0)*Mass*FMath::Min(8.0,1.0/Dt);
    Velocity += ExternalForce*(Dt/Mass);
    // 4c5e8cd: passive stance can stop at zero, but can never spring toward an old position.
    const double Speed = Velocity.Size2D();
    const double Support = bSupportOnly && (!Drive.bPassiveBodyContact || !Drive.bBodyContactDriven) ? Drive.MaxForce : 0;
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
	SetNetworkMoveDataContainer(NetworkMoves);
	NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCatCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || !Body->GetBody()) return;
	if (ObservedControlEpoch != Body->GetControlEpoch())
	{
		ObservedControlEpoch = Body->GetControlEpoch();
		ResetControlPrediction();
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=cmc_prediction_started World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u Result=SavedMovesAndReconciliation"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Cat->HasAuthority(), int32(Cat->GetLocalRole()),
			*GetNameSafe(Cat), *Body->GetBodyId().ToString(), ObservedControlEpoch);
	}
	if (Cat->IsLocallyControlled() || (Cat->HasAuthority() && !Cat->GetController()))
		AddInputVector(Body->GetLocalMoveIntent(), true);
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	// Authority-only controllers (e.g. domain test worlds or an unconnected gameplay host)
	// have neither local player input nor incoming ServerMoves. Preserve their force/floor
	// simulation. A real remote player's UNetConnection is a Player and never enters here.
	const auto* PC = Cast<APlayerController>(Cat->GetController());
	if (Cat->HasAuthority() && PC && !PC->Player && !Cat->IsLocallyControlled())
		ControlledCharacterMove(Body->GetLocalMoveIntent(), DeltaTime);
	Body->CompleteCharacterMovement();
	if (Cat->IsLocallyControlled() && !Cat->HasAuthority() && (!Velocity.IsNearlyZero(3) || !Body->GetLocalMoveIntent().IsNearlyZero())
		&& GetWorld()->GetTimeSeconds() >= NextPredictionLogSeconds)
	{
		NextPredictionLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		const auto* Data = static_cast<FNetworkPredictionData_Client_Character*>(GetPredictionData_Client());
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=cmc_prediction_sample World=%s NetMode=%d Authority=0 LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u PendingMoves=%d MoveTime=%.3f Location=%s Velocity=%s Result=LocallyPredicted"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Cat->GetLocalRole()), *GetNameSafe(Cat), *Body->GetBodyId().ToString(),
			ObservedControlEpoch, Data->SavedMoves.Num(), Data->CurrentTimeStamp, *Cat->GetActorLocation().ToCompactString(), *Velocity.ToCompactString());
	}
}

bool UCatCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || !Body->IsLocomotionEnabled()) return false;
	JumpZVelocity = Body->JumpSpeedCmS;
	const bool bJumped = Super::DoJump(bReplayingMoves, DeltaTime);
	if (bJumped && Cat->HasAuthority()) Body->NotifyCharacterJump();
	return bJumped;
}

void UCatCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || !Body->GetBody() || DeltaSeconds <= 0) return;
	const bool bAuthority = Cat->HasAuthority();
	if (bAuthority && Cat->GetController() && !Cat->IsLocallyControlled())
		Body->SetViewIntent(Cat->GetController()->GetControlRotation());
	// Acceleration is restored from each saved/network move, including replay and analogue input.
	const FVector MoveIntent = (Acceleration / FMath::Max(1.0f, GetMaxAcceleration())).GetClampedToMaxSize(1.0);
	if (bAuthority) Body->SetMoveIntent(MoveIntent);
	if (!bReplayPolicy)
	{
		ActiveDrive = bAuthority ? Body->CaptureDriveSample() : Body->GetReplicatedDrive();
		ActiveExternalForce = bAuthority ? Body->GetExternalForceFromAuthority() : Body->GetReplicatedExternalForce();
	}
	ActiveDrive.MoveIntent = Body->IsLocomotionEnabled() ? MoveIntent : FVector::ZeroVector;
	ActiveDrive.bLocomotion = Body->IsLocomotionEnabled();
	if (const auto* Controller = Cast<ACatfishingPlayerController>(Cat->GetController()); Controller && Controller->IsDayTransitionInputBlocked())
		ActiveDrive.MoveIntent = FVector::ZeroVector;
	if (!ActiveDrive.bFishing) ActiveDrive.MaxSpeed = Body->MaxMovementSpeedCmS;
	const auto EffortDrive = ActiveDrive;
	const FVector StartPosition = Cat->GetActorLocation();
	const FVector StartCorrection = TotalMotionCorrection;
	const uint32 StartResetEpoch = Body->GetResetEpoch();
	const bool bStartedGrounded = IsMovingOnGround();
	FVector IntendedDisplacement = EffortDrive.MoveIntent * EffortDrive.MaxSpeed * DeltaSeconds;
	if (EffortDrive.bCooperative && !EffortDrive.bPassiveBodyContact && EffortDrive.MoveIntent.IsNearlyZero() && EffortDrive.MaxForce > 0)
	{
		// A stance actively opposes the load and existing drift. Convert relative effort to an
		// equivalent directional intent; no load and no drift produce no fictitious support bill.
		FVector Reaction = -Body->GetExternalForceFromAuthority() - Velocity * (FMath::Max(1.0f, Mass) / DeltaSeconds);
		Reaction.Z = 0;
		const double Effort = FMath::Clamp(Reaction.Size() / EffortDrive.MaxForce, 0.0, 1.0);
		IntendedDisplacement = Reaction.GetSafeNormal() * Effort
			* GetDefault<UCatPhysicalEffortSettings>()->SupportReferenceSpeedCmS * DeltaSeconds;
	}
	MaxWalkSpeed = Body->MaxMovementSpeedCmS;
	JumpZVelocity = Body->JumpSpeedCmS;
	GravityScale = Body->GravityScale;
	Acceleration = ActiveDrive.MoveIntent * GetMaxAcceleration();
	// 4c5e8cd: continuous traction uses the same small steps even on slow frames.
    MovementExternalForce = bAuthority ? QueuedExternalImpulse / DeltaSeconds : FVector::ZeroVector;
    if (bAuthority) QueuedExternalImpulse = FVector::ZeroVector;
    if (bAuthority) MovementExternalForce.Z += Body->GetVerticalGripForceFromAuthority();
    else { MovementExternalForce.Z = ActiveExternalForce.Z; ActiveExternalForce.Z = 0; }
    LastExternalForce = ActiveExternalForce + MovementExternalForce;
    LastExternalForce.Z = MovementExternalForce.Z; // CalcVelocity discards source Z; vertical grip is integrated once.
    if (IsMovingOnGround() && MovementExternalForce.Z > -GetGravityZ()*FMath::Max(1.0f,Mass))
    {
        SetMovementMode(MOVE_Falling);
        if (bAuthority) Body->NotifyGripLiftFromAuthority();
    }
    const bool bTraction = ActiveDrive.bFishing || ActiveDrive.bConnected || !MovementExternalForce.IsNearlyZero();
    const float Step = bTraction ? FMath::Min(MaxSimulationTimeStep, 1.0f / 120.0f) : MaxSimulationTimeStep;
    TGuardValue<float> StepGuard(MaxSimulationTimeStep, Step);
    TGuardValue<int32> IterationGuard(MaxSimulationIterations, bTraction
        ? FMath::Max(MaxSimulationIterations, FMath::CeilToInt(FMath::Min(DeltaSeconds, .25f) / Step) + 1) : MaxSimulationIterations);
    Super::PerformMovement(DeltaSeconds);
    const FVector BeforePeerCorrection = TotalMotionCorrection;
    ResolveModelPeerPenetration();
    const FVector PeerCorrection = TotalMotionCorrection - BeforePeerCorrection;
    MovementExternalForce = FVector::ZeroVector;
	bQueuedExternalLoad = false;
	bReplayPolicy = false;
	if (auto* Effort = bAuthority ? Cat->FindComponentByClass<UCatPhysicalEffortComponent>() : nullptr)
		if (StartResetEpoch == Body->GetResetEpoch())
		{
			FVector ActualDisplacement = Cat->GetActorLocation() - StartPosition - (TotalMotionCorrection - StartCorrection);
            // Reverse separation cancels an attempted step. Removing that correction must
            // not credit the rejected step as successful progress (for example against a wall).
            const FVector IntentDirection = IntendedDisplacement.GetSafeNormal2D();
            ActualDisplacement += IntentDirection * FMath::Min(0.0, FVector::DotProduct(PeerCorrection,IntentDirection));
			ActualDisplacement.Z = 0;
			Effort->SettleMovementFromAuthority(EffortDrive, IntendedDisplacement, ActualDisplacement, DeltaSeconds, bStartedGrounded);
		}
	Body->CompleteCharacterMovement();
}

void UCatCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
	auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	if (!Body || DeltaTime <= 0) return;
	// Grounded voluntary braking and external traction share one finite force budget.
	// Vertical force is integrated only by NewFallVelocity; PhysFalling restores CalcVelocity's Z.
	FVector Force = Cat->HasAuthority() ? Body->GetExternalForceFromAuthority() : ActiveExternalForce;
	Force += MovementExternalForce;
	Force.Z = 0;
    if (IsMovingOnGround())
    {
        auto Drive = Cat->HasAuthority() ? Body->CaptureDriveSample() : ActiveDrive;
        Drive.MoveIntent = ActiveDrive.MoveIntent;
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
	const bool bAim = ActiveDrive.bFishing || Body->GetGrab()->IsReaching(true) || Body->GetGrab()->IsReaching(false);
	const double TargetYaw = bAim ? Body->GetViewIntent().Yaw : (!ActiveDrive.MoveIntent.IsNearlyZero()
		? ActiveDrive.MoveIntent.Rotation().Yaw : UpdatedComponent->GetComponentRotation().Yaw);
	const double Yaw = FMath::FixedTurn(UpdatedComponent->GetComponentRotation().Yaw, TargetYaw, 720.0 * FMath::Max(0.0f, DeltaTime));
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
	if (UCatModelContactComponent::UsesModelContacts(CharacterOwner))
		for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
			if (*It != CharacterOwner && UCatModelContactComponent::UsesModelContacts(*It)) OutParams.AddIgnoredActor(*It);
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
		FVector Normal;
		double Penetration = 0;
		const auto* Model = Cat->FindComponentByClass<UCatModelContactComponent>();
		const auto* OtherModel = Other->FindComponentByClass<UCatModelContactComponent>();
		if (Model && OtherModel && Model->HasModelContacts() && OtherModel->HasModelContacts())
		{
			if (!Model->FindPeerContact(OtherModel, Normal, Penetration, 3.0)) continue;
		}
		else
		{
			// Native test characters without a mesh retain the existing capsule contact contract.
			const FVector Difference = Other->GetActorLocation() - Cat->GetActorLocation();
			const double Radius = Capsule->GetScaledCapsuleRadius() + Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
			if (Difference.Size2D() > Radius + 3.0 || FMath::Abs(Difference.Z) >
				Capsule->GetScaledCapsuleHalfHeight() + Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Radius) continue;
			Normal = Difference.GetSafeNormal2D();
			Penetration = FMath::Max(0.0, Radius - Difference.Size2D());
		}
		// Contact transmits motion; a pressed key must not manufacture a second, stamina-free motor.
		const double ClosingSpeed = FVector::DotProduct(Body->GetVelocity() - OtherBody->GetVelocity(), Normal);
		const auto IntoContactDrive = [](UCatPhysicalBodyComponent* Participant, const FVector& Axis, double BodyMass)
		{
			auto Drive = Participant->CaptureDriveSample();
			if (!Drive.bLocomotion || Drive.MoveIntent.IsNearlyZero()) return 0.0;
			if (!Drive.bFishing)
				if (const auto* Effort = Participant->GetOwner()->FindComponentByClass<UCatPhysicalEffortComponent>())
				{ Drive.bCooperative = true; Drive.MaxForce = Effort->GetMaximumForceKgCmS2(); }
			return FVector::DotProduct(UCatPhysicalBodyComponent::ComputeDriveForce(Drive,
				Participant->GetOwner()->GetActorLocation(), Participant->GetVelocity(), BodyMass, 1.0/120.0), Axis);
		};
		// Share the bounded motor reaction according to both inverse masses. A moving
		// cat must retain its share of acceleration while transmitting the rest to its peer.
		const double MassA = FMath::Max(1.0f, Mass), MassB = FMath::Max(1.0f, OtherMovement->Mass);
		const double DriveA = IntoContactDrive(Body, Normal, MassA), DriveB = IntoContactDrive(OtherBody, -Normal, MassB);
		// A retained grab already couples both motors. Keep that contact law and let the
		// grip solve separation, avoiding a second position constraint against its anchors.
		const bool bConstrained = Model && Model->HasTractionConnectionWith(OtherModel);
		const double MotorReaction = bConstrained ? FMath::Max(0.0, FMath::Max(DriveA,DriveB))
			: FMath::Max(0.0, (DriveA / MassA + DriveB / MassB) / (1.0 / MassA + 1.0 / MassB));
		const FVector Force = Normal * FMath::Max(MotorReaction, FMath::Clamp(ClosingSpeed * 12.0 + Penetration * 650.0, 0.0, 3000.0));
		Body->SetExternalForceFromAuthority(OtherMovement, -Force, false, true);
		OtherBody->SetExternalForceFromAuthority(this, Force, false, true);
		if (Model && OtherModel && Model->HasModelContacts() && OtherModel->HasModelContacts()
			&& GetWorld()->GetTimeSeconds() >= NextModelContactLogSeconds)
		{
			NextModelContactLogSeconds = GetWorld()->GetTimeSeconds() + 1;
			UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=model_contact_push World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s Peer=%s PeerBodyId=%s HorizontalSeparationEstimateCm=%.3f ForceOnPeerN=%s Result=ReciprocalHorizontalForce"),
				*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Cat->GetLocalRole()), *GetNameSafe(Cat), *Body->GetBodyId().ToString(),
				*GetNameSafe(Other), *OtherBody->GetBodyId().ToString(), Penetration, *(Force/100).ToCompactString());
		}
	}
}

void UCatCharacterMovementComponent::ResolveModelPeerPenetration()
{
	const auto* Model = CharacterOwner ? CharacterOwner->FindComponentByClass<UCatModelContactComponent>() : nullptr;
	if (!HasValidData() || !CharacterOwner->HasAuthority() || !Model || !Model->HasModelContacts()) return;
	const FVector Before = UpdatedComponent->GetComponentLocation();
	const FVector PreviousCorrection = TotalMotionCorrection;
	const FVector SavedVelocity = Velocity;
	double MaximumDepth = 0;
	// Correct only already intersecting model surfaces. Every adjustment sweeps the terrain
	// capsule and follows its actual walkable floor, rather than teleporting horizontally into a slope.
	// Articulated tails can sweep through several contacts during a walking pose or a hitch.
	// Keep each terrain move bounded, but allow the intersecting pair to finish separating.
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		bool bAdjusted = false;
		for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == CharacterOwner || Model->HasTractionConnectionWith(It->FindComponentByClass<UCatModelContactComponent>())) continue;
			FVector Normal; double Depth;
			if (!Model->FindPeerContact(It->FindComponentByClass<UCatModelContactComponent>(), Normal, Depth) || Depth <= .1) continue;
			MaximumDepth = FMath::Max(MaximumDepth, Depth);
			const FVector Adjustment = -Normal * FMath::Min(4.0, (Depth - .1) * .5);
			const FVector OldLocation = UpdatedComponent->GetComponentLocation();
			if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
			{
				MoveAlongFloor(Adjustment, 1.0f);
				FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
				if (CurrentFloor.IsWalkableFloor()) { AdjustFloorHeight(); SetBaseFromFloor(CurrentFloor); }
				else SetMovementMode(MOVE_Falling);
			}
			else
			{
				FHitResult Hit;
				SafeMoveUpdatedComponent(Adjustment, UpdatedComponent->GetComponentQuat(), true, Hit);
			}
			bAdjusted |= !UpdatedComponent->GetComponentLocation().Equals(OldLocation, .001);
		}
		if (!bAdjusted) break;
	}
	Velocity = SavedVelocity;
	// Collision correction must never become paid player progress or a second motor.
	TotalMotionCorrection = PreviousCorrection + UpdatedComponent->GetComponentLocation() - Before;
	if (MaximumDepth > .2 && GetWorld()->GetTimeSeconds() >= NextPeerSeparationLogSeconds)
	{
		NextPeerSeparationLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		const auto* Body = CastChecked<ACatCharacter>(CharacterOwner)->GetPhysicalBodyComponent();
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=model_contact_resolved World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s HorizontalSeparationEstimateCm=%.3f CorrectionCm=%s FloorNormalZ=%.4f Grounded=%d Result=TerrainSweptSeparation"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(CharacterOwner->GetLocalRole()), *GetNameSafe(CharacterOwner),
			*Body->GetBodyId().ToString(), MaximumDepth, *(UpdatedComponent->GetComponentLocation()-Before).ToCompactString(),
			CurrentFloor.HitResult.ImpactNormal.Z, IsMovingOnGround());
	}
}

bool UCatCharacterMovementComponent::ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation)
{
	const FVector Before = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const bool bResolved = Super::ResolvePenetrationImpl(Adjustment, Hit, Rotation);
	if (UpdatedComponent) TotalMotionCorrection += UpdatedComponent->GetComponentLocation() - Before;
	return bResolved;
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
