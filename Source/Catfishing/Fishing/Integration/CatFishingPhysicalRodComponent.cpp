#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
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
	Body->SetSimulatePhysics(false);
	Body->SetEnableGravity(false);
	// The authoritative body owns the world pose; the replicated Actor observes it without parent feedback.
	if (GetOwner()->HasAuthority()) Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	if (auto* LightProp = GetOwner()->FindComponentByClass<UCatLightPropComponent>())
	{
		LightProp->Initialize(Body);
		LightProp->SetParkedFromAuthority(true);
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
	return ControlledBody.IsValid() ? ControlledAngularVelocity : FVector::ZeroVector;
}

void UCatFishingPhysicalRodComponent::PopulateEndpointResponse(FCatFightRodConstraintInput& OutInput)
{
	if (!bReady || !GetOwner()->HasAuthority() || !Body) return;
	if (ControlledBody.IsValid()) { PopulateCMCEndpointPrediction(OutInput); return; }
	// An unattended rod is a fixed support. Use the original fixed-tip fishing solve.
	OutInput.bPhysicalRodEndpoint = false;
	OutInput.PredictCMCEndpoint = {};
	OutInput.GetCMCTravelLimit = {};
	OutInput.RodTipWorldPosition = CastChecked<ACatFishingRodActor>(GetOwner())->GetRodTipWorldTransform().GetLocation();
	OutInput.RodTipVelocityCentimetersPerSecond = FVector::ZeroVector;
	OutInput.CarrierVelocityCentimetersPerSecond = FVector::ZeroVector;
	OutInput.RodTipAccelerationCentimetersPerSecondSquared = FVector::ZeroVector;
	OutInput.PreviousLineForceNewtons = FVector::ZeroVector;
	OutInput.RodPointInverseMassX = OutInput.RodPointInverseMassY = OutInput.RodPointInverseMassZ = FVector::ZeroVector;
	OutInput.PhysicsStepSeconds = OutInput.PendingLineResponseSeconds = 0;
	OutInput.PendingLineImpulseNewtonSeconds = OutInput.PendingLinePositionMomentNewtonSecondsSquared = FVector::ZeroVector;
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



bool UCatFishingPhysicalRodComponent::BeginPrimaryHold(APlayerState* Player, const bool bPositionAtHand)
{
	ACatCharacter* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	UCatPhysicalBodyComponent* Physical = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
	UCatPhysicsGrabComponent* Grab = Physical ? Physical->GetGrab() : nullptr;
	if (!bReady || !GetOwner()->HasAuthority() || !Grab) return false;
	const auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (!Rod->GetPresentationState().bDeployed || Rod->GetPresentationState().bBroken
		|| (Rod->GetPresentationState().OperatorPlayerState && !Rod->IsPrimaryOperator(Player))) return false;
	ObserveGrab(Grab);
	if (IsHeldBy(Player)) return true;
	const bool bLeft = !Grab->IsGripping(true);
	if (Grab->IsGripping(bLeft)) return false;
	const FTransform PreviousPose = Body->GetComponentTransform();
	TGuardValue<bool> Preparing(bPreparingPrimaryHold, true);
	const FVector HandPoint = Physical->GetHand(bLeft)->GetComponentLocation();
	if (bPositionAtHand)
	{
		const FTransform DesiredGrip(Cat->GetActorQuat(), HandPoint);
		const FTransform ActorPose = GripLocalTransform.Inverse() * DesiredGrip;
		Body->SetWorldTransform(BodyLocal * ActorPose, false, nullptr, ETeleportType::ResetPhysics);
		RefreshObservedPose();
	}

	const FVector GripPoint = CastChecked<ACatFishingRodActor>(GetOwner())->GetGripWorldTransform().GetLocation();
	auto* Light = UCatLightPropComponent::FindFor(Body);
	if (Light) Light->SetParkedFromAuthority(false);
	if (Grab->GripFromAuthority(bLeft, Body, GripPoint)) return true;
	if (Light) Light->SetParkedFromAuthority(true);
	Body->SetWorldTransform(PreviousPose, false, nullptr, ETeleportType::TeleportPhysics);
	RefreshObservedPose();
	return false;
}

bool UCatFishingPhysicalRodComponent::CommitPrimaryHold(APlayerState* Player)
{
	auto* Rod = Cast<ACatFishingRodActor>(GetOwner());
	auto* Cat = Player ? Cast<ACatCharacter>(Player->GetPawn()) : nullptr;
	auto* Grab = Cat && Cat->GetPhysicalBodyComponent() ? Cat->GetPhysicalBodyComponent()->GetGrab() : nullptr;
	if (!Rod || !Rod->HasAuthority() || !Rod->IsPrimaryOperator(Player)
		|| !Grab) return false;
	// Retain and control publish grip callbacks; defer reconciliation until both flags are committed.
	TGuardValue<bool> Preparing(bPreparingPrimaryHold, true);
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
	// The detached fixed body is observed; the controlled body follows its real primary carrier.
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
	if (!bReady || !GetOwner()->HasAuthority() || bRefreshingPrimaryControl || bPreparingPrimaryHold) return;
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
	if (RequiresTargetedPickup() && !ControlledBody.IsValid()) ObserveEscapeAfterPhysics();
	AppliedLineImpulse += PendingPhysicsImpulse;
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
	// 墓碑（2026-09-14）：电机与转矩不再绿零清力量；设计修改记录 2026-09-13 裁决②，体力＝绿＋黄。
	const double Stamina = ASC->GetTotalFightStamina();
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double Force = Balance && FMath::IsFinite(Stamina) && Stamina > 0.0 && FMath::IsFinite(Strength)
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
	// A lost rod stays in its actual fall/drag pose until an explicit pickup commits.
	if (RequiresTargetedPickup() && !Next)
	{
		if (ControlledBody.IsValid())
			if (auto* Previous = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(); Previous && Previous->UsesCharacterMovement())
				GetOwner()->PrimaryActorTick.RemovePrerequisite(Previous, Previous->GetPostMovementTick());
		ControlledBody.Reset();
		return;
	}
	auto* Light = UCatLightPropComponent::FindFor(Body);
	if (Next == ControlledBody.Get() && !Body->IsSimulatingPhysics()
		&& (!Light || (Light->GetState().Mode == ECatLightPropMode::Parked) == (Next == nullptr))) return;
	UPrimitiveComponent* PreviousCarrier = ControlledBody.Get();
    if (ControlledBody.IsValid())
        if (auto* Previous = ControlledBody->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(); Previous && Previous->UsesCharacterMovement())
            GetOwner()->PrimaryActorTick.RemovePrerequisite(Previous, Previous->GetPostMovementTick());
	ControlledBody = Next;
    if (Next)
        if (auto* Current = Next->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>(); Current && Current->UsesCharacterMovement())
            GetOwner()->PrimaryActorTick.AddPrerequisite(Current, Current->GetPostMovementTick());
	ControlledAngularVelocity = FVector::ZeroVector;
	SmoothedFishPull = FVector::ZeroVector;
	Body->SetSimulatePhysics(false);
	Body->SetEnableGravity(false);
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
	else if (PreviousCarrier && !bEndingPlay && !Rod->IsActorBeingDestroyed()
		&& Rod->PresentationState.bDeployed && !Rod->PresentationState.bBroken)
	{
		ParkOnGroundFromAuthority(PreviousCarrier);
		RefreshObservedPose();
		Rod->ForceNetUpdate();
	}
	if (Light)
	{
		Light->SetParkedFromAuthority(Next == nullptr);
		Light->SetGripCarrierFromAuthority(Next);
	}
	if (!Next)
	{
		// A parked rod cannot be a hand anchor. Release through the existing grip authority/replication path.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			if (auto* OtherGrab = It->FindComponentByClass<UCatPhysicsGrabComponent>())
				OtherGrab->ReleaseTargetFromAuthority(Rod, TEXT("RodParked"));
	}
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_controlled_carrier RodActorId=%s PlayerId=%d Carrier=%s Controlled=%d Parked=%d PositionCm=%s RotationDegrees=%s SessionId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=%s"),
		*Rod->PresentationState.RodActorId.ToString(), Cat && Cat->GetPlayerState() ? Cat->GetPlayerState()->GetPlayerId() : INDEX_NONE,
		*GetNameSafe(Next), Next != nullptr, Next == nullptr, *Body->GetComponentLocation().ToCompactString(),
		*Body->GetComponentRotation().ToCompactString(), *LoadSessionId.ToString(), *GetNameSafe(GetWorld()),
		int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()), Next ? TEXT("PrimaryPoseAndPhysicalAssist") : TEXT("FixedSupportNoHandGrips"));
}

void UCatFishingPhysicalRodComponent::ParkOnGroundFromAuthority(UPrimitiveComponent* PreviousCarrier)
{
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	const FTransform HeldPose = GetObservedActorTransform();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FishingRodGroundPark), true);
	// Neither another player nor another parked rod is a ground support.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
	for (TActorIterator<ACatFishingRodActor> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
	const auto* PreviousPawn = Cast<APawn>(PreviousCarrier->GetOwner());
	const int32 PlayerId = PreviousPawn && PreviousPawn->GetPlayerState() ? PreviousPawn->GetPlayerState()->GetPlayerId() : INDEX_NONE;
	const FVector Candidates[] = {HeldPose.GetLocation(), PreviousCarrier->GetComponentLocation()};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Candidates); ++Index)
	{
		FHitResult Ground;
		const FVector Candidate = Candidates[Index];
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Candidate + FVector(0, 0, 100), Candidate - FVector(0, 0, 250), ECC_Visibility, Query)
			|| Ground.bStartPenetrating || Ground.ImpactNormal.Z < 0.7) continue;
		// The formal BP's origin is the butt; its local shaft already carries the authored upward tilt.
		// Keep the horizontal pointing direction, remove held pitch/roll, and seat that authored pose on the surface.
		FVector Heading = Rod->GetAuthoritativeRodForwardVector().GetSafeNormal2D();
		if (Heading.IsNearlyZero()) Heading = PreviousCarrier->GetForwardVector().GetSafeNormal2D();
		if (Heading.IsNearlyZero()) Heading = FVector::ForwardVector;
		const FRotator LocalHeading(0, BodyLocal.GetRotation().GetForwardVector().Rotation().Yaw, 0);
		const FQuat Rotation = FRotationMatrix::MakeFromZX(Ground.ImpactNormal, Heading).ToQuat() * LocalHeading.Quaternion().Inverse();
		Body->SetWorldTransform(BodyLocal * FTransform(Rotation, Ground.ImpactPoint, HeldPose.GetScale3D()), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_ground_parked RodActorId=%s PlayerId=%d SessionId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Support=%s PositionCm=%s Normal=%s Result=%s"),
			*Rod->PresentationState.RodActorId.ToString(), PlayerId, *LoadSessionId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
			int32(Rod->GetLocalRole()), *GetNameSafe(Ground.GetActor()), *Ground.ImpactPoint.ToCompactString(), *Ground.ImpactNormal.ToCompactString(),
			Index == 0 ? TEXT("RodButtGrounded") : TEXT("CarrierGroundFallback"));
		return;
	}
	UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_ground_park_rejected RodActorId=%s PlayerId=%d SessionId=%s World=%s NetMode=%d Authority=1 LocalRole=%d PositionCm=%s Reason=NoNearbyGround Result=PreviousFixedPosePreserved"),
		*Rod->PresentationState.RodActorId.ToString(), PlayerId, *LoadSessionId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
		int32(Rod->GetLocalRole()), *HeldPose.GetLocation().ToCompactString());
}

bool UCatFishingPhysicalRodComponent::RequiresTargetedPickup() const
{
	const auto* Rod = Cast<ACatFishingRodActor>(GetOwner());
	return Rod && Rod->GetPresentationState().EscapePhase != ECatFishingRodEscapePhase::None;
}

bool UCatFishingPhysicalRodComponent::OwnsEscapeHook(const ACatFishingHookActor* Hook) const
{
	return Hook && EscapeHook.Get() == Hook;
}

void UCatFishingPhysicalRodComponent::SetEscapePhase(const ECatFishingRodEscapePhase Phase)
{
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (Rod->PresentationState.EscapePhase == Phase) return;
	auto Next = Rod->PresentationState;
	Next.EscapePhase = Phase;
	if (Phase == ECatFishingRodEscapePhase::None) Next.EscapeSessionId.Invalidate();
	Rod->CommitAuthoritativeMutation(Next, Rod->PresentationState.RodActorRevision);
}

bool UCatFishingPhysicalRodComponent::FindEscapeGround(const FVector& Position, FHitResult& Hit) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RodEscapeGround), false, GetOwner());
	for (TActorIterator<APawn> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
	for (TActorIterator<ACatFishingRodActor> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
	return GetWorld()->LineTraceSingleByChannel(Hit, Position + FVector(0, 0, 10), Position - FVector(0, 0, 250), ECC_Visibility, Query)
		&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= 0.7;
}

bool UCatFishingPhysicalRodComponent::BeginBiteTimeoutEscape(const FGuid SessionId, const FVector& WaterTarget, ACatFishingHookActor* Hook)
{
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (!bReady || !Rod->HasAuthority() || !SessionId.IsValid() || RequiresTargetedPickup()
		|| !Rod->PresentationState.bDeployed || Rod->PresentationState.bBroken || WaterTarget.ContainsNaN()) return false;
	const auto* Settings = GetDefault<UCatFishingSettings>();
	for (const double Value : {Settings->RodEscapeThrowSpeed, Settings->RodEscapeThrowUpSpeed,
		Settings->RodEscapeDragSpeed, Settings->RodEscapeDragAcceleration, Settings->RodEscapeDragSeconds, Settings->RodEscapeMaximumTravel})
		if (!FMath::IsFinite(Value) || Value <= 0)
		{
			UE_LOG(LogCatFishing, Error, TEXT("Event=fishing_rod_escape_rejected SessionId=%s RodActorId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Reason=InvalidEscapeSettings"),
				*SessionId.ToString(), *Rod->PresentationState.RodActorId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Rod->GetLocalRole()));
			return false;
		}
	const bool bWasHeld = Rod->PresentationState.HolderPlayerState != nullptr;
	const FTransform InitialPose = Body->GetComponentTransform();
	EscapeDirection = (WaterTarget - Rod->GetGripWorldTransform().GetLocation()).GetSafeNormal2D();
	if (EscapeDirection.IsNearlyZero()) EscapeDirection = Body->GetForwardVector().GetSafeNormal2D();
	if (EscapeDirection.IsNearlyZero()) EscapeDirection = FVector::ForwardVector;
	EscapeSafePose = InitialPose;
	EscapePreviousPosition = InitialPose.GetLocation();
	EscapeTravel = 0;
	EscapeDragSpeedCommand = 0;
	EscapeStartedAt = GetWorld()->GetTimeSeconds();
	EscapeDragStartedAt = 0;
	EscapeHook = Hook;
	// Keep a reachable fallback at the bank even when the held grip initially overhangs water.
	const auto* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	const FVector InitialGrip = Rod->GetGripWorldTransform().GetLocation();
	for (const double Backoff : {0.0, 25.0, 50.0, 100.0, 150.0})
	{
		const FVector Candidate = InitialGrip - EscapeDirection * Backoff;
		FHitResult Ground;
		if (!FindEscapeGround(Candidate, Ground) || (Water && Water->DoesWorldDropSweepTouchWater(Candidate, Candidate, 2.0))) continue;
		if (Backoff > 0)
		{
			const FQuat Flat = FRotationMatrix::MakeFromXZ(EscapeDirection, Ground.ImpactNormal).ToQuat();
			EscapeSafePose = FTransform(Flat, Ground.ImpactPoint + EscapeDirection * Body->GetScaledBoxExtent().X
				+ Ground.ImpactNormal * (Body->GetScaledBoxExtent().Z + 2), InitialPose.GetScale3D());
		}
		break;
	}
	// Publish the escape guard before releasing any grip: callbacks must never auto-park this rod.
	auto Next = Rod->PresentationState;
	Next.EscapePhase = ECatFishingRodEscapePhase::Falling;
	Next.EscapeSessionId = SessionId;
	Rod->CommitAuthoritativeMutation(Next, Rod->PresentationState.RodActorRevision);
	ReleaseAllConnections(TEXT("TrueBiteTimeout"));
	RefreshPrimaryControl();
	if (auto* Light = UCatLightPropComponent::FindFor(Body))
	{
		Light->SetGripCarrierFromAuthority(nullptr);
		Light->SetParkedFromAuthority(false);
		Light->SetExternalLoadFromAuthority(true);
	}
	Body->SetWorldTransform(InitialPose, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false); // LightProp supplies 1/4 gravity; TickEscape supplies the remainder.
	Body->SetPhysicsLinearVelocity(bWasHeld ? EscapeDirection * Settings->RodEscapeThrowSpeed + FVector(0, 0, Settings->RodEscapeThrowUpSpeed) : FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInRadians(FVector::CrossProduct(FVector::UpVector, EscapeDirection) * (bWasHeld ? 1.5 : 0.8));
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_escape_started SessionId=%s RodActorId=%s ItemInstanceId=%s Held=%d PositionCm=%s VelocityCmS=%s MaxDragSpeedCmS=%.2f MaxTravelCm=%.2f World=%s NetMode=%d Authority=1 LocalRole=%d Result=PickupRequired"),
		*SessionId.ToString(), *Rod->PresentationState.RodActorId.ToString(), *Rod->PresentationState.ItemInstanceId.ToString(), bWasHeld,
		*InitialPose.GetLocation().ToCompactString(), *Body->GetPhysicsLinearVelocity().ToCompactString(), Settings->RodEscapeDragSpeed,
		Settings->RodEscapeMaximumTravel, *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Rod->GetLocalRole()));
	return true;
}

void UCatFishingPhysicalRodComponent::StopEscape(const FName Reason)
{
	if (EscapeHook.IsValid()) EscapeHook->Destroy();
	EscapeHook.Reset();
	if (!RequiresTargetedPickup()) return;
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (!ControlledBody.IsValid() && Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Falling)
	{
		const FVector SafeGrip = (BodyLocal.Inverse() * EscapeSafePose).TransformPosition(GripLocalTransform.GetLocation());
		FHitResult Ground;
		if (FindEscapeGround(SafeGrip, Ground))
		{
			const FQuat Flat = FRotationMatrix::MakeFromXZ(EscapeDirection, Ground.ImpactNormal).ToQuat();
			Body->SetWorldLocationAndRotation(Ground.ImpactPoint + EscapeDirection * Body->GetScaledBoxExtent().X
				+ Ground.ImpactNormal * (Body->GetScaledBoxExtent().Z + 2), Flat, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	if (Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		Body->SetSimulatePhysics(false);
	}
	if (auto* Light = UCatLightPropComponent::FindFor(Body))
	{
		Light->SetExternalLoadFromAuthority(false);
		Light->SetParkedFromAuthority(!ControlledBody.IsValid());
	}
	SetEscapePhase(ECatFishingRodEscapePhase::Stopped);
	RefreshObservedPose();
	Rod->ForceNetUpdate();
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_escape_stopped SessionId=%s RodActorId=%s TravelCm=%.3f PositionCm=%s Reason=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=%s"),
		*Rod->PresentationState.EscapeSessionId.ToString(), *Rod->PresentationState.RodActorId.ToString(), EscapeTravel,
		*Body->GetComponentLocation().ToCompactString(), *Reason.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Rod->GetLocalRole()),
		ControlledBody.IsValid() ? TEXT("PickedUp") : TEXT("PickupRequired"));
}

void UCatFishingPhysicalRodComponent::CompleteEscapePickup()
{
	if (!GetOwner()->HasAuthority() || !RequiresTargetedPickup() || !ControlledBody.IsValid()) return;
	StopEscape(TEXT("TargetedPickup"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	SetEscapePhase(ECatFishingRodEscapePhase::None);
}

void UCatFishingPhysicalRodComponent::ObserveEscapeAfterPhysics()
{
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Stopped) return;
	// Contact impulses can turn the last bit of falling rotation into horizontal speed.
	// Enforce the same drag ceiling after Chaos, including frames where the rod briefly bounces.
	if (Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Dragging)
	{
		const FVector Velocity = Body->GetPhysicsLinearVelocity();
		const FVector Horizontal = FVector(Velocity.X, Velocity.Y, 0).GetClampedToMaxSize(GetDefault<UCatFishingSettings>()->RodEscapeDragSpeed);
		Body->SetPhysicsLinearVelocity(FVector(Horizontal.X, Horizontal.Y, Velocity.Z));
	}
	const FVector Position = Body->GetComponentLocation();
	const FTransform ActorPose = GetObservedActorTransform();
	const FVector Grip = ActorPose.TransformPosition(GripLocalTransform.GetLocation());
	const FVector SafeGrip = (BodyLocal.Inverse() * EscapeSafePose).TransformPosition(GripLocalTransform.GetLocation());
	const auto* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	FHitResult Ground;
	const bool bHasGround = FindEscapeGround(Grip, Ground);
	const bool bWater = Water && Water->DoesWorldDropSweepTouchWater(SafeGrip, Grip, 2.0);
	const double Travel = FVector::Dist2D(EscapePreviousPosition, Position);
	if (!bHasGround || bWater || EscapeTravel + Travel > GetDefault<UCatFishingSettings>()->RodEscapeMaximumTravel)
	{
		Body->SetWorldTransform(EscapeSafePose, false, nullptr, ETeleportType::TeleportPhysics);
		// Finish on the last reachable support, including a rod initially held over the bank.
		FHitResult SafeGround;
		if (FindEscapeGround(SafeGrip, SafeGround))
		{
			const FQuat Flat = FRotationMatrix::MakeFromXZ(Body->GetForwardVector().GetSafeNormal2D(), SafeGround.ImpactNormal).ToQuat();
			const FVector Half = Body->GetScaledBoxExtent();
			Body->SetWorldLocationAndRotation(SafeGround.ImpactPoint + Flat.GetForwardVector() * Half.X + SafeGround.ImpactNormal * (Half.Z + 2), Flat, false, nullptr, ETeleportType::TeleportPhysics);
		}
		StopEscape(bWater ? TEXT("WaterBoundary") : !bHasGround ? TEXT("NoGroundAhead") : TEXT("TravelLimit"));
		return;
	}
	EscapeTravel += Travel;
	EscapePreviousPosition = Position;
	EscapeSafePose = Body->GetComponentTransform();
}

void UCatFishingPhysicalRodComponent::TickEscape(const float DeltaTime)
{
	auto* Rod = CastChecked<ACatFishingRodActor>(GetOwner());
	if (Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Stopped) return;
	const auto* Settings = GetDefault<UCatFishingSettings>();
	const double Now = GetWorld()->GetTimeSeconds();
	if (auto* Light = UCatLightPropComponent::FindFor(Body))
	{
		Light->SetParkedFromAuthority(false);
		Light->SetExternalLoadFromAuthority(true);
	}
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->AddForce(FVector(0, 0, GetWorld()->GetGravityZ() * (1.0 - UCatLightPropComponent::ReleasedGravityScale) * Body->GetMass()));
	FHitResult Ground;
	const FVector Position = Body->GetComponentLocation();
	const bool bTouchingGround = FindEscapeGround(Position, Ground)
		&& Position.Z - Ground.ImpactPoint.Z <= Body->Bounds.BoxExtent.Z + 4.0
		&& FMath::Abs(Body->GetForwardVector().Z) < 0.3;
	if (Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Falling && bTouchingGround)
	{
		EscapeDragStartedAt = Now;
		SetEscapePhase(ECatFishingRodEscapePhase::Dragging);
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_escape_grounded SessionId=%s RodActorId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=Dragging"),
			*Rod->PresentationState.EscapeSessionId.ToString(), *Rod->PresentationState.RodActorId.ToString(), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Rod->GetLocalRole()));
	}
	if (Now - EscapeStartedAt > 5.0 + Settings->RodEscapeDragSeconds
		|| (EscapeDragStartedAt > 0 && Now - EscapeDragStartedAt >= Settings->RodEscapeDragSeconds))
	{
		StopEscape(TEXT("TimeLimit"));
		return;
	}
	FVector Velocity = Body->GetPhysicsLinearVelocity();
	FVector Horizontal(Velocity.X, Velocity.Y, 0);
	if (Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Dragging && bTouchingGround)
	{
		// Ramp the drive command, not last frame's friction-reduced velocity; otherwise static friction
		// can erase each tiny increment and the rod never visibly drags. Contacts still block actual travel.
		EscapeDragSpeedCommand = FMath::Min(Settings->RodEscapeDragSpeed,
			EscapeDragSpeedCommand + Settings->RodEscapeDragAcceleration * FMath::Max(0.0f, DeltaTime));
		Horizontal = EscapeDirection * EscapeDragSpeedCommand;
	}
	else Horizontal = Horizontal.GetClampedToMaxSize(Rod->PresentationState.EscapePhase == ECatFishingRodEscapePhase::Dragging
		? Settings->RodEscapeDragSpeed : Settings->RodEscapeThrowSpeed);
	Body->SetPhysicsLinearVelocity(FVector(Horizontal.X, Horizontal.Y, FMath::Min(Velocity.Z, Settings->RodEscapeThrowUpSpeed)));
	Body->SetPhysicsAngularVelocityInRadians(Body->GetPhysicsAngularVelocityInRadians().GetClampedToMaxSize(3.0));
	if (EscapeHook.IsValid())
	{
		const double Distance = FVector::Distance(Rod->GetRodTipWorldTransform().GetLocation(), EscapeHook->GetActorLocation());
		EscapeHook->SetFishingLinePresentationFromAuthority(Distance, Distance, 0, 0, true);
	}
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
			|| ASC->GetTotalFightStamina() <= 0)
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
	if (!Rod->PresentationState.bDeployed || Rod->PresentationState.bBroken)
	{
		if (RequiresTargetedPickup() && Rod->PresentationState.EscapePhase != ECatFishingRodEscapePhase::Stopped) StopEscape(TEXT("RodUnavailable"));
		ReleaseAllConnections(TEXT("RodUnavailable"));
		return;
	}
	if (RequiresTargetedPickup() && !ControlledBody.IsValid()) { TickEscape(DeltaTime); return; }
	const UPhysicsSettings* Physics = UPhysicsSettings::Get();
	const double NetworkScale = GetWorld()->GetPhysicsScene() ? GetWorld()->GetPhysicsScene()->GetNetworkDeltaTimeScale() : 1.0;
	const double RequestedPhysicsSeconds = FMath::Max(0.0, double(DeltaTime) * NetworkScale);
	const double PhysicsLimit = Physics->bSubstepping ? Physics->MaxSubsteps * double(Physics->MaxSubstepDeltaTime) : double(Physics->MaxPhysicsDeltaTime);
	const double PhysicsSeconds = PhysicsLimit > 0 ? FMath::Min(RequestedPhysicsSeconds, PhysicsLimit) : RequestedPhysicsSeconds;
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
		// Without a primary, the fixed support absorbs this once-consumed load without moving the rod.
	}

}

void UCatFishingPhysicalRodComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (EscapeHook.IsValid()) EscapeHook->Destroy();
	EscapeHook.Reset();
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
