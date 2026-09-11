#include "Interaction/Grab/CatPhysicsGrabComponent.h"

#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Interaction/CatModelContactComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

DEFINE_LOG_CATEGORY(LogCatPhysicsGrab);

UCatPhysicsGrabComponent::UCatPhysicsGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UCatPhysicsGrabComponent::InitializeHands(UPrimitiveComponent* InBody, USphereComponent* InLeft,
	USphereComponent* InRight, UPhysicsConstraintComponent* InLeftArm, UPhysicsConstraintComponent* InRightArm, double InGeometryScale)
{
	GeometryScale = FMath::IsFinite(InGeometryScale) && InGeometryScale > UE_DOUBLE_SMALL_NUMBER ? InGeometryScale : 1.0;
	Body = InBody;
	Hands = {InLeft, InRight};
	Arms = {InLeftArm, InRightArm};
	if (!GetOwner()->HasAuthority() || !Contacts.IsEmpty()) return;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto* Contact = NewObject<UPhysicsConstraintComponent>(GetOwner(),
			Index == 0 ? TEXT("LeftGrabContact") : TEXT("RightGrabContact"));
		GetOwner()->AddInstanceComponent(Contact);
		Contact->RegisterComponent();
		Contact->SetLinearXLimit(LCM_Locked, 0.0f);
		Contact->SetLinearYLimit(LCM_Locked, 0.0f);
		Contact->SetLinearZLimit(LCM_Locked, 0.0f);
		Contact->SetAngularSwing1Limit(ACM_Free, 0.0f);
		Contact->SetAngularSwing2Limit(ACM_Free, 0.0f);
		Contact->SetAngularTwistLimit(ACM_Free, 0.0f);
		// kg cm/s^2, 600 N. A bounded grip can fail instead of stretching a body indefinitely.
		Contact->SetLinearBreakable(true, 60000.0f);
		Contact->SetDisableCollision(true);
		Contacts.Add(Contact);
	}
}

void UCatPhysicsGrabComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCatPhysicsGrabComponent, LeftGrip);
	DOREPLIFETIME(UCatPhysicsGrabComponent, RightGrip);
	DOREPLIFETIME(UCatPhysicsGrabComponent, InputEpoch);
}

void UCatPhysicsGrabComponent::SetGrabInput(const bool bLeft, const bool bHeld)
{
	if (!GetOwner()) return;
	if (GetOwner()->HasAuthority())
	{
		ApplyGrabInput(bLeft, bHeld);
		return;
	}
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) return;
	const uint32 Sequence = ++LocalSequence[bLeft ? 0 : 1];
	UE_LOG(LogCatPhysicsGrab, Log,
		TEXT("Event=physics_grip_requested World=%s NetMode=%d Authority=0 LocalRole=%d Actor=%s Hand=%s Epoch=%u Sequence=%u Held=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), bLeft ? TEXT("Left") : TEXT("Right"), InputEpoch, Sequence, bHeld);
	ServerSetGrabInput(bLeft, bHeld, InputEpoch, Sequence);
}

void UCatPhysicsGrabComponent::ServerSetGrabInput_Implementation(const bool bLeft, const bool bHeld,
	const uint32 Epoch, const uint32 Sequence)
{
	const int32 Index = bLeft ? 0 : 1;
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->GetController() || Epoch != InputEpoch || Sequence == 0
		|| static_cast<int32>(Sequence - AcceptedSequence[Index]) <= 0)
	{
		UE_LOG(LogCatPhysicsGrab, Warning,
			TEXT("Event=physics_grip_rejected World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s Hand=%s Epoch=%u CurrentEpoch=%u Sequence=%u Result=StaleOrUnpossessed"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()), static_cast<int32>(GetOwner()->GetLocalRole()),
			*GetNameSafe(GetOwner()), bLeft ? TEXT("Left") : TEXT("Right"), Epoch, InputEpoch, Sequence);
		return;
	}
	AcceptedSequence[Index] = Sequence;
	ApplyGrabInput(bLeft, bHeld);
}

void UCatPhysicsGrabComponent::ApplyGrabInput(const bool bLeft, const bool bHeld)
{
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	if (!bHeld)
	{
		if (State.bGripped && State.bExplicitHold)
		{
			LogGrip(bLeft, TEXT("physics_grip_input_released"), TEXT("ExplicitHoldRetained"));
			return;
		}
		ReleaseHand(bLeft, TEXT("InputReleased"), true);
		bLatchedUntilRelease[bLeft ? 0 : 1] = false;
		return;
	}
	const UCatPhysicalBodyComponent* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	if (PhysicalBody && !PhysicalBody->IsLocomotionEnabled()) { LogGrip(bLeft,TEXT("physics_grip_rejected"),TEXT("BodyUnavailable")); return; }
	if (State.bReaching) return;
	const FCatPhysicsGripState Previous = State;
	State.bReaching = true;
	++State.Revision;
	LogGrip(bLeft, TEXT("physics_grip_reach"), TEXT("Accepted"));
	OnGripChanged.Broadcast(this,bLeft,Previous,State);
	GetOwner()->ForceNetUpdate();
}

UPrimitiveComponent* UCatPhysicsGrabComponent::ResolveTarget(const FCatPhysicsGripState& State) const
{
	if (!IsValid(State.TargetActor)) return nullptr;
	TInlineComponentArray<UPrimitiveComponent*> Components(State.TargetActor);
	for (UPrimitiveComponent* Component : Components)
		if (IsValid(Component) && Component->GetFName() == State.TargetComponentName) return Component;
	return nullptr;
}

FVector UCatPhysicsGrabComponent::GetGripWorldLocation(const bool bLeft) const
{
	const FCatPhysicsGripState& State = GetGripState(bLeft);
	if (UPrimitiveComponent* Target = ResolveTarget(State))
	{
		const FTransform Frame = State.TargetBone.IsNone() ? Target->GetComponentTransform()
			: Target->GetSocketTransform(State.TargetBone, RTS_World);
		return Frame.TransformPosition(State.TargetLocalPoint);
	}
	const int32 Index = bLeft ? 0 : 1;
	return Hands.IsValidIndex(Index) && Hands[Index] ? Hands[Index]->GetComponentLocation() : FVector::ZeroVector;
}

void UCatPhysicsGrabComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner()->HasAuthority() || !Body || Hands.Num() != 2 || Arms.Num() != 2 || Contacts.Num() != 2) return;
	const auto* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const FVector Aim = PhysicalBody ? PhysicalBody->GetViewIntent().Vector() : GetOwner()->GetActorForwardVector();
	if (PhysicalBody && !PhysicalBody->IsLocomotionEnabled())
	{
		ReleaseAllFromAuthority(TEXT("BodyUnavailable"));
		for (UPhysicsConstraintComponent* Arm : Arms) Arm->SetLinearDriveParams(0,0,0);
		return;
	}
	UpdateHand(true, Aim);
	UpdateHand(false, Aim);
}

FVector UCatPhysicsGrabComponent::GetShoulderWorldLocation(bool bLeft) const
{
	return Body ? Body->GetComponentTransform().TransformPosition(ShoulderLocal(bLeft) * GeometryScale) : FVector::ZeroVector;
}
double UCatPhysicsGrabComponent::GetReachLengthCm() const
{
	return ReachLengthCm * GeometryScale * (Body ? Body->GetComponentScale().GetAbsMax() : 1.0);
}

bool UCatPhysicsGrabComponent::IsReachSurface(const UPrimitiveComponent* Target, const FName Bone, const bool bLeft) const
{
	const int32 Index = bLeft ? 0 : 1;
	if (!IsValid(Target) || !IsValid(Target->GetOwner()) || Target->GetOwner() == GetOwner()
		|| !Hands.IsValidIndex(Index) || !Hands[Index] || !Target->GetBodyInstance(Bone)
		|| Target->GetCollisionResponseToChannel(Hands[Index]->GetCollisionObjectType()) != ECR_Block) return false;
	if (UCatModelContactComponent::IsLegacyContactProxy(Target)) return false;
	if (Target->IsA<UCatModelContactBody>()) return Target->IsQueryCollisionEnabled();
	if (const auto* Light = UCatLightPropComponent::FindFor(Target); Light && Light->GetState().Mode == ECatLightPropMode::Parked) return false;
	const auto Collision = Target->GetCollisionEnabled();
	if (Collision == ECollisionEnabled::QueryAndPhysics || Collision == ECollisionEnabled::PhysicsOnly) return true;
	if (Collision != ECollisionEnabled::QueryOnly) return false;
	// Remote body proxies and a CMC capsule are real contact surfaces despite being query-only.
	// Do not grant the exception to unrelated interaction components on the same character.
	const auto* Physical = Target->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	if (!Physical) return false;
	const auto* Character = Cast<ACharacter>(Target->GetOwner());
	return Target == Physical->GetBody() || Target == Physical->GetHand(true) || Target == Physical->GetHand(false)
		|| (Physical->UsesCharacterMovement() && Character && Target == Character->GetCapsuleComponent());
}

bool UCatPhysicsGrabComponent::TraceReachSurface(const bool bLeft, const FVector& Start, const FVector& End, FHitResult& Hit)
{
	const int32 Index = bLeft ? 0 : 1;
	if (!Hands.IsValidIndex(Index) || !Hands[Index] || !GetWorld()) return false;
	TArray<FHitResult> Hits;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsGrabReach), false, GetOwner());
	// Object sweeps return surfaces behind a blocking query volume, too. Filtering only a
	// Visibility single-hit result would still let that volume hide the real contact.
	GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
		FCollisionShape::MakeSphere(Hands[Index]->GetScaledSphereRadius()), Params);
	bool bFound = false;
	int32 IgnoredVolumes = 0;
	for (const FHitResult& Candidate : Hits)
	{
		if (!IsReachSurface(Candidate.GetComponent(), Candidate.BoneName, bLeft)) { ++IgnoredVolumes; continue; }
		if (!bFound || Candidate.Time < Hit.Time) { Hit = Candidate; bFound = true; }
	}
	UPrimitiveComponent* Surface = bFound ? Hit.GetComponent() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextReachLogSeconds[Index] && (ObservedIgnoredReachVolumes[Index] != IgnoredVolumes || ObservedReachSurface[Index].Get() != Surface))
	{
		NextReachLogSeconds[Index] = Now + 0.25;
		ObservedIgnoredReachVolumes[Index] = IgnoredVolumes;
		ObservedReachSurface[Index] = Surface;
		const auto* Physical = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
		UE_LOG(LogCatPhysicsGrab, Log,
			TEXT("Event=physics_reach_surface World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s Hand=%s GripId=%s SurfaceActor=%s Component=%s IgnoredVolumes=%d HandChannel=%d Result=%s"),
			*GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()),
			*GetNameSafe(GetOwner()), Physical ? *Physical->GetBodyId().ToString() : TEXT("None"), bLeft ? TEXT("Left") : TEXT("Right"),
			*GetGripState(bLeft).GripId.ToString(), *GetNameSafe(Surface ? Surface->GetOwner() : nullptr), *GetNameSafe(Surface),
			IgnoredVolumes, int32(Hands[Index]->GetCollisionObjectType()), bFound ? TEXT("SolidSurface") : TEXT("Clear"));
	}
	return bFound;
}

void UCatPhysicsGrabComponent::UpdateHand(const bool bLeft, const FVector& Aim)
{
	const int32 Index = bLeft ? 0 : 1;
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	USphereComponent* Hand = Hands[Index];
	const FVector Shoulder = GetShoulderWorldLocation(bLeft);
	if (State.bGripped)
	{
		RefreshContact(bLeft);
		UPrimitiveComponent* Target = ResolveTarget(State);
		const bool bOwnCarrier = State.bControlledHold || ResolveConstraintTarget(State) == Body;
		if (!Target || !Target->IsRegistered() || !Target->IsCollisionEnabled())
			ReleaseHand(bLeft, TEXT("TargetUnavailable"), false);
		else if (!UsesCharacterMovement() && !bOwnCarrier && Contacts[Index]->IsBroken())
			ReleaseHand(bLeft, TEXT("ForceLimit"), false);
		else if (!bOwnCarrier && FVector::DistSquared(Shoulder, GetGripWorldLocation(bLeft)) > FMath::Square(GetReachLengthCm() + 14.0 * GeometryScale))
		{
			FVector ConstraintForce, ConstraintTorque;
			Contacts[Index]->GetConstraintForce(ConstraintForce, ConstraintTorque);
			UE_LOG(LogCatPhysicsGrab, Log,
				TEXT("Event=physics_grip_reach_limit World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s Hand=%s GripId=%s Target=%s Shoulder=%s HandPosition=%s Contact=%s ActualReachCm=%.3f MaximumReachCm=%.3f HeldReachCm=%.3f HandVelocity=%s ConstraintForceN=%s Result=Released"),
				*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
				bLeft ? TEXT("Left") : TEXT("Right"), *State.GripId.ToString(), *GetNameSafe(Target), *Shoulder.ToCompactString(),
				*Hand->GetComponentLocation().ToCompactString(), *GetGripWorldLocation(bLeft).ToCompactString(),
				FVector::Distance(Shoulder, GetGripWorldLocation(bLeft)), GetReachLengthCm() + 14.0 * GeometryScale, State.HeldReachDistanceCm,
				*Hand->GetPhysicsLinearVelocity().ToCompactString(), *(ConstraintForce / 100.0).ToCompactString());
			ReleaseHand(bLeft, TEXT("ReachLimit"), false);
		}
	}
	FVector Desired = Body->GetComponentTransform().TransformPosition(RestHandLocal(bLeft) * GeometryScale);
	FHitResult Candidate;
	bool bHit = false;
	if (State.bReaching)
	{
		// Keep the contact's existing arm length. Extending to full reach on the first gripped
		// frame would push through a nearby target even though the player's aim has not changed.
		const double DriveDistanceCm = State.bGripped ? State.HeldReachDistanceCm : GetReachLengthCm();
		Desired = Shoulder + Aim.GetSafeNormal() * DriveDistanceCm;
		if (!State.bGripped && !bLatchedUntilRelease[Index])
		{
			bHit = TraceReachSurface(bLeft, Shoulder, Desired, Candidate);
			if (bHit) Desired = Candidate.Location;
		}
	}
	if (State.bGripped && (State.bControlledHold || ResolveConstraintTarget(State) == Body))
	{
		// Controlled holding poses the hand without creating a self-constraining physical loop.
		Desired = GetGripWorldLocation(bLeft);
	}
	if (UsesCharacterMovement())
	{
		if (State.bGripped) Desired = GetGripWorldLocation(bLeft);
		Hand->SetWorldLocation(Desired, false, nullptr, ETeleportType::TeleportPhysics);
		ApplyTraction(bLeft);
	}
	else
	{
	// The shoulder drive acts on BOTH connected rigid bodies. It is never a kinematic teleport of the hand.
	const FVector LocalTarget = Body->GetComponentTransform().InverseTransformPosition(Desired) - ShoulderLocal(bLeft) * GeometryScale;
	Arms[Index]->SetLinearPositionTarget(LocalTarget.GetClampedToMaxSize(ReachLengthCm * GeometryScale));
	Arms[Index]->SetLinearDriveParams(State.bReaching ? 650.0f : 140.0f, State.bReaching ? 24.0f : 10.0f,
		State.bReaching ? 10000.0f : 1200.0f);
	}
	if (bHit && !State.bGripped && !bLatchedUntilRelease[Index] && Candidate.GetComponent())
	{
		FVector ContactPoint;
		const float SurfaceDistance = Candidate.GetComponent()->GetClosestPointOnCollision(
			Hand->GetComponentLocation(), ContactPoint, Candidate.BoneName);
		// Same actual-contact tolerance as explicit grips. A yielding prop need not reach the
		// predicted sweep centre before it can be gripped by the already touching hand sphere.
		if (SurfaceDistance >= 0 && SurfaceDistance <= Hand->GetScaledSphereRadius() + 1.0)
		{
			Candidate.ImpactPoint = ContactPoint;
			TryLatch(bLeft, Candidate);
		}
	}
}

void UCatPhysicsGrabComponent::TryLatch(const bool bLeft, const FHitResult& Hit)
{
	UPrimitiveComponent* Target = Hit.GetComponent();
	if (!IsReachSurface(Target, Hit.BoneName, bLeft)) return;
	const UCatPhysicalBodyComponent* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	if (PhysicalBody && !PhysicalBody->IsLocomotionEnabled()) return;
	const int32 Index = bLeft ? 0 : 1;
	if (!Contacts.IsValidIndex(Index)) return;
	const FCatPhysicsGripState Previous=GetGripState(bLeft);
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	State.bGripped = true;
	State.GripId = FGuid::NewGuid();
	State.TargetActor = Hit.GetActor();
	State.TargetComponentName = Target->GetFName();
	State.TargetBone = Hit.BoneName;
	const FTransform Frame = Hit.BoneName.IsNone() ? Target->GetComponentTransform()
		: Target->GetSocketTransform(Hit.BoneName, RTS_World);
	State.TargetLocalPoint = Frame.InverseTransformPosition(Hit.ImpactPoint);
	State.HeldReachDistanceCm = FMath::Clamp(FVector::Distance(GetShoulderWorldLocation(bLeft), Hit.Location),
		0.0, GetReachLengthCm());
	State.HeldAimLocalOffset = PhysicalBody ? PhysicalBody->GetViewIntent().UnrotateVector(Hit.ImpactPoint - GetShoulderWorldLocation(bLeft)) : FVector::ZeroVector;
	RefreshContact(bLeft, true);
	++State.Revision;
	bLatchedUntilRelease[Index] = true;
	LogGrip(bLeft, TEXT("physics_grip_created"), UsesCharacterMovement() ? TEXT("BidirectionalTraction") : TEXT("Constrained"));
	if (auto* LightProp = UCatLightPropComponent::FindFor(Target)) LightProp->RefreshGripsFromAuthority(TEXT("GripCreated"));
	OnGripChanged.Broadcast(this,bLeft,Previous,State);
	GetOwner()->ForceNetUpdate();
}

void UCatPhysicsGrabComponent::ReleaseHand(const bool bLeft, const FName Reason, const bool bStopReaching)
{
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	const int32 Index = bLeft ? 0 : 1;
	const FCatPhysicsGripState Previous=State;
	ClearTraction(bLeft);
	const bool bChanged = State.bGripped || (bStopReaching && State.bReaching);
	TWeakObjectPtr<UCatLightPropComponent> PreviousLightProp = UCatLightPropComponent::FindFor(ResolveTarget(State));
	if (Contacts.IsValidIndex(Index) && Contacts[Index]) Contacts[Index]->BreakConstraint();
	State.bGripped = false;
	State.bExplicitHold = false;
	State.bControlledHold = false;
	State.HeldReachDistanceCm = 0.0;
	State.HeldAimLocalOffset = FVector::ZeroVector;
	// An explicit hold replaced the button-held source. Breaking it has no remaining
	// held-button request to keep reaching or to recreate a new contact.
	if (bStopReaching || Previous.bExplicitHold) State.bReaching = false;
	if (Previous.bExplicitHold) bLatchedUntilRelease[Index] = false;
	if (!bChanged) return;
	++State.Revision;
	// Keep the final GripId in the release snapshot so both ends can correlate its lifetime.
	LogGrip(bLeft, TEXT("physics_grip_released"), Reason);
	State.TargetActor = nullptr;
	State.TargetComponentName = NAME_None;
	State.TargetBone = NAME_None;
	State.TargetLocalPoint = FVector::ZeroVector;
	if (PreviousLightProp.IsValid()) PreviousLightProp->RefreshGripsFromAuthority(Reason);
	OnGripChanged.Broadcast(this,bLeft,Previous,State);
	GetOwner()->ForceNetUpdate();
}

void UCatPhysicsGrabComponent::ReleaseAllFromAuthority(const FName Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ReleaseHand(true, Reason, true);
	ReleaseHand(false, Reason, true);
	bLatchedUntilRelease[0] = bLatchedUntilRelease[1] = false;
}

void UCatPhysicsGrabComponent::ReleaseTargetFromAuthority(AActor* Target, const FName Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Target) return;
	if (LeftGrip.TargetActor == Target) ReleaseHand(true, Reason, true);
	if (RightGrip.TargetActor == Target) ReleaseHand(false, Reason, true);
}

void UCatPhysicsGrabComponent::BeginInputEpochFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ReleaseAllFromAuthority(TEXT("ControlChanged"));
	++InputEpoch;
	if (InputEpoch == 0) InputEpoch = 1;
	AcceptedSequence[0] = AcceptedSequence[1] = 0;
	GetOwner()->ForceNetUpdate();
}

void UCatPhysicsGrabComponent::LogGrip(const bool bLeft, const FName Event, const FName Result) const
{
	const FCatPhysicsGripState& State = GetGripState(bLeft);
	const auto* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const FString Record = FString::Printf(
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s Hand=%s GripId=%s Revision=%u Target=%s Component=%s Reaching=%d Gripped=%d ExplicitHold=%d ControlledHold=%d HeldReachCm=%.3f Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		GetOwner()->HasAuthority(), static_cast<int32>(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
		PhysicalBody ? *PhysicalBody->GetBodyId().ToString() : TEXT("None"), bLeft ? TEXT("Left") : TEXT("Right"),
		*State.GripId.ToString(), State.Revision, *GetNameSafe(State.TargetActor), *State.TargetComponentName.ToString(),
		State.bReaching, State.bGripped, State.bExplicitHold, State.bControlledHold, State.HeldReachDistanceCm, *Result.ToString());
	if (Event.ToString().EndsWith(TEXT("_rejected")))
	{
		UE_LOG(LogCatPhysicsGrab, Warning, TEXT("%s"), *Record);
	}
	else
	{
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("%s"), *Record);
	}
}

void UCatPhysicsGrabComponent::OnRep_GripState()
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FCatPhysicsGripState& State = GetGripState(Index == 0);
		if (State.Revision == ObservedRevision[Index]) continue;
		ObservedRevision[Index] = State.Revision;
		LogGrip(Index == 0, TEXT("physics_grip_observed"), State.bGripped ? TEXT("Gripped") : TEXT("ReleasedOrReaching"));
	}
}

void UCatPhysicsGrabComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAllFromAuthority(TEXT("EndPlay"));
	for (UPhysicsConstraintComponent* Contact : Contacts)
		if (IsValid(Contact)) Contact->DestroyComponent();
	Contacts.Reset();
	Super::EndPlay(EndPlayReason);
}

UPrimitiveComponent* UCatPhysicsGrabComponent::GetGripTargetComponent(bool bLeft) const
{
	return IsGripping(bLeft) ? ResolveTarget(GetGripState(bLeft)) : nullptr;
}

UPrimitiveComponent* UCatPhysicsGrabComponent::ResolveConstraintTarget(const FCatPhysicsGripState& State) const
{
	auto* Target = ResolveTarget(State);
	if (auto* Light = UCatLightPropComponent::FindFor(Target))
		if (auto* Carrier = Light->GetGripCarrier()) return Carrier;
	return Target;
}

void UCatPhysicsGrabComponent::RefreshContact(const bool bLeft, const bool bForceRebind)
{
	const int32 Index = bLeft ? 0 : 1;
	const auto& State = GetGripState(bLeft);
	if (!State.bGripped || !Contacts.IsValidIndex(Index)) return;
	auto* Target = ResolveConstraintTarget(State);
	if (!IsValid(Target)) return;
	UPhysicsConstraintComponent* Contact = Contacts[Index];
	if (UsesCharacterMovement() || State.bControlledHold || Target == Body)
	{
		if (!Contact->IsBroken()) Contact->BreakConstraint();
		return;
	}
	UPrimitiveComponent *A = nullptr, *B = nullptr; FName BoneA, BoneB;
	Contact->GetConstrainedComponents(A, BoneA, B, BoneB);
	const FName TargetBone = Target == ResolveTarget(State) ? State.TargetBone : NAME_None;
	const bool bRebind = bForceRebind || B != Target || BoneB != TargetBone;
	const FVector Point = GetGripWorldLocation(bLeft);
	if (bRebind)
	{
		// Preserve the hand-local contact when changing carrier, even if the joint has small solver error.
		const FTransform HandFrame = Contact->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1);
		const float PreviousScale = Contact->ConstraintInstance.GetLastKnownScale();
		Contact->BreakConstraint();
		Contact->SetWorldLocationAndRotation(Point, FRotator::ZeroRotator);
		Contact->SetLinearBreakable(true, 60000.0f);
		Contact->SetConstrainedComponents(Hands[Index], NAME_None, Target, TargetBone);
		if (!bForceRebind && A == Hands[Index])
			Contact->SetConstraintReferencePosition(EConstraintFrame::Frame1,
				HandFrame.GetLocation() * (PreviousScale / FMath::Max(.01f, Contact->ConstraintInstance.GetLastKnownScale())));
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_grip_receiver_bound World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s Hand=%s GripId=%s Target=%s ReceiverActor=%s ReceiverComponent=%s Result=Bidirectional"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
			bLeft ? TEXT("Left") : TEXT("Right"), *State.GripId.ToString(), *GetNameSafe(State.TargetActor), *GetNameSafe(Target->GetOwner()), *GetNameSafe(Target));
	}
	// A controlled shaft can rotate about its holder. Keep the same chosen point on that shaft.
	if (Target != ResolveTarget(State) && !Contact->IsBroken())
	{
		FTransform Pose = Target->GetComponentTransform(); Pose.RemoveScaling();
		Contact->SetConstraintReferencePosition(EConstraintFrame::Frame2,
			Pose.InverseTransformPosition(Point) / FMath::Max(.01f, Contact->ConstraintInstance.GetLastKnownScale()));
	}
}

void UCatPhysicsGrabComponent::RefreshTargetConstraintsFromAuthority(UPrimitiveComponent* Target)
{
	if (!GetOwner()->HasAuthority()) return;
	for (const bool bLeft : {true, false})
		if (GetGripTargetComponent(bLeft) == Target) RefreshContact(bLeft);
}

bool UCatPhysicsGrabComponent::ControlRetainedGripFromAuthority(const bool bLeft, UPrimitiveComponent* ExpectedTarget)
{
	auto& State = bLeft ? LeftGrip : RightGrip;
	if (!GetOwner()->HasAuthority() || !State.bGripped || !State.bExplicitHold
		|| !IsValid(ExpectedTarget) || ResolveTarget(State) != ExpectedTarget) return false;
	if (State.bControlledHold) return true;
	const auto Previous = State;
	State.bControlledHold = true;
	RefreshContact(bLeft);
	++State.Revision;
	LogGrip(bLeft, TEXT("physics_grip_controlled_hold"), TEXT("PrimaryPoseOwned"));
	OnGripChanged.Broadcast(this, bLeft, Previous, State);
	GetOwner()->ForceNetUpdate();
	return true;
}
bool UCatPhysicsGrabComponent::RetainGripFromAuthority(const bool bLeft, UPrimitiveComponent* ExpectedTarget)
{
	const int32 Index = bLeft ? 0 : 1;
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	const UCatPhysicalBodyComponent* PhysicalBody = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	if (!IsValid(ExpectedTarget) || !ExpectedTarget->IsRegistered() || !ExpectedTarget->IsCollisionEnabled()
		|| !State.bGripped || ResolveTarget(State) != ExpectedTarget || !Contacts.IsValidIndex(Index)
		|| !Contacts[Index] || (!UsesCharacterMovement() && !State.bControlledHold && Contacts[Index]->IsBroken()) || !PhysicalBody || !PhysicalBody->IsLocomotionEnabled())
	{
		LogGrip(bLeft, TEXT("physics_grip_rejected"), TEXT("ExplicitHoldTargetUnavailable"));
		return false;
	}
	if (State.bExplicitHold) return true;
	const FCatPhysicsGripState Previous = State;
	State.bExplicitHold = true;
	++State.Revision;
	LogGrip(bLeft, TEXT("physics_grip_source_changed"), TEXT("ExplicitHoldRetained"));
	OnGripChanged.Broadcast(this, bLeft, Previous, State);
	GetOwner()->ForceNetUpdate();
	return true;
}
void UCatPhysicsGrabComponent::ReleaseHandFromAuthority(bool bLeft,FName Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ReleaseHand(bLeft,Reason,true); bLatchedUntilRelease[bLeft ? 0 : 1]=false;
}
bool UCatPhysicsGrabComponent::GripFromAuthority(bool bLeft,UPrimitiveComponent* Target,const FVector& WorldPoint)
{
	const int32 Index=bLeft ? 0 : 1;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Target) || WorldPoint.ContainsNaN()
		|| !Hands.IsValidIndex(Index) || !Hands[Index] || IsGripping(bLeft)
		|| FVector::DistSquared(Hands[Index]->GetComponentLocation(),WorldPoint)>FMath::Square(Hands[Index]->GetScaledSphereRadius()+1.0)) return false;
	FVector ClosestPoint;
	const float SurfaceDistance = Target->GetClosestPointOnCollision(WorldPoint, ClosestPoint);
	if (SurfaceDistance < 0.0f || SurfaceDistance > 1.0f)
	{
		LogGrip(bLeft, TEXT("physics_grip_rejected"), TEXT("PointAwayFromTarget"));
		return false;
	}
	FHitResult Hit(Target->GetOwner(),Target,WorldPoint,FVector::UpVector);
	Hit.ImpactPoint=WorldPoint; Hit.Location=Hands[Index]->GetComponentLocation();
	ApplyGrabInput(bLeft,true);
	if (!IsReaching(bLeft)) return false;
	TryLatch(bLeft,Hit);
	return IsGripping(bLeft);
}

bool UCatPhysicsGrabComponent::UsesCharacterMovement() const
{
	const auto* Physical = GetOwner() ? GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
	return Physical && Physical->UsesCharacterMovement();
}

void UCatPhysicsGrabComponent::ClearTraction(bool bLeft)
{
	const int32 Index = bLeft ? 0 : 1;
	if (!Contacts.IsValidIndex(Index)) return;
	if (auto* Physical = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>()) Physical->ClearExternalForce(Contacts[Index]);
	if (TractionReceiver[Index].IsValid()) TractionReceiver[Index]->ClearExternalForce(Contacts[Index]);
	TractionReceiver[Index].Reset();
	LastTractionForce[Index] = FVector::ZeroVector;
}

void UCatPhysicsGrabComponent::ApplyTraction(bool bLeft)
{
	if (!GetOwner()->HasAuthority()) return;
	ClearTraction(bLeft);
	const int32 Index = bLeft ? 0 : 1;
	const auto& State = GetGripState(bLeft);
	auto* Physical = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	auto* Target = ResolveConstraintTarget(State);
	if (!State.bGripped || State.bControlledHold || !Physical || !IsValid(Target) || Target == Body) return;
	auto* Receiver = Target->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const FVector Point = GetGripWorldLocation(bLeft);
	const FVector Desired = GetShoulderWorldLocation(bLeft) + Physical->GetViewIntent().RotateVector(State.HeldAimLocalOffset);
	const FVector TargetVelocity = Receiver ? Receiver->GetVelocity() : Target->GetPhysicsLinearVelocityAtPoint(Point);
	// Force on target; the holder receives the equal and opposite force. No fishing membership or stamina sum.
	FVector Force = ((Desired - Point) * 650.0 + (Physical->GetVelocity() - TargetVelocity) * 24.0).GetClampedToMaxSize(10000.0);
    const bool bCharacterPair = Physical->UsesCharacterMovement() && Receiver && Receiver->UsesCharacterMovement();
    const double JumpWeight = bCharacterPair ? FMath::Max(Physical->GetJumpTractionWeight(),Receiver->GetJumpTractionWeight()) : 0;
    if (bCharacterPair)
    {
        // An implicit spring/damper bounds the average force over a slow frame. Every grip
        // on this pair sees their combined mass response; two hands cannot double an old
        // relative velocity impulse and fling the grounded endpoint during a hitch.
        int32 PairGripCount = 0;
        for (const auto* Component : {this,Receiver->GetGrab()})
            if (Component) for (const bool Left : {true,false})
            {
                const auto& PairState=Component->GetGripState(Left);
                const auto* PairTarget=Component->ResolveConstraintTarget(PairState);
                if (PairState.bGripped && !PairState.bControlledHold && PairTarget
                    && PairTarget->GetOwner()==(Component==this ? Receiver->GetOwner() : GetOwner())) ++PairGripCount;
            }
        const double InverseMass = 1.0/FMath::Max(1.0f,CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->Mass)
            + 1.0/FMath::Max(1.0f,CastChecked<ACharacter>(Receiver->GetOwner())->GetCharacterMovement()->Mass);
        const double Dt = FMath::Max(0.0f,GetWorld()->GetDeltaSeconds());
        const double Spring=650*JumpWeight, Damping=24*JumpWeight;
        const double RelativeSpeed=Physical->GetVelocity().Z-TargetVelocity.Z;
        Force.Z=(Spring*(Desired.Z-Point.Z+RelativeSpeed*Dt)+Damping*RelativeSpeed)
            /(1+(Damping*Dt+Spring*Dt*Dt)*InverseMass*FMath::Max(1,PairGripCount));
        Force=Force.GetClampedToMaxSize(10000.0);
    }
    const bool bVerticalTraction = JumpWeight > 0;
	if (GetWorld()->GetTimeSeconds() >= NextTractionLogSeconds[Index])
	{
		NextTractionLogSeconds[Index] = GetWorld()->GetTimeSeconds() + 1.0;
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_grip_traction World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s Hand=%s GripId=%s Target=%s Receiver=%s ForceOnTargetN=%s ErrorCm=%s JumpTractionWeight=%.3f Result=ReciprocalGripForce"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
			*Physical->GetBodyId().ToString(), bLeft ? TEXT("Left") : TEXT("Right"), *State.GripId.ToString(), *GetNameSafe(State.TargetActor),
			*GetNameSafe(Target->GetOwner()), *(Force / 100.0).ToCompactString(), *(Desired-Point).ToCompactString(), JumpWeight);
	}
	LastTractionForce[Index] = Force;
	Physical->SetExternalForceFromAuthority(Contacts[Index], -Force, bVerticalTraction);
	if (Receiver)
	{
		Receiver->SetExternalForceFromAuthority(Contacts[Index], Force, bVerticalTraction);
		TractionReceiver[Index] = Receiver;
	}
	else if (Target->IsSimulatingPhysics(State.TargetBone)) Target->AddForceAtLocation(Force, Point, State.TargetBone);
}

void UCatPhysicsGrabComponent::RefreshKinematicHands()
{
	if (!UsesCharacterMovement() || !Body || Hands.Num() != 2) return;
	const auto* Physical = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	for (const bool bLeft : {true,false})
	{
		const auto& State = GetGripState(bLeft);
		FVector Point = Body->GetComponentTransform().TransformPosition(RestHandLocal(bLeft) * GeometryScale);
		if (State.bGripped) Point = GetGripWorldLocation(bLeft);
		else if (State.bReaching)
		{
			const FVector Shoulder = GetShoulderWorldLocation(bLeft);
			Point = Shoulder + Physical->GetViewIntent().Vector() * GetReachLengthCm();
			FHitResult Hit;
			if (TraceReachSurface(bLeft, Shoulder, Point, Hit)) Point = Hit.Location;
		}
		Hands[bLeft?0:1]->SetWorldLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FVector UCatPhysicsGrabComponent::GetTractionErrorForDiagnostics(bool bLeft) const
{
	const auto* Physical = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	return Physical && IsGripping(bLeft) ? GetShoulderWorldLocation(bLeft)
		+ Physical->GetViewIntent().RotateVector(GetGripState(bLeft).HeldAimLocalOffset) - GetGripWorldLocation(bLeft) : FVector::ZeroVector;
}
