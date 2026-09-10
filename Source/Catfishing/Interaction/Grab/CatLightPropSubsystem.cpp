#include "Interaction/Grab/CatLightPropSubsystem.h"

#include "Chaos/ContactModification.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/SimCallbackObject.h"
#include "CollisionQueryParams.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Engine/World.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#include <atomic>

namespace
{
	struct FParticleRolesInput : Chaos::FSimCallbackInput
	{
		TMap<int32, bool> Roles;
		uint64 Revision = 0;
		void Reset() { Roles.Reset(); Revision = 0; }
	};
}

/** Only particle IDs cross the thread boundary. The callback never reads an Actor/component. */
class FCatLightPropContactCallback : public Chaos::TSimCallbackObject<FParticleRolesInput,
	Chaos::FSimCallbackNoOutput, Chaos::ESimCallbackOptions::Presimulate
	| Chaos::ESimCallbackOptions::ContactModification | Chaos::ESimCallbackOptions::ParticleUnregister>
{
public:
	std::atomic<uint64> ModifiedContacts{0};
	std::atomic<int32> LastCatParticle{INDEX_NONE};
	std::atomic<int32> LastPropParticle{INDEX_NONE};
private:
	TMap<int32, bool> Roles;
	uint64 AppliedRevision = 0;
	virtual void OnPreSimulate_Internal() override
	{
		if (const FParticleRolesInput* Input = GetConsumerInput_Internal(); Input && Input->Revision > AppliedRevision)
		{
			Roles = Input->Roles;
			AppliedRevision = Input->Revision;
		}
	}
	virtual void OnParticleUnregistered_Internal(TArray<TTuple<Chaos::FUniqueIdx, Chaos::FSingleParticlePhysicsProxy*>>& Removed) override
	{
		for (const auto& Entry : Removed) Roles.Remove(Entry.Get<0>().Idx);
	}
	virtual void OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifier) override
	{
		for (Chaos::FContactPairModifier& Pair : Modifier)
		{
			const auto Particles = Pair.GetParticlePair();
			if (!Particles[0] || !Particles[1]) continue;
			const int32 Id0 = Particles[0]->UniqueIdx().Idx;
			const int32 Id1 = Particles[1]->UniqueIdx().Idx;
			const bool* Light0 = Roles.Find(Id0);
			const bool* Light1 = Roles.Find(Id1);
			if (!Light0 || !Light1 || *Light0 == *Light1) continue;
			const int32 CatIndex = *Light0 ? 1 : 0;
			Pair.ModifyInvMassScale(0, CatIndex);
			Pair.ModifyInvInertiaScale(0, CatIndex);
			LastCatParticle.store(CatIndex == 0 ? Id0 : Id1, std::memory_order_relaxed);
			LastPropParticle.store(CatIndex == 0 ? Id1 : Id0, std::memory_order_relaxed);
			ModifiedContacts.fetch_add(1, std::memory_order_relaxed);
		}
	}
};

bool UCatLightPropSubsystem::DoesSupportWorldType(const EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE || Type == EWorldType::GamePreview;
}

void UCatLightPropSubsystem::RegisterCatPart(UPrimitiveComponent* Component) { RegisterBody(Component, false); }
void UCatLightPropSubsystem::RegisterLightProp(UPrimitiveComponent* Component) { RegisterBody(Component, true); }

void UCatLightPropSubsystem::RegisterBody(UPrimitiveComponent* Component, const bool bLightProp)
{
	if (!IsValid(Component) || Component->GetWorld() != GetWorld() || bShuttingDown) return;
	RegisteredBodies.Add(Component, bLightProp);
	UnavailableBodies.Remove(Component);
	Component->OnComponentPhysicsStateChanged.AddUniqueDynamic(this, &ThisClass::HandlePhysicsStateChanged);
	PublishParticleRoles();
}

void UCatLightPropSubsystem::UnregisterBody(UPrimitiveComponent* Component)
{
	if (!Component) return;
	Component->OnComponentPhysicsStateChanged.RemoveDynamic(this, &ThisClass::HandlePhysicsStateChanged);
	RegisteredBodies.Remove(Component);
	UnavailableBodies.Remove(Component);
	PublishParticleRoles();
}

void UCatLightPropSubsystem::HandlePhysicsStateChanged(UPrimitiveComponent* Component, const EComponentPhysicsStateChange Change)
{
	if (Change == EComponentPhysicsStateChange::Destroyed) UnavailableBodies.Add(Component);
	else UnavailableBodies.Remove(Component);
	PublishParticleRoles();
}

void UCatLightPropSubsystem::PublishParticleRoles()
{
	if (bShuttingDown || !GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	if (!Scene || !Scene->GetSolver()) return;
	if (!ContactCallback)
	{
		ContactCallback = Scene->GetSolver()->CreateAndRegisterSimCallbackObject_External<FCatLightPropContactCallback>();
		UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_light_prop_contact_policy_started World=%s NetMode=%d Authority=1 Result=OneWayPropCatContacts"),
			*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
	}
	FParticleRolesInput* Input = ContactCallback->GetProducerInputData_External();
	Input->Roles.Reset();
	Input->Revision = ++PublishRevision;
	for (auto It = RegisteredBodies.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Component = It.Key().Get();
		if (!IsValid(Component)) { It.RemoveCurrent(); continue; }
		if (UnavailableBodies.Contains(Component)) continue;
		FBodyInstance* Instance = Component->GetBodyInstance();
		if (!Instance || !Instance->ActorHandle) continue;
		const Chaos::FUniqueIdx ParticleId = Instance->ActorHandle->GetGameThreadAPI().UniqueIdx();
		if (!ParticleId.IsValid()) continue;
		Instance->SetContactModification(true);
		Input->Roles.Add(ParticleId.Idx, It.Value());
		if (!PublishedRoles.Contains(ParticleId.Idx))
		{
			const auto* Cat = Component->GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
			const auto* Prop = Component->GetOwner()->FindComponentByClass<UCatLightPropComponent>();
			UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_light_prop_particle_registered World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s Component=%s BodyId=%s PropId=%s ParticleId=%d LightProp=%d Revision=%llu Result=Registered"),
				*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Component->GetOwner()->GetLocalRole()),
				*GetNameSafe(Component->GetOwner()), *GetNameSafe(Component), Cat ? *Cat->GetBodyId().ToString() : TEXT("None"),
				Prop ? *Prop->GetState().PropId.ToString() : TEXT("None"), ParticleId.Idx, It.Value(), PublishRevision);
		}
	}
	for (const auto& Previous : PublishedRoles)
		if (!Input->Roles.Contains(Previous.Key))
			UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_light_prop_particle_removed World=%s NetMode=%d Authority=1 ParticleId=%d Revision=%llu Result=Removed"),
				*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), Previous.Key, PublishRevision);
	PublishedRoles = Input->Roles;
}

void UCatLightPropSubsystem::AppendSupportQueryIgnores(FCollisionQueryParams& Params) const
{
	for (const auto& Entry : RegisteredBodies)
		if (Entry.Value && Entry.Key.IsValid()) Params.AddIgnoredComponent(Entry.Key.Get());
}

uint64 UCatLightPropSubsystem::GetModifiedContactCount() const
{
	return ContactCallback ? ContactCallback->ModifiedContacts.load(std::memory_order_relaxed) : 0;
}

void UCatLightPropSubsystem::Tick(float DeltaTime)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const uint64 Count = GetModifiedContactCount();
	if (Count == LastLoggedContactCount || Now < NextContactLogSeconds) return;
	UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=physics_light_prop_contacts_modified World=%s NetMode=%d Authority=1 CatParticleId=%d PropParticleId=%d Pairs=%llu TotalPairs=%llu CatInvMassScale=0 CatInvInertiaScale=0 Result=PropYields"),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), ContactCallback->LastCatParticle.load(std::memory_order_relaxed),
		ContactCallback->LastPropParticle.load(std::memory_order_relaxed), Count - LastLoggedContactCount, Count);
	LastLoggedContactCount = Count;
	NextContactLogSeconds = Now + 1.0;
}

TStatId UCatLightPropSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UCatLightPropSubsystem, STATGROUP_Tickables); }

void UCatLightPropSubsystem::Deinitialize()
{
	bShuttingDown = true;
	for (const auto& Entry : RegisteredBodies)
		if (Entry.Key.IsValid()) Entry.Key->OnComponentPhysicsStateChanged.RemoveDynamic(this, &ThisClass::HandlePhysicsStateChanged);
	RegisteredBodies.Reset(); UnavailableBodies.Reset();
	if (ContactCallback)
	{
		if (FPhysScene* Scene = GetWorld()->GetPhysicsScene(); Scene && Scene->GetSolver())
			Scene->GetSolver()->UnregisterAndFreeSimCallbackObject_External(ContactCallback);
		ContactCallback = nullptr;
	}
	Super::Deinitialize();
}
