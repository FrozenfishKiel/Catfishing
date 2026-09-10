#include "Interaction/Grab/CatPhysicsGrabProp.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatPhysicsPrototypeProp, Log, All);

ACatPhysicsGrabProp::ACatPhysicsGrabProp()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetNetUpdateFrequency(30.0f);
	PhysicsMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysicsMesh"));
	LightProp = CreateDefaultSubobject<UCatLightPropComponent>(TEXT("LightProp"));
	SetRootComponent(PhysicsMesh);
	PhysicsMesh->SetMobility(EComponentMobility::Movable);
	PhysicsMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicsMesh->SetCollisionObjectType(ECC_WorldStatic);
	PhysicsMesh->SetCollisionResponseToAllChannels(ECR_Block);
	PhysicsMesh->SetLinearDamping(0.25f);
	PhysicsMesh->SetAngularDamping(0.5f);
	PhysicsMesh->SetUseCCD(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rod(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BlockMesh = Block.Object;
	RodMesh = Rod.Object;
	BaseMaterial = Material.Object;
	PhysicsMesh->SetStaticMesh(BlockMesh);
	PhysicsMesh->SetMaterial(0, BaseMaterial);
	Tags.Add(TEXT("CatPhysicsPrototype"));
}

bool ACatPhysicsGrabProp::ConfigureFromAuthority(const FVector& DimensionsCentimeters, const bool bDynamic,
	const float MassKilograms, const FLinearColor& Color, const bool bRod)
{
	if (!HasAuthority() || DimensionsCentimeters.ContainsNaN() || DimensionsCentimeters.GetMin() <= 0.0
		|| !FMath::IsFinite(MassKilograms) || MassKilograms <= 0.0f)
	{
		UE_LOG(LogCatPhysicsPrototypeProp, Warning,
			TEXT("Event=physics_prototype_prop_rejected Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=InvalidConfiguration"),
			*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
		return false;
	}
	Configuration.DimensionsCentimeters = DimensionsCentimeters;
	Configuration.bDynamic = bDynamic;
	Configuration.MassKilograms = MassKilograms;
	Configuration.Color = Color;
	Configuration.bRod = bRod;
	ApplyConfiguration();
	ResetTransform = BodyTransform = GetActorTransform();
	ForceNetUpdate();
	return true;
}

void ACatPhysicsGrabProp::ApplyConfiguration()
{
	// A prop has one body. Mesh/scale changes occur only during setup, never during a grip solve.
	LightProp->RestoreOrdinaryPhysics();
	PhysicsMesh->SetSimulatePhysics(false);
	PhysicsMesh->SetStaticMesh(Configuration.bRod ? RodMesh : BlockMesh);
	SetActorScale3D(Configuration.DimensionsCentimeters / 100.0);
	PhysicsMesh->SetCollisionObjectType(Configuration.bDynamic ? ECC_PhysicsBody : ECC_WorldStatic);
	PhysicsMesh->SetMassOverrideInKg(NAME_None, Configuration.MassKilograms);
	if (!ColorMaterial && BaseMaterial)
		ColorMaterial = PhysicsMesh->CreateDynamicMaterialInstance(0, BaseMaterial);
	FLinearColor ExistingColor;
	if (ColorMaterial && ColorMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), ExistingColor))
	{
		PhysicsMesh->SetMaterial(0, ColorMaterial);
		ColorMaterial->SetVectorParameterValue(TEXT("Color"), Configuration.Color);
	}
	else if (!bMaterialWarningReported)
	{
		bMaterialWarningReported = true;
		UE_LOG(LogCatPhysicsPrototypeProp, Warning,
			TEXT("Event=physics_prototype_prop_material_unavailable Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=MissingColorParameter"),
			*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
	}
	PhysicsMesh->SetSimulatePhysics(Configuration.bDynamic && HasAuthority());
	if (Configuration.bDynamic) LightProp->Initialize(PhysicsMesh);
}

void ACatPhysicsGrabProp::BeginPlay()
{
	Super::BeginPlay();
	ApplyConfiguration();
	ResetTransform = GetActorTransform();
	if (HasAuthority()) BodyTransform = ResetTransform;
	UE_LOG(LogCatPhysicsPrototypeProp, Log,
		TEXT("Event=physics_prototype_prop_ready Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Dynamic=%d Simulating=%d MassKg=%.3f Result=Ready"),
		*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()),
		Configuration.bDynamic, PhysicsMesh->IsSimulatingPhysics(), Configuration.MassKilograms);
}

void ACatPhysicsGrabProp::OnRep_Configuration()
{
	ApplyConfiguration();
	// Property notification ordering is not a contract: rebuild geometry at the last received pose.
	if (bHasReceivedBodyTransform) OnRep_BodyTransform();
}

void ACatPhysicsGrabProp::OnRep_BodyTransform()
{
	const bool bFirstSnapshot = !bHasReceivedBodyTransform;
	// Static props do not tick an interpolation path. Every authoritative static pose applies immediately.
	if (!Configuration.bDynamic || bFirstSnapshot
		|| FVector::DistSquared(GetActorLocation(), BodyTransform.GetLocation()) > FMath::Square(200.0))
	{
		SetActorLocationAndRotation(BodyTransform.GetLocation(), BodyTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	}
	bHasReceivedBodyTransform = true;
	if (bFirstSnapshot)
	{
		UE_LOG(LogCatPhysicsPrototypeProp, Log,
			TEXT("Event=physics_prototype_prop_pose_received Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Location=%s Collision=%d Simulating=%d Result=Initialized"),
			*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()),
			*GetActorLocation().ToCompactString(), int32(PhysicsMesh->GetCollisionEnabled()), PhysicsMesh->IsSimulatingPhysics());
	}
}

void ACatPhysicsGrabProp::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Configuration.bDynamic) return;
	if (HasAuthority())
	{
		if (GetActorLocation().Z < -200.0) ResetFromAuthority();
		SnapshotElapsedSeconds += DeltaSeconds;
		if (SnapshotElapsedSeconds >= 1.0f / 30.0f)
		{
			SnapshotElapsedSeconds = 0.0f;
			BodyTransform = GetActorTransform();
		}
	}
	else if (bHasReceivedBodyTransform)
	{
		const FVector Location = FMath::VInterpTo(GetActorLocation(), BodyTransform.GetLocation(), DeltaSeconds, 18.0f);
		const FQuat Rotation = FMath::QInterpTo(GetActorQuat(), BodyTransform.GetRotation(), DeltaSeconds, 18.0f);
		SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ACatPhysicsGrabProp::ResetFromAuthority()
{
	if (!HasAuthority() || !Configuration.bDynamic) return;
	// Remove every incoming grip before teleporting, including hands on another player's body.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (UCatPhysicsGrabComponent* Grab = It->FindComponentByClass<UCatPhysicsGrabComponent>())
			Grab->ReleaseTargetFromAuthority(this, TEXT("PropReset"));
	}
	SetActorTransform(ResetTransform, false, nullptr, ETeleportType::TeleportPhysics);
	PhysicsMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	PhysicsMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	PhysicsMesh->WakeAllRigidBodies();
	BodyTransform = ResetTransform;
	ForceNetUpdate();
	UE_LOG(LogCatPhysicsPrototypeProp, Log,
		TEXT("Event=physics_prototype_prop_reset Actor=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=Reset"),
		*GetName(), *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
}

void ACatPhysicsGrabProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Configuration);
	DOREPLIFETIME(ThisClass, BodyTransform);
}
