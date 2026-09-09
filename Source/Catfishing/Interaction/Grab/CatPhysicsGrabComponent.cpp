#include "Interaction/Grab/CatPhysicsGrabComponent.h"

#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
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
	USphereComponent* InRight, UPhysicsConstraintComponent* InLeftArm, UPhysicsConstraintComponent* InRightArm)
{
	Body = InBody;
	Hands = {InLeft, InRight};
	Arms = {InLeftArm, InRightArm};
	if (!GetOwner()->HasAuthority()) return;
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
		ReleaseHand(bLeft, TEXT("InputReleased"), true);
		bLatchedUntilRelease[bLeft ? 0 : 1] = false;
		return;
	}
	if (State.bReaching) return;
	State.bReaching = true;
	++State.Revision;
	LogGrip(bLeft, TEXT("physics_grip_reach"), TEXT("Accepted"));
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
	const auto* Pawn = Cast<ACatPhysicsPrototypePawn>(GetOwner());
	const FVector Aim = Pawn ? Pawn->GetPrototypeView().Vector() : GetOwner()->GetActorForwardVector();
	UpdateHand(true, Aim);
	UpdateHand(false, Aim);
}

void UCatPhysicsGrabComponent::UpdateHand(const bool bLeft, const FVector& Aim)
{
	const int32 Index = bLeft ? 0 : 1;
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	USphereComponent* Hand = Hands[Index];
	const FVector Shoulder = Body->GetComponentTransform().TransformPosition(ShoulderLocal(bLeft));
	if (State.bGripped)
	{
		UPrimitiveComponent* Target = ResolveTarget(State);
		if (!Target || !Target->IsRegistered() || !Target->IsCollisionEnabled())
			ReleaseHand(bLeft, TEXT("TargetUnavailable"), false);
		else if (Contacts[Index]->IsBroken())
			ReleaseHand(bLeft, TEXT("ForceLimit"), false);
		else if (FVector::DistSquared(Shoulder, GetGripWorldLocation(bLeft)) > FMath::Square(ReachLengthCm + 14.0))
			ReleaseHand(bLeft, TEXT("ReachLimit"), false);
	}
	FVector Desired = Body->GetComponentTransform().TransformPosition(RestHandLocal(bLeft));
	FHitResult Candidate;
	bool bHit = false;
	if (State.bReaching)
	{
		Desired = Shoulder + Aim.GetSafeNormal() * ReachLengthCm;
		if (!State.bGripped && !bLatchedUntilRelease[Index])
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CatPhysicsGrabReach), false, GetOwner());
			bHit = GetWorld()->SweepSingleByChannel(Candidate, Shoulder, Desired, FQuat::Identity,
				ECC_Visibility, FCollisionShape::MakeSphere(HandRadiusCm), Params);
			if (bHit) Desired = Candidate.Location;
		}
	}
	// The shoulder drive acts on BOTH connected rigid bodies. It is never a kinematic teleport of the hand.
	const FVector LocalTarget = Body->GetComponentTransform().InverseTransformPosition(Desired) - ShoulderLocal(bLeft);
	Arms[Index]->SetLinearPositionTarget(LocalTarget.GetClampedToMaxSize(ReachLengthCm));
	Arms[Index]->SetLinearDriveParams(State.bReaching ? 650.0f : 140.0f, State.bReaching ? 24.0f : 10.0f,
		State.bReaching ? 10000.0f : 1200.0f);
	if (bHit && !State.bGripped && !bLatchedUntilRelease[Index]
		&& FVector::DistSquared(Hand->GetComponentLocation(), Candidate.Location) <= FMath::Square(1.0))
	{
		TryLatch(bLeft, Candidate);
	}
}

void UCatPhysicsGrabComponent::TryLatch(const bool bLeft, const FHitResult& Hit)
{
	UPrimitiveComponent* Target = Hit.GetComponent();
	if (!IsValid(Target) || !IsValid(Hit.GetActor()) || Hit.GetActor() == GetOwner()
		|| !Target->GetBodyInstance(Hit.BoneName)) return;
	const ECollisionEnabled::Type Collision = Target->GetCollisionEnabled();
	if (Collision != ECollisionEnabled::QueryAndPhysics && Collision != ECollisionEnabled::PhysicsOnly) return;
	const int32 Index = bLeft ? 0 : 1;
	UPhysicsConstraintComponent* Contact = Contacts[Index];
	Contact->BreakConstraint();
	Contact->SetWorldLocation(Hit.ImpactPoint);
	Contact->SetWorldRotation(FRotator::ZeroRotator);
	Contact->SetConstrainedComponents(Hands[Index], NAME_None, Target, Hit.BoneName);
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	State.bGripped = true;
	State.GripId = FGuid::NewGuid();
	State.TargetActor = Hit.GetActor();
	State.TargetComponentName = Target->GetFName();
	State.TargetBone = Hit.BoneName;
	const FTransform Frame = Hit.BoneName.IsNone() ? Target->GetComponentTransform()
		: Target->GetSocketTransform(Hit.BoneName, RTS_World);
	State.TargetLocalPoint = Frame.InverseTransformPosition(Hit.ImpactPoint);
	++State.Revision;
	bLatchedUntilRelease[Index] = true;
	LogGrip(bLeft, TEXT("physics_grip_created"), TEXT("Constrained"));
	GetOwner()->ForceNetUpdate();
}

void UCatPhysicsGrabComponent::ReleaseHand(const bool bLeft, const FName Reason, const bool bStopReaching)
{
	FCatPhysicsGripState& State = bLeft ? LeftGrip : RightGrip;
	const int32 Index = bLeft ? 0 : 1;
	const bool bChanged = State.bGripped || (bStopReaching && State.bReaching);
	if (Contacts.IsValidIndex(Index) && Contacts[Index]) Contacts[Index]->BreakConstraint();
	State.bGripped = false;
	if (bStopReaching) State.bReaching = false;
	if (!bChanged) return;
	++State.Revision;
	// Keep the final GripId in the release snapshot so both ends can correlate its lifetime.
	LogGrip(bLeft, TEXT("physics_grip_released"), Reason);
	State.TargetActor = nullptr;
	State.TargetComponentName = NAME_None;
	State.TargetBone = NAME_None;
	State.TargetLocalPoint = FVector::ZeroVector;
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
	const auto* Pawn = Cast<ACatPhysicsPrototypePawn>(GetOwner());
	UE_LOG(LogCatPhysicsGrab, Log,
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s Hand=%s GripId=%s Revision=%u Target=%s Component=%s Reaching=%d Gripped=%d Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		GetOwner()->HasAuthority(), static_cast<int32>(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
		Pawn ? *Pawn->GetPrototypeId().ToString() : TEXT("None"), bLeft ? TEXT("Left") : TEXT("Right"),
		*State.GripId.ToString(), State.Revision, *GetNameSafe(State.TargetActor), *State.TargetComponentName.ToString(),
		State.bReaching, State.bGripped, *Result.ToString());
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
