#include "Character/Physics/CatPhysicsPrototypePawn.h"

#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

ACatPhysicsPrototypePawn::ACatPhysicsPrototypePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bAlwaysRelevant = true; // Deliberately bounded experiment: both players see the complete contact scene.
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30.0f);
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicsBody"));
	SetRootComponent(Body);
	Body->InitBoxExtent(FVector(13.0, 5.0, 7.0));
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetMassOverrideInKg(NAME_None, 4.0f, true);
	Body->SetLinearDamping(0.15f);
	Body->SetAngularDamping(0.8f);
	Body->BodyInstance.bUseCCD = true;
	LeftHand = CreateDefaultSubobject<USphereComponent>(TEXT("LeftPhysicsHand"));
	RightHand = CreateDefaultSubobject<USphereComponent>(TEXT("RightPhysicsHand"));
	for (USphereComponent* Hand : {LeftHand.Get(), RightHand.Get()})
	{
		Hand->SetupAttachment(Body);
		Hand->InitSphereRadius(UCatPhysicsGrabComponent::HandRadiusCm);
		Hand->SetCollisionProfileName(TEXT("PhysicsActor"));
		Hand->SetCollisionResponseToAllChannels(ECR_Block);
		Hand->SetMassOverrideInKg(NAME_None, 0.12f, true);
		Hand->SetLinearDamping(0.3f);
		Hand->SetAngularDamping(1.0f);
		Hand->BodyInstance.bUseCCD = true;
	}
	LeftHand->SetRelativeLocation(UCatPhysicsGrabComponent::RestHandLocal(true));
	RightHand->SetRelativeLocation(UCatPhysicsGrabComponent::RestHandLocal(false));
	LeftArm = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LeftShoulder"));
	RightArm = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("RightShoulder"));
	LeftArm->SetupAttachment(Body);
	RightArm->SetupAttachment(Body);
	Grab = CreateDefaultSubobject<UCatPhysicsGrabComponent>(TEXT("PhysicsGrab"));
	Visual = CreateDefaultSubobject<UCatPhysicsPrototypeVisualComponent>(TEXT("PrototypeVisual"));
}

void ACatPhysicsPrototypePawn::BeginPlay()
{
	Super::BeginPlay();
	SpawnTransform = GetActorTransform();
	ViewInput = FRotator(-15.0, GetActorRotation().Yaw, 0.0);
	if (HasAuthority())
	{
		PrototypeId = FGuid::NewGuid();
		Body->SetSimulatePhysics(true);
		LeftHand->SetSimulatePhysics(true);
		RightHand->SetSimulatePhysics(true);
		ConfigureArm(true);
		ConfigureArm(false);
	}
	else
	{
		// Match the independent server bodies: moving the root must not move a hand a second time before interpolation.
		LeftHand->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		RightHand->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		LeftHand->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RightHand->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	Grab->InitializeHands(Body, LeftHand, RightHand, LeftArm, RightArm);
	Grab->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	Visual->InitializeVisual(Body, LeftHand, RightHand);
	if (HasAuthority()) CaptureSnapshot();
	UE_LOG(LogCatPhysicsGrab, Log,
		TEXT("Event=physics_body_started World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s MassKg=%.3f Model=BodyAndTwoHands Replication=ServerSnapshots"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()),
		*GetName(), *PrototypeId.ToString(), HasAuthority() ? Body->GetMass() : 4.0f);
}

void ACatPhysicsPrototypePawn::ConfigureArm(const bool bLeft)
{
	UPhysicsConstraintComponent* Arm = bLeft ? LeftArm.Get() : RightArm.Get();
	USphereComponent* Hand = bLeft ? LeftHand.Get() : RightHand.Get();
	Arm->BreakConstraint();
	Arm->SetWorldLocation(Body->GetComponentTransform().TransformPosition(UCatPhysicsGrabComponent::ShoulderLocal(bLeft)));
	Arm->SetWorldRotation(GetActorRotation());
	Arm->SetLinearXLimit(LCM_Limited, 19.3f);
	Arm->SetLinearYLimit(LCM_Limited, 19.3f);
	Arm->SetLinearZLimit(LCM_Limited, 19.3f);
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
		FTransform(FQuat::Identity, UCatPhysicsGrabComponent::ShoulderLocal(bLeft)));
	Arm->SetLinearPositionTarget(UCatPhysicsGrabComponent::RestHandLocal(bLeft)
		- UCatPhysicsGrabComponent::ShoulderLocal(bLeft));
}

void ACatPhysicsPrototypePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACatPhysicsPrototypePawn, Snapshot);
	DOREPLIFETIME(ACatPhysicsPrototypePawn, PrototypeId);
	DOREPLIFETIME(ACatPhysicsPrototypePawn, ControlEpoch);
}

FVector ACatPhysicsPrototypePawn::GetVelocity() const
{
	return HasAuthority() && Body ? Body->GetPhysicsLinearVelocity() : Snapshot.Velocity;
}

void ACatPhysicsPrototypePawn::SetPrototypeInput(FVector2D Move, FRotator View)
{
	if (Move.ContainsNaN() || View.ContainsNaN()) return;
	Move = Move.GetClampedToMaxSize(1.0);
	View.Pitch = FMath::ClampAngle(View.Pitch, -85.0, 75.0);
	View.Yaw = FRotator::NormalizeAxis(View.Yaw);
	View.Roll = 0.0;
	MoveInput = Move;
	ViewInput = View;
	LastInputSeconds = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() && IsLocallyControlled() && LastInputSeconds - LastSendSeconds >= 1.0 / 30.0)
	{
		LastSendSeconds = LastInputSeconds;
		ServerSetPrototypeInput(Move, View, ControlEpoch, ++LocalInputSequence);
	}
}

void ACatPhysicsPrototypePawn::ServerSetPrototypeInput_Implementation(FVector2D Move, FRotator View,
	const uint32 Epoch, const uint32 Sequence)
{
	if (!GetController() || Epoch != ControlEpoch || Sequence == 0 || Move.ContainsNaN() || View.ContainsNaN()
		|| static_cast<int32>(Sequence - AcceptedInputSequence) <= 0)
	{
		if (GetWorld()->GetTimeSeconds() >= NextInputRejectLogSeconds)
		{
			NextInputRejectLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
			UE_LOG(LogCatPhysicsGrab, Warning,
				TEXT("Event=physics_body_input_rejected World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Epoch=%u CurrentEpoch=%u Sequence=%u AcceptedSequence=%u Result=InvalidOrStale"),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()),
				*PrototypeId.ToString(), Epoch, ControlEpoch, Sequence, AcceptedInputSequence);
		}
		return;
	}
	AcceptedInputSequence = Sequence;
	SetPrototypeInput(Move, View);
}

void ACatPhysicsPrototypePawn::SetGrabInput(const bool bLeft, const bool bHeld)
{
	Grab->SetGrabInput(bLeft, bHeld);
}

void ACatPhysicsPrototypePawn::RequestJump()
{
	if (!HasAuthority()) { if (IsLocallyControlled()) ServerRequestJump(ControlEpoch); return; }
	if (!bGrounded || GetWorld()->GetTimeSeconds() < SupportDisabledUntilSeconds)
	{
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_body_jump_rejected World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Result=NoGroundSupport"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString());
		return;
	}
	SupportDisabledUntilSeconds = GetWorld()->GetTimeSeconds() + 0.25;
	Body->AddImpulse(FVector(0.0, 0.0, 220.0 * Body->GetMass()));
	bGrounded = false;
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_body_jump World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Result=Applied"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString());
}

void ACatPhysicsPrototypePawn::ServerRequestJump_Implementation(const uint32 Epoch)
{
	if (GetController() && Epoch == ControlEpoch) RequestJump();
}

void ACatPhysicsPrototypePawn::RequestReset()
{
	if (HasAuthority()) ResetFromAuthority();
	else if (IsLocallyControlled()) ServerRequestReset(ControlEpoch);
}

void ACatPhysicsPrototypePawn::ServerRequestReset_Implementation(const uint32 Epoch)
{
	if (GetController() && Epoch == ControlEpoch) ResetFromAuthority();
}

void ACatPhysicsPrototypePawn::UpdatePhysicalMovement(const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f) return;
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	const double Mass = Body->GetMass();
	const double Gravity = FMath::Abs(GetWorld()->GetGravityZ());
	const bool bCanSupport = GetWorld()->GetTimeSeconds() >= SupportDisabledUntilSeconds
		&& Body->GetUpVector().Z > 0.35;
	bool bNewGrounded = false;
	if (bCanSupport)
	{
		for (const FVector FootLocal : {FVector(8.0, -3.0, 0.0), FVector(8.0, 3.0, 0.0),
			FVector(-8.0, -3.0, 0.0), FVector(-8.0, 3.0, 0.0)})
		{
			const FVector Origin = Body->GetComponentTransform().TransformPosition(FootLocal);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsFootSupport), false, this);
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin - FVector(0.0, 0.0, 26.0), ECC_Visibility, Params)
				|| Hit.ImpactNormal.Z < 0.55) continue;
			bNewGrounded = true;
			UPrimitiveComponent* Support = Hit.GetComponent();
			const double BaseVelocity = Support && Support->IsSimulatingPhysics(Hit.BoneName)
				? Support->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint, Hit.BoneName).Z : 0.0;
			const double VerticalVelocity = Body->GetPhysicsLinearVelocityAtPoint(Origin).Z - BaseVelocity;
			const double ForceZ = Mass * FMath::Clamp(Gravity + (20.0 - Hit.Distance) * 140.0 - VerticalVelocity * 18.0,
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
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString(), bGrounded);
	}
	const FRotator Yaw(0.0, ViewInput.Yaw, 0.0);
	const FVector Forward = Yaw.Vector();
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	const FVector DesiredVelocity = (Forward * MoveInput.X + Right * MoveInput.Y) * 100.0;
	if (bGrounded)
	{
		const FVector Acceleration = ((DesiredVelocity - FVector(Velocity.X, Velocity.Y, 0.0)) / 0.22).GetClampedToMaxSize(450.0);
		Body->AddForce(Acceleration * Mass);
	}
	// Bounded upright motor, deliberately weaker in the air so a held body still swings under gravity.
	const FVector UpError = FVector::CrossProduct(Body->GetUpVector(), FVector::UpVector);
	const double YawError = FVector::CrossProduct(Body->GetForwardVector().GetSafeNormal2D(), Forward).Z;
	const FVector AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
	const FVector AngularAcceleration = (UpError * (bGrounded ? 55.0 : 6.0)
		+ FVector(0.0, 0.0, YawError * (bGrounded ? 24.0 : 2.0)) - AngularVelocity * (bGrounded ? 9.0 : 0.7))
		.GetClampedToMaxSize(100.0);
	Body->AddTorqueInRadians(AngularAcceleration, NAME_None, true);
	bSupportSampleReady = true;
}

void ACatPhysicsPrototypePawn::CaptureSnapshot()
{
	Snapshot.BodyLocation = Body->GetComponentLocation();
	Snapshot.BodyRotation = Body->GetComponentRotation();
	Snapshot.Velocity = Body->GetPhysicsLinearVelocity();
	Snapshot.LeftHandLocation = LeftHand->GetComponentLocation();
	Snapshot.RightHandLocation = RightHand->GetComponentLocation();
	Snapshot.bGrounded = bGrounded;
	Snapshot.bSupportSampleReady = bSupportSampleReady;
	++Snapshot.Revision;
}

void ACatPhysicsPrototypePawn::OnRep_PhysicsSnapshot()
{
	if (!bReceivedSnapshot || ClientResetEpoch != Snapshot.ResetEpoch)
	{
		SetActorLocationAndRotation(Snapshot.BodyLocation, Snapshot.BodyRotation);
		LeftHand->SetWorldLocation(Snapshot.LeftHandLocation);
		RightHand->SetWorldLocation(Snapshot.RightHandLocation);
		ClientResetEpoch = Snapshot.ResetEpoch;
	}
	bReceivedSnapshot = true;
}

void ACatPhysicsPrototypePawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		if (GetController() && !IsLocallyControlled() && GetWorld()->GetTimeSeconds() - LastInputSeconds > 0.5)
			MoveInput = FVector2D::ZeroVector;
		UpdatePhysicalMovement(DeltaSeconds);
		if (GetActorLocation().ContainsNaN() || GetActorLocation().Z < -250.0) ResetFromAuthority();
		if (GetWorld()->GetTimeSeconds() - LastSnapshotSeconds >= 1.0 / 30.0)
		{
			LastSnapshotSeconds = GetWorld()->GetTimeSeconds();
			CaptureSnapshot();
		}
		if (GetWorld()->GetTimeSeconds() >= NextMotionLogSeconds && (!MoveInput.IsNearlyZero() || Grab->IsGripping(true) || Grab->IsGripping(false)))
		{
			NextMotionLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
			UE_LOG(LogCatPhysicsGrab, Log,
				TEXT("Event=physics_body_motion World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s Location=%s Velocity=%s Grounded=%d LeftGrip=%s RightGrip=%s"),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString(),
				*GetActorLocation().ToCompactString(), *GetVelocity().ToCompactString(), bGrounded,
				*Grab->GetGripState(true).GripId.ToString(), *Grab->GetGripState(false).GripId.ToString());
		}
	}
	else if (bReceivedSnapshot)
	{
		const double Alpha = 1.0 - FMath::Exp(-20.0 * FMath::Max(0.0f, DeltaSeconds));
		SetActorLocationAndRotation(FMath::Lerp(GetActorLocation(), Snapshot.BodyLocation, Alpha),
			FQuat::Slerp(GetActorQuat(), Snapshot.BodyRotation.Quaternion(), Alpha));
		LeftHand->SetWorldLocation(FMath::Lerp(LeftHand->GetComponentLocation(), Snapshot.LeftHandLocation, Alpha));
		RightHand->SetWorldLocation(FMath::Lerp(RightHand->GetComponentLocation(), Snapshot.RightHandLocation, Alpha));
		if (GetWorld()->GetTimeSeconds() >= NextMotionLogSeconds && (Snapshot.Velocity.SizeSquared() > 9.0
			|| Grab->IsGripping(true) || Grab->IsGripping(false)))
		{
			NextMotionLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
			UE_LOG(LogCatPhysicsGrab, Log,
				TEXT("Event=physics_body_snapshot_observed World=%s NetMode=%d Authority=0 LocalRole=%d BodyId=%s Revision=%u ResetEpoch=%u Location=%s Velocity=%s LeftGrip=%s RightGrip=%s"),
				*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString(),
				Snapshot.Revision, Snapshot.ResetEpoch, *Snapshot.BodyLocation.ToCompactString(), *Snapshot.Velocity.ToCompactString(),
				*Grab->GetGripState(true).GripId.ToString(), *Grab->GetGripState(false).GripId.ToString());
		}
	}
	Visual->SetHandReachState(Grab->IsReaching(true), Grab->IsReaching(false));
	if (bShowDiagnostics && GetNetMode() != NM_DedicatedServer)
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const bool bLeft = Index == 0;
			if (!Grab->IsReaching(bLeft)) continue;
			const FVector HandPosition = (bLeft ? LeftHand : RightHand)->GetComponentLocation();
			const FColor Color = Grab->IsGripping(bLeft) ? FColor::Green : FColor::Cyan;
			DrawDebugSphere(GetWorld(), HandPosition, UCatPhysicsGrabComponent::HandRadiusCm, 10, Color, false, -1.0f, 0, 0.25f);
			DrawDebugLine(GetWorld(), Body->GetComponentTransform().TransformPosition(UCatPhysicsGrabComponent::ShoulderLocal(bLeft)),
				HandPosition, Color, false, -1.0f, 0, 0.25f);
			if (Grab->IsGripping(bLeft)) DrawDebugPoint(GetWorld(), Grab->GetGripWorldLocation(bLeft), 6.0f, FColor::Yellow);
		}
	}
}

void ACatPhysicsPrototypePawn::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	(void)DeltaTime;
	const FRotator CameraRotation = IsLocallyControlled() ? ViewInput : FRotator(-15.0, GetActorRotation().Yaw, 0.0);
	const FVector Pivot = GetActorLocation() + FVector(0.0, 0.0, 12.0);
	FVector Desired = Pivot - CameraRotation.Vector() * 120.0;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsPrototypeCamera), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Pivot, Desired, FQuat::Identity, ECC_Camera,
		FCollisionShape::MakeSphere(3.0), Params)) Desired = Hit.Location;
	OutResult.Location = Desired;
	OutResult.Rotation = CameraRotation;
	OutResult.FOV = 75.0f;
}

void ACatPhysicsPrototypePawn::ReleaseConnections(const FName Reason)
{
	if (!HasAuthority()) return;
	Grab->ReleaseAllFromAuthority(Reason);
	for (TActorIterator<ACatPhysicsPrototypePawn> It(GetWorld()); It; ++It)
		if (*It != this && It->GetGrabComponent()) It->GetGrabComponent()->ReleaseTargetFromAuthority(this, Reason);
}

void ACatPhysicsPrototypePawn::ResetFromAuthority()
{
	bSupportSampleReady = false;
	ReleaseConnections(TEXT("Reset"));
	LeftArm->BreakConstraint();
	RightArm->BreakConstraint();
	Body->SetWorldTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		USphereComponent* Hand = Index == 0 ? LeftHand.Get() : RightHand.Get();
		Hand->SetWorldLocationAndRotation(SpawnTransform.TransformPosition(UCatPhysicsGrabComponent::RestHandLocal(Index == 0)),
			SpawnTransform.GetRotation(), false, nullptr, ETeleportType::ResetPhysics);
		Hand->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Hand->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		ConfigureArm(Index == 0);
	}
	MoveInput = FVector2D::ZeroVector;
	ViewInput = FRotator(-15.0, SpawnTransform.Rotator().Yaw, 0.0);
	SupportDisabledUntilSeconds = 0.0;
	++Snapshot.ResetEpoch;
	CaptureSnapshot();
	ForceNetUpdate();
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_body_reset World=%s NetMode=%d Authority=1 LocalRole=%d BodyId=%s ResetEpoch=%u Result=ReleasedAndReset"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *PrototypeId.ToString(), Snapshot.ResetEpoch);
}

void ACatPhysicsPrototypePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	++ControlEpoch;
	if (ControlEpoch == 0) ControlEpoch = 1;
	AcceptedInputSequence = 0;
	MoveInput = FVector2D::ZeroVector;
	LastInputSeconds = GetWorld()->GetTimeSeconds();
	Grab->BeginInputEpochFromAuthority();
}

void ACatPhysicsPrototypePawn::UnPossessed()
{
	ReleaseConnections(TEXT("Unpossessed"));
	MoveInput = FVector2D::ZeroVector;
	Grab->BeginInputEpochFromAuthority();
	Super::UnPossessed();
}

void ACatPhysicsPrototypePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseConnections(TEXT("EndPlay"));
	LeftArm->BreakConstraint();
	RightArm->BreakConstraint();
	Super::EndPlay(EndPlayReason);
}
