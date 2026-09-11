#include "Interaction/Grab/CatLightPropComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/Grab/CatLightPropSubsystem.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Net/UnrealNetwork.h"

UCatLightPropComponent::UCatLightPropComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UCatLightPropComponent::Initialize(UPrimitiveComponent* InBody)
{
	if (!IsValid(InBody) || InBody->GetOwner() != GetOwner() || bEndingPlay) return;
	if (Body && Body != InBody) return;
	if (!Body)
	{
		Body = InBody;
		OriginalLinearDamping = Body->GetLinearDamping();
		OriginalAngularDamping = Body->GetAngularDamping();
	}
	if (GetOwner()->HasAuthority() && !State.PropId.IsValid()) State.PropId = FGuid::NewGuid();
	if (auto* Policy = GetWorld()->GetSubsystem<UCatLightPropSubsystem>()) Policy->RegisterLightProp(Body);
	if (GetOwner()->HasAuthority())
	{
		RefreshGripsFromAuthority(TEXT("Initialized"));
		if (State.Mode == ECatLightPropMode::Inactive) RefreshMode(TEXT("Initialized"));
		else ApplyPhysicsPolicy();
	}
	LogState(TEXT("physics_light_prop_ready"), TEXT("Registered"));
}

void UCatLightPropComponent::RestoreOrdinaryPhysics()
{
	if (!Body) return;
	SetGripCarrierFromAuthority(nullptr);
	if (auto* Policy = GetWorld() ? GetWorld()->GetSubsystem<UCatLightPropSubsystem>() : nullptr) Policy->UnregisterBody(Body);
	if (GetOwner()->HasAuthority())
	{
		Body->SetEnableGravity(true);
		Body->SetLinearDamping(OriginalLinearDamping);
		Body->SetAngularDamping(OriginalAngularDamping);
		State.GripCount = 0;
		State.bExternalLoad = false;
		bParked = false;
		State.Mode = ECatLightPropMode::Inactive;
		++State.Revision;
		GetOwner()->ForceNetUpdate();
	}
	LogState(TEXT("physics_light_prop_disabled"), TEXT("OrdinaryPhysicsRestored"));
	Body = nullptr;
}

UCatLightPropComponent* UCatLightPropComponent::FindFor(const UPrimitiveComponent* Target)
{
	if (!IsValid(Target) || !Target->GetOwner()) return nullptr;
	auto* Policy = Target->GetOwner()->FindComponentByClass<UCatLightPropComponent>();
	return Policy && Policy->Body == Target && !Policy->bEndingPlay ? Policy : nullptr;
}

void UCatLightPropComponent::RefreshGripsFromAuthority(const FName Reason)
{
	if (!Body || !GetOwner()->HasAuthority() || bEndingPlay) return;
	int32 Count = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (auto* Grab = It->FindComponentByClass<UCatPhysicsGrabComponent>())
			for (const bool bLeft : {true, false})
				if (Grab->GetGripTargetComponent(bLeft) == Body) ++Count;
	}
	if (Count == State.GripCount) return;
	State.GripCount = Count;
	RefreshMode(Reason);
}

void UCatLightPropComponent::SetExternalLoadFromAuthority(const bool bActive)
{
	if (!GetOwner()->HasAuthority() || bEndingPlay || State.bExternalLoad == bActive) return;
	State.bExternalLoad = bActive;
	RefreshMode(TEXT("ExternalLoadChanged"));
}

void UCatLightPropComponent::SetParkedFromAuthority(const bool bNewParked)
{
	if (!GetOwner()->HasAuthority() || bEndingPlay || bParked == bNewParked) return;
	bParked = bNewParked;
	RefreshMode(TEXT("ParkedSupportChanged"));
}

void UCatLightPropComponent::SetGripCarrierFromAuthority(UPrimitiveComponent* Carrier)
{
	if (!GetOwner()->HasAuthority() || GripCarrier.Get() == Carrier) return;
	GripCarrier = Carrier;
	RefreshGripConstraintsFromAuthority();
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_light_prop_carrier_changed World=%s NetMode=%d Authority=1 LocalRole=%d PropId=%s Actor=%s Carrier=%s Component=%s Result=ExistingGripsRebound"),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(GetOwner()->GetLocalRole()),
		*State.PropId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(Carrier ? Carrier->GetOwner() : nullptr), *GetNameSafe(Carrier));
}

void UCatLightPropComponent::RefreshGripConstraintsFromAuthority()
{
	if (!Body || !GetOwner()->HasAuthority()) return;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (auto* Grab = It->FindComponentByClass<UCatPhysicsGrabComponent>())
			Grab->RefreshTargetConstraintsFromAuthority(Body);
}

void UCatLightPropComponent::RefreshMode(const FName Reason)
{
	State.Mode = bParked ? ECatLightPropMode::Parked : State.GripCount > 0 ? ECatLightPropMode::Held
		: State.bExternalLoad ? ECatLightPropMode::Loaded : ECatLightPropMode::Falling;
	++State.Revision;
	ApplyPhysicsPolicy();
	LogState(TEXT("physics_light_prop_mode_changed"), Reason);
	GetOwner()->ForceNetUpdate();
}

void UCatLightPropComponent::ApplyPhysicsPolicy()
{
	if (!Body || !GetOwner()->HasAuthority()) return;
	// Gravity is applied once, below, with the selected scale. Mass/inertia and grip joints are unchanged.
	Body->SetEnableGravity(false);
	const bool bGentleFall = State.Mode == ECatLightPropMode::Falling;
	Body->SetLinearDamping(bGentleFall ? FallingLinearDamping : OriginalLinearDamping);
	Body->SetAngularDamping(bGentleFall ? FallingAngularDamping : OriginalAngularDamping);
	Body->WakeAllRigidBodies();
}

void UCatLightPropComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!Body || !GetOwner()->HasAuthority() || bEndingPlay || !Body->IsSimulatingPhysics()) return;
	if (State.Mode == ECatLightPropMode::Falling || State.Mode == ECatLightPropMode::Loaded)
		Body->AddForce(FVector(0, 0, GetWorld()->GetGravityZ() * ReleasedGravityScale * Body->GetMass()));
}

void UCatLightPropComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCatLightPropComponent, State);
}

void UCatLightPropComponent::OnRep_State() { LogState(TEXT("physics_light_prop_state_received"), TEXT("AuthorityObservation")); }

void UCatLightPropComponent::LogState(const FName Event, const FName Reason) const
{
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Component=%s PropId=%s Revision=%u GripCount=%d Mode=%d ExternalLoad=%d GravityScale=%.2f Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : INDEX_NONE,
		GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Body),
		*State.PropId.ToString(), State.Revision, State.GripCount, int32(State.Mode), State.bExternalLoad,
		State.Mode == ECatLightPropMode::Inactive ? 1.0f : (State.Mode == ECatLightPropMode::Held || State.Mode == ECatLightPropMode::Parked) ? 0.0f : ReleasedGravityScale, *Reason.ToString());
}

void UCatLightPropComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (auto* Policy = GetWorld() ? GetWorld()->GetSubsystem<UCatLightPropSubsystem>() : nullptr) Policy->UnregisterBody(Body);
	Super::EndPlay(EndPlayReason);
}
