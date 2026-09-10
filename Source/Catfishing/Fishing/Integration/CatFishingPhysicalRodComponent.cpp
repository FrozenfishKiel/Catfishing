#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Logging/CatLog.h"



UCatFishingPhysicalRodComponent::UCatFishingPhysicalRodComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCatFishingPhysicalRodComponent::Initialize(UBoxComponent* InBody, const FTransform& GripLocal, const FTransform& TipLocal)
{
	if (bReady || !InBody || !GetOwner()) return;
	Body = InBody;
	GripLocalTransform = GripLocal;
	const FVector Shaft = TipLocal.GetLocation() - GripLocal.GetLocation();
	BodyLocal = FTransform(FQuat::FindBetweenNormals(FVector::ForwardVector, Shaft.GetSafeNormal()),
		(GripLocal.GetLocation() + TipLocal.GetLocation()) * 0.5);
	Body->SetRelativeTransform(BodyLocal);
	Body->SetBoxExtent(FVector(FMath::Max(3.0, Shaft.Size() * 0.5), 0.8, 0.8));
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetGenerateOverlapEvents(false);
	Body->SetLinearDamping(0.15);
	Body->SetAngularDamping(0.8);
	Body->SetMassOverrideInKg(NAME_None, 0.35, true);
	Body->BodyInstance.bUseCCD = true;
	if (GetOwner()->HasAuthority()) Body->SetSimulatePhysics(true);
	if (auto* LightProp = GetOwner()->FindComponentByClass<UCatLightPropComponent>())
	{
		LightProp->Initialize(Body);
		LightProp->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
	bReady = true;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_physical_rod_ready RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d MassKg=0.35 ShaftCm=%.3f Result=PhysicalReceiver"),
		*CastChecked<ACatFishingRodActor>(GetOwner())->GetPresentationState().RodActorId.ToString(),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), Shaft.Size());
}

FTransform UCatFishingPhysicalRodComponent::GetObservedActorTransform() const
{
	return Body ? BodyLocal.Inverse() * Body->GetComponentTransform() : GetOwner()->GetActorTransform();
}

FVector UCatFishingPhysicalRodComponent::GetPointVelocity(const FVector& WorldPoint) const
{
	if (ControlledBody.IsValid())
	{
		const auto* Carrier = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
		return (Carrier ? Carrier->GetVelocity() : ControlledBody->GetPhysicsLinearVelocity())
			+ FVector::CrossProduct(ControlledAngularVelocity, WorldPoint - ControlledBody->GetComponentLocation());
	}
	return Body && Body->IsSimulatingPhysics() ? Body->GetPhysicsLinearVelocityAtPoint(WorldPoint) : FVector::ZeroVector;
}

FVector UCatFishingPhysicalRodComponent::GetAngularVelocityRadiansPerSecond() const
{
	return ControlledBody.IsValid() ? ControlledAngularVelocity : Body ? Body->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
}

void UCatFishingPhysicalRodComponent::PopulateEndpointResponse(FCatFightRodConstraintInput& OutInput)
{
	if (!bReady || !GetOwner()->HasAuthority() || !Body) return;
	const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	const FVector Tip = Rod->GetRodTipWorldTransform().GetLocation();
	const FVector Velocity = GetPointVelocity(Tip);
	const auto* Carrier = ControlledBody.IsValid() ? ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
	const bool bCMC = Carrier && Carrier->UsesCharacterMovement();
	if (bCMC) { PopulateCMCEndpointPrediction(OutInput); return; }
	struct FLockedEdge
	{
		UPhysicsConstraintComponent* Constraint;
		UPrimitiveComponent* ComponentA;
		UPrimitiveComponent* ComponentB;
		FBodyInstance* A;
		FBodyInstance* B;
	};
	TArray<FLockedEdge> Edges;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TInlineComponentArray<UPhysicsConstraintComponent*> Constraints(*It);
		for (auto* Constraint : Constraints)
		{
			if (!Constraint || Constraint->IsBroken()) continue;
			const FConstraintInstance& Joint = Constraint->ConstraintInstance;
			if (Joint.GetLinearXMotion() != LCM_Locked && Joint.GetLinearYMotion() != LCM_Locked
				&& Joint.GetLinearZMotion() != LCM_Locked) continue;
			UPrimitiveComponent* A = nullptr; UPrimitiveComponent* B = nullptr; FName BoneA, BoneB;
			Constraint->GetConstrainedComponents(A, BoneA, B, BoneB);
			FBodyInstance* InstanceA = A ? A->GetBodyInstance(BoneA) : nullptr;
			FBodyInstance* InstanceB = B ? B->GetBodyInstance(BoneB) : nullptr;
			if (InstanceA || InstanceB) Edges.Add({Constraint, A, B, InstanceA, InstanceB});
		}
	}
	FBodyInstance* RootInstance = ControlledBody.IsValid() ? ControlledBody->GetBodyInstance() : Body->GetBodyInstance();
	if (!RootInstance || (!RootInstance->IsInstanceSimulatingPhysics() && !bCMC)) return;
	TSet<FBodyInstance*> Connected; Connected.Add(RootInstance);
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (const auto& Edge : Edges)
		{
			if (Edge.A && Connected.Contains(Edge.A) && Edge.A->IsInstanceSimulatingPhysics() && Edge.B && !Connected.Contains(Edge.B))
			{ Connected.Add(Edge.B); bChanged = true; }
			if (Edge.B && Connected.Contains(Edge.B) && Edge.B->IsInstanceSimulatingPhysics() && Edge.A && !Connected.Contains(Edge.A))
			{ Connected.Add(Edge.A); bChanged = true; }
		}
	}
	struct FResponseBody
	{
		FBodyInstance* Instance;
		double InverseMass;
		FVector Center;
		FQuat MassRotation;
		FVector InverseInertia;
		FVector ApplyInverseInertia(const FVector& Torque) const
		{ return MassRotation.RotateVector(MassRotation.UnrotateVector(Torque) * InverseInertia); }
	};
	TArray<FResponseBody> Bodies;
	TMap<FBodyInstance*, int32> BodyIndices;
	double TotalMass = 0;
	bool bStaticConstraint = false;
	uint32 TopologyHash = 0;
	const auto AddBody = [&](FBodyInstance* Instance)
	{
		if (!Instance || BodyIndices.Contains(Instance)) return;
		TopologyHash ^= PointerHash(Instance);
		if (!Instance->IsInstanceSimulatingPhysics()) { bStaticConstraint = true; return; }
		const double Mass = Instance->GetBodyMass();
		const FVector Inertia = Instance->GetBodyInertiaTensor();
		BodyIndices.Add(Instance, Bodies.Num());
		Bodies.Add({Instance, 1.0 / FMath::Max(Mass, UE_DOUBLE_SMALL_NUMBER), Instance->GetCOMPosition(),
			Instance->GetMassSpaceToWorldSpace().GetRotation(),
			FVector(1.0 / FMath::Max(Inertia.X, UE_DOUBLE_SMALL_NUMBER), 1.0 / FMath::Max(Inertia.Y, UE_DOUBLE_SMALL_NUMBER),
				1.0 / FMath::Max(Inertia.Z, UE_DOUBLE_SMALL_NUMBER))});
		TotalMass += Mass;
	};
	AddBody(RootInstance);
	for (FBodyInstance* Instance : Connected) AddBody(Instance);
	struct FJacobianRow
	{
		int32 BodyA = INDEX_NONE, BodyB = INDEX_NONE;
		FVector LinearA = FVector::ZeroVector, AngularA = FVector::ZeroVector;
		FVector LinearB = FVector::ZeroVector, AngularB = FVector::ZeroVector;
	};
	TArray<FJacobianRow> Rows;
	for (const auto& Edge : Edges)
	{
		if ((!Edge.A || !Connected.Contains(Edge.A)) && (!Edge.B || !Connected.Contains(Edge.B))) continue;
		const int32* IndexA = BodyIndices.Find(Edge.A);
		const int32* IndexB = BodyIndices.Find(Edge.B);
		if (!IndexA && !IndexB) continue;
		bStaticConstraint |= !Edge.A || !Edge.B;
		const FConstraintInstance& Joint = Edge.Constraint->ConstraintInstance;
		TopologyHash ^= GetTypeHash(Edge.Constraint->GetUniqueID());
		const auto Anchor = [&](FBodyInstance* Instance, EConstraintFrame::Type Frame)
		{
			FTransform Local = Joint.GetRefFrame(Frame);
			if (!Instance) return Local;
			Local.ScaleTranslation(FVector(Joint.GetLastKnownScale()));
			FTransform Pose = Instance->GetUnrealWorldTransform(); Pose.RemoveScaling();
			return Local * Pose;
		};
		const FTransform AnchorA = Anchor(Edge.A, EConstraintFrame::Frame1);
		const FTransform AnchorB = Anchor(Edge.B, EConstraintFrame::Frame2);
		const ELinearConstraintMotion Motions[] = {Joint.GetLinearXMotion(), Joint.GetLinearYMotion(), Joint.GetLinearZMotion()};
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			if (Motions[AxisIndex] != LCM_Locked) continue;
			FVector LocalAxis = FVector::ZeroVector; LocalAxis[AxisIndex] = 1;
			const FVector Axis = AnchorA.GetRotation().RotateVector(LocalAxis);
			FJacobianRow Row;
			if (IndexA)
			{
				Row.BodyA = *IndexA; Row.LinearA = Axis;
				Row.AngularA = FVector::CrossProduct(AnchorA.GetLocation() - Bodies[*IndexA].Center, Axis);
			}
			if (IndexB)
			{
				Row.BodyB = *IndexB; Row.LinearB = -Axis;
				Row.AngularB = FVector::CrossProduct(AnchorB.GetLocation() - Bodies[*IndexB].Center, -Axis);
			}
			Rows.Add(Row);
		}
		for (const auto* Component : {Edge.ComponentA, Edge.ComponentB})
			if (const auto* Cat = Component ? Cast<ACatCharacter>(Component->GetOwner()) : nullptr)
			{
				const auto* Physical = Cat->GetPhysicalBodyComponent(); const auto* Grab = Physical->GetGrab();
				uint32 Revision = GetTypeHash(Physical->GetResetEpoch());
				if (Grab) Revision = HashCombine(Revision, HashCombine(GetTypeHash(Grab->GetGripState(true).GripId), GetTypeHash(Grab->GetGripState(false).GripId)));
				TopologyHash = HashCombine(TopologyHash, HashCombine(GetTypeHash(Component->GetUniqueID()), Revision));
			}
	}
	const int32 RowCount = Rows.Num();
	TArray<double> Cholesky; Cholesky.SetNumZeroed(RowCount * RowCount);
	const auto PairResponse = [&](const FJacobianRow& A, const FJacobianRow& B)
	{
		double Value = 0;
		const int32 IndicesA[] = {A.BodyA, A.BodyB}, IndicesB[] = {B.BodyA, B.BodyB};
		const FVector LinearA[] = {A.LinearA, A.LinearB}, LinearB[] = {B.LinearA, B.LinearB};
		const FVector AngularA[] = {A.AngularA, A.AngularB}, AngularB[] = {B.AngularA, B.AngularB};
		for (int32 I = 0; I < 2; ++I) for (int32 J = 0; J < 2; ++J)
			if (IndicesA[I] != INDEX_NONE && IndicesA[I] == IndicesB[J])
			{
				const auto& Rigid = Bodies[IndicesA[I]];
				Value += FVector::DotProduct(LinearA[I], LinearB[J]) * Rigid.InverseMass
					+ FVector::DotProduct(AngularA[I], Rigid.ApplyInverseInertia(AngularB[J]));
			}
		return Value;
	};
	double MaximumDiagonal = 0;
	for (const auto& Row : Rows) MaximumDiagonal = FMath::Max(MaximumDiagonal, PairResponse(Row, Row));
	// Redundant grip cycles are allowed. This numerical regularization preserves a conservative point response.
	const double Regularization = FMath::Max(1.e-12, MaximumDiagonal * 1.e-9);
	for (int32 I = 0; I < RowCount; ++I) for (int32 J = 0; J <= I; ++J)
	{
		double Value = PairResponse(Rows[I], Rows[J]) + (I == J ? Regularization : 0);
		for (int32 K = 0; K < J; ++K) Value -= Cholesky[I * RowCount + K] * Cholesky[J * RowCount + K];
		Cholesky[I * RowCount + J] = I == J ? FMath::Sqrt(FMath::Max(Value, Regularization))
			: Value / Cholesky[J * RowCount + J];
	}
	const FResponseBody& Root = Bodies[0];
	// Held translation is driven once at the carrier COM. The original aim solver owns rod rotation.
	const FVector Lever = ControlledBody.IsValid() ? FVector::ZeroVector : Tip - Root.Center;
	const auto PointResponse = [&](const FVector& UnitForce)
	{
		FVector Linear = UnitForce * Root.InverseMass;
		FVector Angular = Root.ApplyInverseInertia(FVector::CrossProduct(Lever, UnitForce));
		TArray<double> Lambda; Lambda.SetNumZeroed(RowCount);
		for (int32 I = 0; I < RowCount; ++I)
		{
			const auto& Row = Rows[I];
			double Value = Row.BodyA == 0 ? FVector::DotProduct(Row.LinearA, Linear) + FVector::DotProduct(Row.AngularA, Angular) : 0;
			if (Row.BodyB == 0) Value += FVector::DotProduct(Row.LinearB, Linear) + FVector::DotProduct(Row.AngularB, Angular);
			for (int32 K = 0; K < I; ++K) Value -= Cholesky[I * RowCount + K] * Lambda[K];
			Lambda[I] = Value / Cholesky[I * RowCount + I];
		}
		for (int32 I = RowCount - 1; I >= 0; --I)
		{
			for (int32 K = I + 1; K < RowCount; ++K) Lambda[I] -= Cholesky[K * RowCount + I] * Lambda[K];
			Lambda[I] /= Cholesky[I * RowCount + I];
		}
		for (int32 I = 0; I < RowCount; ++I)
		{
			if (Rows[I].BodyA == 0) { Linear -= Rows[I].LinearA * (Lambda[I] * Root.InverseMass); Angular -= Root.ApplyInverseInertia(Rows[I].AngularA) * Lambda[I]; }
			if (Rows[I].BodyB == 0) { Linear -= Rows[I].LinearB * (Lambda[I] * Root.InverseMass); Angular -= Root.ApplyInverseInertia(Rows[I].AngularB) * Lambda[I]; }
		}
		return Linear + FVector::CrossProduct(Angular, Lever);
	};
	OutInput.RodPointInverseMassX = PointResponse(FVector::ForwardVector);
	OutInput.RodPointInverseMassY = PointResponse(FVector::RightVector);
	OutInput.RodPointInverseMassZ = PointResponse(FVector::UpVector);
	const double Now = GetWorld()->GetTimeSeconds();
	const double Elapsed = Now - LastEndpointSampleSeconds;
	const double PhysicsElapsed = AppliedPhysicsSeconds - LastSampleAppliedPhysicsSeconds;
	const bool bReset = LastEndpointSampleSeconds < 0 || TopologyHash != LastMechanicalTopologyHash
		|| Elapsed > 0.25 || (Tip - LastEndpointPosition - LastEndpointVelocity * FMath::Max(0.0, Elapsed)).Size() > 100.0;
	if (bReset)
	{
		ObservedEndpointAcceleration = FVector::ZeroVector;
		ObservedAppliedAverageForce = FVector::ZeroVector;
	}
	else if (Elapsed > UE_DOUBLE_SMALL_NUMBER && PhysicsElapsed > UE_DOUBLE_SMALL_NUMBER)
	{
		ObservedEndpointAcceleration = (Velocity - LastEndpointVelocity) / PhysicsElapsed;
		ObservedAppliedAverageForce = (AppliedLineImpulse - LastSampleAppliedImpulse) / PhysicsElapsed;
	}
	if (Elapsed > UE_DOUBLE_SMALL_NUMBER || bReset)
	{
		LastEndpointSampleSeconds = Now; LastEndpointPosition = Tip; LastEndpointVelocity = Velocity;
		LastSampleAppliedImpulse = AppliedLineImpulse; LastSampleAppliedPhysicsSeconds = AppliedPhysicsSeconds;
		LastMechanicalTopologyHash = TopologyHash;
	}
	OutInput.RodTipWorldPosition = Tip;
	OutInput.RodTipVelocityCentimetersPerSecond = Velocity;
	OutInput.RodTipAccelerationCentimetersPerSecondSquared = ObservedEndpointAcceleration;
	OutInput.PreviousLineForceNewtons = ObservedAppliedAverageForce;
	OutInput.PhysicsStepSeconds = LastPhysicsSubstepSeconds;
	OutInput.PendingLineResponseSeconds = GetQueuedLineSecondsForDiagnostics();
	OutInput.PendingLineImpulseNewtonSeconds = GetQueuedLineImpulseNewtonSecondsForDiagnostics();
	OutInput.PendingLinePositionMomentNewtonSecondsSquared = FVector::ZeroVector;
	double QueueStart = 0;
	for (const auto& Segment : LineSegments)
	{
		OutInput.PendingLinePositionMomentNewtonSecondsSquared += Segment.ForceNewtons * Segment.RemainingSeconds
			* (OutInput.PendingLineResponseSeconds - QueueStart - Segment.RemainingSeconds * 0.5);
		QueueStart += Segment.RemainingSeconds;
	}
	if (bReset || Now >= NextEndpointLogSeconds)
	{
		NextEndpointLogSeconds = Now + 1;
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_physical_endpoint_response SessionId=%s RodActorId=%s LockedBodies=%d LockedMassKg=%.4f StaticLockedEndpoint=%d ObserverReset=%d TipVelocityCmS=%s TipAccelerationCmS2=%s InverseMassX=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*LoadSessionId.ToString(), *Rod->GetPresentationState().RodActorId.ToString(), Bodies.Num(), TotalMass, bStaticConstraint, bReset,
			*Velocity.ToCompactString(), *ObservedEndpointAcceleration.ToCompactString(), *OutInput.RodPointInverseMassX.ToCompactString(),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
	}
}
bool UCatFishingPhysicalRodComponent::IsHeldBy(const APlayerState* Player) const
{
	const ACatCharacter* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	const auto* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	const auto* Grab = Physical ? Physical->GetGrab() : nullptr;
	return Grab && (Grab->GetGripTargetComponent(true) == Body || Grab->GetGripTargetComponent(false) == Body);
}

void UCatFishingPhysicalRodComponent::ObserveGrab(UCatPhysicsGrabComponent* Grab)
{
	if (!Grab || ObservedGrabs.Contains(Grab)) return;
	ObservedGrabs.Add(Grab);
	Grab->OnGripChanged.AddUObject(this, &ThisClass::HandleGripChanged);
}

void UCatFishingPhysicalRodComponent::HandleGripChanged(UCatPhysicsGrabComponent* Grab, const bool bLeft,
	const FCatPhysicsGripState& Previous, const FCatPhysicsGripState& Current)
{
	if (bEndingPlay || (!Previous.bGripped && !Current.bGripped)) return;
	RefreshPrimaryControl();
}



bool UCatFishingPhysicalRodComponent::BeginPrimaryHold(APlayerState* Player, const bool bPositionNewRod)
{
	ACatCharacter* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	UCatPhysicalBodyComponent* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	UCatPhysicsGrabComponent* Grab = Physical ? Physical->GetGrab() : nullptr;
	if (!bReady || !GetOwner()->HasAuthority() || !Grab) return false;
	ObserveGrab(Grab);
	if (IsHeldBy(Player)) return true;
	const bool bLeft = !Grab->IsGripping(true);
	if (Grab->IsGripping(bLeft)) return false;
	const FVector HandPoint = Physical->GetHand(bLeft)->GetComponentLocation();
	if (bPositionNewRod)
	{
		const FTransform DesiredGrip(Cat->GetActorQuat(), HandPoint);
		const FTransform ActorPose = GripLocalTransform.Inverse() * DesiredGrip;
		Body->SetWorldTransform(BodyLocal * ActorPose, false, nullptr, ETeleportType::ResetPhysics);
		Body->SetPhysicsLinearVelocity(Physical->GetVelocity());
		RefreshObservedPose();
	}

	const FVector GripPoint = CastChecked<ACatFishingRodActor>(GetOwner())->GetGripWorldTransform().GetLocation();
	return Grab->GripFromAuthority(bLeft, Body, GripPoint);
}

bool UCatFishingPhysicalRodComponent::CommitPrimaryHold(APlayerState* Player)
{
	auto* Rod = Cast<ACatFishingRodActor>(GetOwner());
	auto* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	auto* Grab = Cat && Cat->GetPhysicalBodyComponent() ? Cat->GetPhysicalBodyComponent()->GetGrab() : nullptr;
	if (!Rod || !Rod->HasAuthority() || !Rod->IsPrimaryOperator(Player)
		|| Rod->GetPresentationState().OwnerPlayerState != Player || !Grab) return false;
	// Commit only one existing contact. The other hand remains an ordinary continuous grip.
	RefreshInputTickPrerequisites();
	for (const bool bLeft : {true, false})
		if (Grab->GetGripTargetComponent(bLeft) == Body)
			{
				if (!Grab->RetainGripFromAuthority(bLeft, Body) || !Grab->ControlRetainedGripFromAuthority(bLeft, Body)) return false;
				RefreshControlledCarrier();
				return true;
			}
	return false;
}

void UCatFishingPhysicalRodComponent::ReleasePrimaryHold(APlayerState* Player, const FName Reason)
{
	ACatCharacter* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	auto* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	auto* Grab = Physical ? Physical->GetGrab() : nullptr;
	if (!GetOwner()->HasAuthority() || !Grab) return;
	for (const bool bLeft : {true, false})
		if (Grab->GetGripTargetComponent(bLeft) == Body) Grab->ReleaseHandFromAuthority(bLeft, Reason);
	Physical->ClearFishingMotorBudget(this);
	RefreshControlledCarrier();
}

void UCatFishingPhysicalRodComponent::ReleaseAllConnections(const FName Reason)
{
	if (GetOwner()->HasAuthority())
		for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
			if (UCatPhysicalBodyComponent* Physical = It->GetPhysicalBodyComponent(); Physical && Physical->GetGrab())
				Physical->GetGrab()->ReleaseTargetFromAuthority(GetOwner(), Reason);
	if (BudgetBody.IsValid()) BudgetBody->ClearFishingMotorBudget(this);
	BudgetBody.Reset();
	ClearLineLoad();
}

void UCatFishingPhysicalRodComponent::RefreshObservedPose()
{
	if (!bReady || !GetOwner()->HasAuthority()) return;
	ACatFishingRodActor* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (ControlledBody.IsValid()) PositionControlledRod();
	// The detached dynamic body is observed; the controlled body follows its real primary carrier.
	Rod->SetActorTransform(GetObservedActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	Rod->AuthoritativeHeldAimRotation = Rod->GetGripWorldTransform().Rotator();
	Rod->AuthoritativeRodTipVelocity = GetPointVelocity(Rod->GetRodTipWorldTransform().GetLocation());
	const ACatCharacter* Holder = Cast<ACatCharacter>(Rod->GetHolderPawnFromAuthority());
	if (Holder)
	{
		Rod->bHeldAimInitialized = true;
		Rod->AuthoritativeAimHolder = const_cast<ACatCharacter*>(Holder);
	}
	Rod->AuthoritativeHolderVelocity = Holder && Holder->GetPhysicalBodyComponent()
		? Holder->GetPhysicalBodyComponent()->GetVelocity() : FVector::ZeroVector;
}

void UCatFishingPhysicalRodComponent::RefreshPrimaryControl()
{
	if (!bReady || !GetOwner()->HasAuthority() || bRefreshingPrimaryControl) return;
	TGuardValue<bool> Guard(bRefreshingPrimaryControl, true);
	if (auto* Service = GetWorld()->GetSubsystem<UCatFishingService>())
		Service->ReconcilePrimaryControlFromPhysicalGrip(CastChecked<ACatFishingRodActor>(GetOwner()));
	RefreshControlledCarrier();
}

FVector UCatFishingPhysicalRodComponent::GetQueuedLineImpulseNewtonSecondsForDiagnostics() const
{
	FVector Impulse = PendingPhysicsImpulse;
	for (const auto& Segment : LineSegments) Impulse += Segment.ForceNewtons * Segment.RemainingSeconds;
	return Impulse;
}

double UCatFishingPhysicalRodComponent::GetQueuedLineSecondsForDiagnostics() const
{
	double Seconds = 0;
	for (const auto& Segment : LineSegments) Seconds += Segment.RemainingSeconds;
	return Seconds;
}

void UCatFishingPhysicalRodComponent::FinishPhysicsFrame()
{
	if (!GetOwner()->HasAuthority()) return;
	AppliedLineImpulse += PendingPhysicsImpulse;
	AppliedPhysicsSeconds += PendingPhysicsSeconds;
	PendingPhysicsImpulse = FVector::ZeroVector;
	PendingPhysicsSeconds = 0;
}

void UCatFishingPhysicalRodComponent::SetLineLoad(const FGuid SessionId, const uint64 Step,
	const FVector& ForceNewtons, const double SimulatedSeconds, const double LifetimeSeconds)
{
	if (!GetOwner()->HasAuthority()) return;
	ACatFishingRodActor* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	UCatFishingService* Service = GetWorld()->GetSubsystem<UCatFishingService>();
	const ACatFishingSession* Session = Service ? Service->FindActiveSessionByRod(Rod) : nullptr;
	const bool bValidDomain = Session && Session->GetSnapshot().FishingSessionId == SessionId;
	if (!bValidDomain || !SessionId.IsValid() || ForceNewtons.ContainsNaN()
		|| !FMath::IsFinite(SimulatedSeconds) || SimulatedSeconds <= 0
		|| !FMath::IsFinite(LifetimeSeconds) || LifetimeSeconds <= 0.0 || (LoadSessionId == SessionId && Step <= LoadStep))
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_physical_line_load_rejected SessionId=%s ActiveSessionId=%s RodActorId=%s Step=%llu PreviousStep=%llu World=%s NetMode=%d Authority=1 LocalRole=%d Reason=%s"),
			*SessionId.ToString(), Session ? *Session->GetSnapshot().FishingSessionId.ToString() : TEXT("None"),
			*Rod->GetPresentationState().RodActorId.ToString(), Step, LoadStep, *GetNameSafe(GetWorld()),
			int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), bValidDomain ? TEXT("InvalidOrStaleSample") : TEXT("SessionDomain"));
		return;
	}
	if (LoadSessionId != SessionId)
	{
		ClearLineLoad();
		LastEndpointSampleSeconds = -1;
	}
	LoadSessionId = SessionId; LoadStep = Step; LineForceNewtons = ForceNewtons;
	LineSegments.Add({ForceNewtons, SimulatedSeconds});
	SubmittedLineImpulse += ForceNewtons * SimulatedSeconds;
	// The timeout is also a bound on outstanding simulated force time. Long hitches never build an unbounded backlog.
	// PrePhysics production may briefly contain both this frame and its future tail. Only the future tail is backlog.
	const double AllowedSeconds = LifetimeSeconds + (bProducingCurrentPhysicsFrame ? PendingPhysicsSeconds : 0.0);
	const double ExcessSeconds = GetQueuedLineSecondsForDiagnostics() - AllowedSeconds;
	if (ExcessSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		double RemainingExcess = ExcessSeconds;
		FVector Removed = FVector::ZeroVector;
		while (RemainingExcess > UE_DOUBLE_SMALL_NUMBER && !LineSegments.IsEmpty())
		{
			auto& Segment = LineSegments[0];
			const double Used = FMath::Min(RemainingExcess, Segment.RemainingSeconds);
			Removed += Segment.ForceNewtons * Used;
			RemainingExcess -= Used; Segment.RemainingSeconds -= Used;
			if (Segment.RemainingSeconds <= UE_DOUBLE_SMALL_NUMBER) LineSegments.RemoveAt(0);
		}
		DiscardedLineImpulse += Removed;
		LastEndpointSampleSeconds = -1;
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_physical_line_backlog_expired SessionId=%s RodActorId=%s Step=%llu DiscardedSeconds=%.6f DiscardedImpulseNs=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=OldestLoadRemoved"),
			*SessionId.ToString(), *Rod->GetPresentationState().RodActorId.ToString(), Step, ExcessSeconds, *Removed.ToCompactString(),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
	}
	LoadExpiresAt = GetWorld()->GetTimeSeconds() + LifetimeSeconds;
	if (GetWorld()->GetTimeSeconds() >= NextLoadLogSeconds)
	{
		NextLoadLogSeconds = GetWorld()->GetTimeSeconds() + 1.0;
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_physical_line_load SessionId=%s RodActorId=%s Step=%llu ForceN=%s SimulatedSeconds=%.6f QueuedSeconds=%.6f SubmittedImpulseNs=%s AppliedImpulseNs=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=RodTipImpulseQueued"),
			*SessionId.ToString(), *Rod->GetPresentationState().RodActorId.ToString(),
			Step, *ForceNewtons.ToCompactString(), SimulatedSeconds, GetQueuedLineSecondsForDiagnostics(),
			*SubmittedLineImpulse.ToCompactString(), *AppliedLineImpulse.ToCompactString(),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
	}
}

void UCatFishingPhysicalRodComponent::ClearLineLoad(const FGuid SessionId)
{
	if (SessionId.IsValid() && SessionId != LoadSessionId) return;
	FVector RemovedImpulse = FVector::ZeroVector;
	for (const auto& Segment : LineSegments) RemovedImpulse += Segment.ForceNewtons * Segment.RemainingSeconds;
	if (!LineForceNewtons.IsZero() || !LineSegments.IsEmpty())
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_physical_line_load_cleared SessionId=%s RodActorId=%s Step=%llu DiscardedImpulseNs=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=QueuedForceRemoved"),
			*LoadSessionId.ToString(), *CastChecked<ACatFishingRodActor>(GetOwner())->GetPresentationState().RodActorId.ToString(),
			LoadStep, *RemovedImpulse.ToCompactString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()));
	DiscardedLineImpulse += RemovedImpulse;
	LineSegments.Reset();
	LineForceNewtons = FVector::ZeroVector; LoadExpiresAt = 0.0;
	if (auto* LightProp = UCatLightPropComponent::FindFor(Body)) LightProp->SetExternalLoadFromAuthority(false);
}

void UCatFishingPhysicalRodComponent::RefreshInputTickPrerequisites()
{
	const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	const auto* Cat = Cast<ACatCharacter>(Rod->GetHolderPawnFromAuthority());
	auto* Controller = Cat ? Cat->GetController() : nullptr;
	auto* Grab = Cat && Cat->GetPhysicalBodyComponent() ? Cat->GetPhysicalBodyComponent()->GetGrab() : nullptr;
	if (Controller != InputTickController.Get())
	{
		if (InputTickController.IsValid()) PrimaryComponentTick.RemovePrerequisite(InputTickController.Get(), InputTickController->PrimaryActorTick);
		InputTickController = Controller;
		if (Controller) PrimaryComponentTick.AddPrerequisite(Controller, Controller->PrimaryActorTick);
	}
	if (Grab != InputTickGrab.Get())
	{
		if (InputTickGrab.IsValid()) PrimaryComponentTick.RemovePrerequisite(InputTickGrab.Get(), InputTickGrab->PrimaryComponentTick);
		InputTickGrab = Grab;
		if (Grab) PrimaryComponentTick.AddPrerequisite(Grab, Grab->PrimaryComponentTick);
	}
}

void UCatFishingPhysicalRodComponent::UpdatePrimaryMotorBudget()
{
	RefreshInputTickPrerequisites();
	const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	auto* Cat = Cast<ACatCharacter>(Rod->GetHolderPawnFromAuthority());
	auto* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	const auto* ASC = Cat ? Cat->GetCatAbilitySystemComponent() : nullptr;
	if (BudgetBody.IsValid() && BudgetBody.Get() != Physical) BudgetBody->ClearFishingMotorBudget(this);
	BudgetBody = Physical;
	if (!Physical || !ASC) return;
	const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
	const double Stamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double Force = Balance && Stamina > 0.0 && FMath::IsFinite(Strength)
		? FMath::Max(0.0, Strength) * Balance->ForcePerStrengthNewtons * 100.0 : 0.0;
	Physical->SetFishingMotorBudget(this, Force, Physical->MaxMovementSpeedCmS);
}

void UCatFishingPhysicalRodComponent::RefreshControlledCarrier()
{
	if (!bReady || !GetOwner()->HasAuthority()) return;
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	auto* Cat = Cast<ACatCharacter>(Rod->GetHolderPawnFromAuthority());
	auto* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	auto* Grab = Physical ? Physical->GetGrab() : nullptr;
	UPrimitiveComponent* Next = nullptr;
	if (Grab && Physical->IsLocomotionEnabled())
		for (const bool bLeft : {true, false})
			if (Grab->GetGripTargetComponent(bLeft) == Body && Grab->GetGripState(bLeft).bControlledHold) Next = Physical->GetBody();
	if (Next == ControlledBody.Get() && Body->IsSimulatingPhysics() == (Next == nullptr)) return;
	const FVector ReleaseVelocity = GetPointVelocity(Body->GetComponentLocation());
	const FVector ReleaseAngularVelocity = GetAngularVelocityRadiansPerSecond();
    if (ControlledBody.IsValid())
        if (auto* Previous = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(); Previous && Previous->UsesCharacterMovement())
            GetOwner()->PrimaryActorTick.RemovePrerequisite(Previous, Previous->GetPostMovementTick());
	ControlledBody = Next;
    if (Next)
        if (auto* Current = Next->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(); Current && Current->UsesCharacterMovement())
            GetOwner()->PrimaryActorTick.AddPrerequisite(Current, Current->GetPostMovementTick());
	ControlledAngularVelocity = FVector::ZeroVector;
	SmoothedFishPull = FVector::ZeroVector;
	Body->SetSimulatePhysics(Next == nullptr);
	if (Next)
	{
		FRotator Aim = Physical->GetViewIntent();
		const auto* Settings = GetDefault<UCatFishingSettings>();
		Aim.Pitch = FMath::ClampAngle(Aim.Pitch, Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
		Aim.Roll = 0;
		Rod->AuthoritativeHeldAimRotation = Aim;
		Rod->bHeldAimInitialized = true;
		Rod->AuthoritativeAimHolder = Cat;
		PositionControlledRod();
	}
	else
	{
		Body->SetPhysicsLinearVelocity(ReleaseVelocity);
		Body->SetPhysicsAngularVelocityInRadians(ReleaseAngularVelocity);
	}
	if (auto* Light = UCatLightPropComponent::FindFor(Body)) Light->SetGripCarrierFromAuthority(Next);
	LastEndpointSampleSeconds = -1;
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_controlled_carrier RodActorId=%s PlayerId=%d Carrier=%s Controlled=%d World=%s NetMode=%d Authority=1 LocalRole=%d Result=PrimaryPoseAndPhysicalAssist"),
		*Rod->PresentationState.RodActorId.ToString(), Cat && Cat->GetPlayerState() ? Cat->GetPlayerState()->GetPlayerId() : INDEX_NONE,
		*GetNameSafe(Next), Next != nullptr, *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
}

void UCatFishingPhysicalRodComponent::PositionControlledRod()
{
	if (!ControlledBody.IsValid()) return;
	const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	const FQuat Aim = Rod->AuthoritativeHeldAimRotation.Quaternion();
	const FVector Grip = ControlledBody->GetComponentLocation()
		+ Aim.RotateVector(GetDefault<UCatFishingSettings>()->HeldRodGripOffsetCentimeters);
	const FTransform ActorPose = GripLocalTransform.Inverse() * FTransform(Aim, Grip);
	Body->SetWorldTransform(BodyLocal * ActorPose, false, nullptr, ETeleportType::TeleportPhysics);
	if (auto* Light = UCatLightPropComponent::FindFor(Body)) Light->RefreshGripConstraintsFromAuthority();
}

bool UCatFishingPhysicalRodComponent::BuildControlledRotationInput(FCatFishingRodRotationInput& Input) const
{
    if (!ControlledBody.IsValid()) return false;
    const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
    const auto* Physical = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
    const auto* Settings = GetDefault<UCatFishingSettings>();
    if (!Physical || !Rod->CarrierConstraintState.bFightActive || Rod->bAwaitingNewHolderAim) return false;
    const bool bMouseActive = Rod->HeldAimInput.IsMouseActive(GetWorld()->GetTimeSeconds());
		Input.CurrentAim = Rod->AuthoritativeHeldAimRotation;
		Input.RequestedAim = bMouseActive ? Rod->HeldAimInput.GetRequestedAim() : Input.CurrentAim;
		Input.bCatDriveActive = bMouseActive;
		Input.PullAxis = Rod->CarrierConstraintState.RodPullAxis.IsNearlyZero() ? FVector::ForwardVector : Rod->CarrierConstraintState.RodPullAxis;
		Input.PreviousSmoothedFishPullStrengthMeters = SmoothedFishPull;
		Input.PreviousAngularVelocityRadiansPerSecond = ControlledAngularVelocity;
		Input.CatTorqueCapacity = Rod->CarrierConstraintState.CatTorqueCapacityStrengthMeters;
		const auto* Cat = Cast<ACatCharacter>(ControlledBody->GetOwner());
		const auto* ASC = Cat ? Cat->GetCatAbilitySystemComponent() : nullptr;
		if (!ASC || !Physical->IsLocomotionEnabled()
			|| ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) <= 0)
			Input.CatTorqueCapacity = 0;
		Input.MaximumFishTorque = Rod->CarrierConstraintState.MaximumFishTorqueStrengthMeters;
		Input.MaximumAngularSpeedDegreesPerSecond = Settings->HeldRodMaximumAngularSpeedDegreesPerSecond;
		Input.ResponseSeconds = Settings->HeldRodAngularResistanceResponseSeconds;
		Input.AngularInertiaSeconds = Settings->HeldRodAngularInertiaSeconds;
		Input.FishPullSmoothingSeconds = Settings->HeldRodFishPullSmoothingSeconds;
		Input.LoadedAngularDampingRatio = Settings->HeldRodLoadedAngularDampingRatio;
		Input.MinimumPitchDegrees = Settings->HeldRodMinimumPitchDegrees;
		Input.MaximumPitchDegrees = Settings->HeldRodMaximumPitchDegrees;
    return true;
}

void UCatFishingPhysicalRodComponent::AdvanceControlledAim(const float DeltaTime)
{
	if (!ControlledBody.IsValid()) return;
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	auto* Physical = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const auto* Settings = GetDefault<UCatFishingSettings>();
	if (!Physical) return;
	if (ControlledEffortEpoch != Rod->AuthoritativeRotationEffort.Epoch)
	{
		ControlledEffortEpoch = Rod->AuthoritativeRotationEffort.Epoch;
		ControlledAngularVelocity = SmoothedFishPull = FVector::ZeroVector;
	}
	const bool bExpired = Rod->HeldAimInput.ExpireInput(GetWorld()->GetTimeSeconds(), Rod->AuthoritativeHeldAimRotation);
	const bool bActive = !Rod->bAwaitingNewHolderAim && Rod->CarrierConstraintState.bFightActive
		&& Rod->HeldAimInput.IsMouseActive(GetWorld()->GetTimeSeconds());
	if (bExpired || bLastMouseMotorActive != bActive)
	{
		bLastMouseMotorActive = bActive;
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_controlled_aim_input RodActorId=%s AimInputEpoch=%u AimSequence=%lld MouseActive=%d Expired=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*Rod->PresentationState.RodActorId.ToString(), Rod->CarrierConstraintState.AimInputEpoch, Rod->HeldAimInput.GetLastSequence(),
			bActive, bExpired, *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
	}
	if (!Rod->CarrierConstraintState.bFightActive)
	{
		FRotator Aim = Physical->GetViewIntent();
		Aim.Pitch = FMath::ClampAngle(Aim.Pitch, Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
		Aim.Roll = 0;
		Rod->AuthoritativeHeldAimRotation = Aim;
		ControlledAngularVelocity = SmoothedFishPull = FVector::ZeroVector;
	}
	else if (!Rod->bAwaitingNewHolderAim)
	{
		FCatFishingRodRotationInput Input;
		if (!BuildControlledRotationInput(Input)) return;
		Input.DeltaSeconds = DeltaTime;
		const auto Step = FCatFishingRodResistanceModel::StepRotation(Input);
		if (Step.bSucceeded)
		{
			Rod->AuthoritativeHeldAimRotation = Step.ActualAim;
			ControlledAngularVelocity = Step.AngularVelocityRadiansPerSecond;
			SmoothedFishPull = Step.SmoothedFishPullStrengthMeters;
			Rod->AuthoritativeRotationEffort.ExertionSquaredSeconds += Step.CatExertionSquaredSeconds;
			Rod->AuthoritativeRotationEffort.PositiveWorkRadians += Step.CatPositiveWorkRadians;
			Rod->AuthoritativeRotationEffort.IntegratedSeconds += Step.IntegratedSeconds;
		}
	}
	RefreshObservedPose();
}

void UCatFishingPhysicalRodComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!bReady || !GetOwner()->HasAuthority()) return;
	RefreshObservedPose();
	RefreshPrimaryControl();
	UpdatePrimaryMotorBudget();
	ACatFishingRodActor* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (!Rod->PresentationState.bDeployed || Rod->PresentationState.bBroken) { ReleaseAllConnections(TEXT("RodUnavailable")); return; }
	const UPhysicsSettings* Physics = UPhysicsSettings::Get();
	const double NetworkScale = GetWorld()->GetPhysicsScene() ? GetWorld()->GetPhysicsScene()->GetNetworkDeltaTimeScale() : 1.0;
	const double RequestedPhysicsSeconds = FMath::Max(0.0, double(DeltaTime) * NetworkScale);
	const double PhysicsLimit = Physics->bSubstepping ? Physics->MaxSubsteps * double(Physics->MaxSubstepDeltaTime) : double(Physics->MaxPhysicsDeltaTime);
	const double PhysicsSeconds = PhysicsLimit > 0 ? FMath::Min(RequestedPhysicsSeconds, PhysicsLimit) : RequestedPhysicsSeconds;
	const int32 Substeps = Physics->bSubstepping && Physics->MaxSubstepDeltaTime > 0
		? FMath::Clamp(FMath::CeilToInt(PhysicsSeconds / Physics->MaxSubstepDeltaTime), 1, Physics->MaxSubsteps) : 1;
	LastPhysicsSubstepSeconds = PhysicsSeconds / Substeps;
	PendingPhysicsSeconds = PhysicsSeconds;
	AdvanceControlledAim(DeltaTime);
	{
		TGuardValue<bool> Producing(bProducingCurrentPhysicsFrame, true);
		BeforePhysicsForces.Broadcast(DeltaTime);
	}
	if (!IsValid(Rod) || Rod->IsActorBeingDestroyed() || !Rod->PresentationState.bDeployed || Rod->PresentationState.bBroken) return;
	if (!LineSegments.IsEmpty() && GetWorld()->GetTimeSeconds() > LoadExpiresAt)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_physical_line_load_expired SessionId=%s RodActorId=%s Step=%llu QueuedSeconds=%.6f World=%s NetMode=%d Authority=1 LocalRole=%d Result=QueuedForceRemoved"),
			*LoadSessionId.ToString(), *Rod->PresentationState.RodActorId.ToString(), LoadStep, GetQueuedLineSecondsForDiagnostics(),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()));
		ClearLineLoad(LoadSessionId);
	}
	double RemainingFrame = PhysicsSeconds;
	FVector FrameImpulse = FVector::ZeroVector;
	while (RemainingFrame > UE_DOUBLE_SMALL_NUMBER && !LineSegments.IsEmpty())
	{
		auto& Segment = LineSegments[0];
		const double Used = FMath::Min(RemainingFrame, Segment.RemainingSeconds);
		FrameImpulse += Segment.ForceNewtons * Used;
		RemainingFrame -= Used; Segment.RemainingSeconds -= Used;
		if (Segment.RemainingSeconds <= UE_DOUBLE_SMALL_NUMBER) LineSegments.RemoveAt(0);
	}
	PendingPhysicsImpulse = FrameImpulse;
	if (auto* LightProp = UCatLightPropComponent::FindFor(Body))
		LightProp->SetExternalLoadFromAuthority(!FrameImpulse.IsNearlyZero() || !GetQueuedLineImpulseNewtonSecondsForDiagnostics().IsNearlyZero());
	if (PhysicsSeconds > UE_DOUBLE_SMALL_NUMBER && !FrameImpulse.IsZero())
		{
		const FVector Force = FrameImpulse * (100.0 / PhysicsSeconds);
		if (ControlledBody.IsValid())
		{
			if (auto* Carrier = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>())
				Carrier->AddExternalImpulseFromAuthority(FrameImpulse * 100.0);
			else ControlledBody->AddForce(Force);
		}
		else Body->AddForceAtLocation(Force, Rod->GetRodTipWorldTransform().GetLocation());
	}

}

void UCatFishingPhysicalRodComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	PhysicsReceiverUnavailable.Broadcast();
	PhysicsReceiverUnavailable.Clear();
	BeforePhysicsForces.Clear();
	if (InputTickController.IsValid()) PrimaryComponentTick.RemovePrerequisite(InputTickController.Get(), InputTickController->PrimaryActorTick);
	if (InputTickGrab.IsValid()) PrimaryComponentTick.RemovePrerequisite(InputTickGrab.Get(), InputTickGrab->PrimaryComponentTick);
	InputTickController.Reset(); InputTickGrab.Reset();
	for (const auto& Grab : ObservedGrabs) if (Grab.IsValid()) Grab->OnGripChanged.RemoveAll(this);
	ObservedGrabs.Reset();
	ReleaseAllConnections(TEXT("RodEndPlay"));
	Super::EndPlay(EndPlayReason);
}

void UCatFishingPhysicalRodComponent::PopulateCMCEndpointPrediction(FCatFightRodConstraintInput& OutInput)
{
    const auto* Cat = CastChecked<ACatCharacter>(ControlledBody->GetOwner());
    auto* Movement = CastChecked<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
    const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
    const FCatCMCMotionPrediction Motion = Movement->CaptureMotionPrediction();
    const TArray<FLineForceSegment> Pending = LineSegments;
    FCatFishingRodRotationInput Rotation;
    const bool bPredictRotation = BuildControlledRotationInput(Rotation);
    const FRotator InitialAim = Rod->AuthoritativeHeldAimRotation;
    const FVector Tip = Rod->GetRodTipWorldTransform().GetLocation();
    const FVector TipOffset = InitialAim.UnrotateVector(Tip - Motion.Position);
    OutInput.bPhysicalRodEndpoint = true;
    OutInput.RodTipWorldPosition = Tip;
    OutInput.RodTipVelocityCentimetersPerSecond = GetPointVelocity(Tip);
    OutInput.PhysicsStepSeconds = 1.0 / 120.0;
    const TWeakObjectPtr<UCatCharacterMovementComponent> WeakMovement(Movement);
    OutInput.GetCMCTravelLimit = [WeakMovement](const FVector& Axis, double Distance)
    { return WeakMovement.IsValid() ? WeakMovement->GetExternalTractionTravelLimit(Axis, Distance) : 0.0; };
    OutInput.PredictCMCEndpoint = [Motion, Pending, Rotation, bPredictRotation, InitialAim, TipOffset](const FCatFightCMCPredictionQuery& Query)
    {
        FCatFightCMCPredictionResult Result;
        auto PredictedMotion = Motion;
        auto PredictedRotation = Rotation;
        FRotator Aim = InitialAim;
        const auto Advance = [&](const FVector& Force, double Seconds)
        {
            UCatCharacterMovementComponent::AdvanceMotionPrediction(PredictedMotion, Force, Seconds);
            if (!bPredictRotation) return true;
            PredictedRotation.DeltaSeconds = Seconds;
            PredictedRotation.MaximumFishTorque = Force.Size() * Query.TorqueStrengthMetersPerNewton;
            PredictedRotation.PullAxis = Force.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, PredictedRotation.PullAxis);
            const auto Step = FCatFishingRodResistanceModel::StepRotation(PredictedRotation);
            if (!Step.bSucceeded) return false;
            Aim = PredictedRotation.CurrentAim = Step.ActualAim;
            PredictedRotation.PreviousAngularVelocityRadiansPerSecond = Step.AngularVelocityRadiansPerSecond;
            PredictedRotation.PreviousSmoothedFishPullStrengthMeters = Step.SmoothedFishPullStrengthMeters;
            return true;
        };
        for (const auto& Segment : Pending) if (!Advance(Segment.ForceNewtons, Segment.RemainingSeconds)) return Result;
        if (!Advance(Query.ForceNewtons, Query.Seconds)) return Result;
        FVector Travel = PredictedMotion.Position - Motion.Position;
        const double Along = FVector::DotProduct(Travel, Query.TravelAxis);
        if (Along > Query.TravelLimitCentimeters) Travel -= Query.TravelAxis * (Along - Query.TravelLimitCentimeters);
        Result.RodTipWorldPosition = Motion.Position + Travel + Aim.RotateVector(TipOffset);
        Result.bSucceeded = !Result.RodTipWorldPosition.ContainsNaN();
        return Result;
    };
    if (GetWorld()->GetTimeSeconds() >= NextEndpointLogSeconds)
    {
        NextEndpointLogSeconds = GetWorld()->GetTimeSeconds() + 1;
        UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_cmc_endpoint_snapshot SessionId=%s RodActorId=%s BodyId=%s World=%s NetMode=%d Authority=1 LocalRole=%d MassKg=%.3f PendingSeconds=%.6f RotationPredicted=%d Result=FrozenCandidateInputs"),
            *LoadSessionId.ToString(), *Rod->GetPresentationState().RodActorId.ToString(), *Cat->GetPhysicalBodyComponent()->GetBodyId().ToString(),
            *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), Motion.MassKg, GetQueuedLineSecondsForDiagnostics(), bPredictRotation);
    }
}
