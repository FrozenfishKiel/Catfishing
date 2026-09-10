#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
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
	LeftHand=CreateDefaultSubobject<USphereComponent>(TEXT("LeftPhysicsHand"));
	RightHand=CreateDefaultSubobject<USphereComponent>(TEXT("RightPhysicsHand"));
	LeftHand->SetupAttachment(Body); RightHand->SetupAttachment(Body);
	UCatPhysicalBodyComponent::ConfigureGeometry(Body,LeftHand,RightHand);
	LeftArm = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LeftShoulder"));
	RightArm = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("RightShoulder"));
	LeftArm->SetupAttachment(Body);
	RightArm->SetupAttachment(Body);
	Grab = CreateDefaultSubobject<UCatPhysicsGrabComponent>(TEXT("PhysicsGrab"));
	PhysicalBody=CreateDefaultSubobject<UCatPhysicalBodyComponent>(TEXT("PhysicalBody"));
	Visual = CreateDefaultSubobject<UCatPhysicsPrototypeVisualComponent>(TEXT("PrototypeVisual"));
}


void ACatPhysicsPrototypePawn::BeginPlay()
{
	Super::BeginPlay(); SpawnTransform=GetActorTransform();
	PhysicalBody->Initialize(Body,LeftHand,RightHand,LeftArm,RightArm,Grab);
	PhysicalBody->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
	Visual->InitializeVisual(Body,LeftHand,RightHand);
}
void ACatPhysicsPrototypePawn::SetPrototypeInput(FVector2D Move,FRotator View)
{
	PhysicalBody->SetViewIntent(View);
	const FRotator Yaw(0,View.Yaw,0);
	PhysicalBody->SetMoveIntent(Yaw.Vector()*Move.X+FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*Move.Y);
}
void ACatPhysicsPrototypePawn::SetGrabInput(bool bLeft,bool bHeld) { Grab->SetGrabInput(bLeft,bHeld); }
void ACatPhysicsPrototypePawn::RequestJump() { PhysicalBody->RequestJump(); }
void ACatPhysicsPrototypePawn::RequestReset()
{
	if (HasAuthority()) PhysicalBody->TeleportBodyFromAuthority(SpawnTransform,TEXT("PrototypeReset"));
	else if (IsLocallyControlled()) ServerRequestReset(PhysicalBody->GetControlEpoch());
}
void ACatPhysicsPrototypePawn::ServerRequestReset_Implementation(uint32 Epoch) { if (GetController() && Epoch==PhysicalBody->GetControlEpoch()) RequestReset(); }
FVector ACatPhysicsPrototypePawn::GetVelocity() const { return PhysicalBody ? PhysicalBody->GetVelocity() : FVector::ZeroVector; }
void ACatPhysicsPrototypePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && GetActorLocation().Z < -250) RequestReset();
	Visual->SetHandReachState(Grab->IsReaching(true),Grab->IsReaching(false));
	if (bShowDiagnostics && GetNetMode()!=NM_DedicatedServer)
	{
		for (int32 Index=0;Index<2;++Index)
		{
			const bool bLeft=Index==0;
			if (!Grab->IsReaching(bLeft)) continue;
			const FVector HandPosition=(bLeft ? LeftHand : RightHand)->GetComponentLocation();
			const FColor Color=Grab->IsGripping(bLeft)?FColor::Green:FColor::Cyan;
			DrawDebugSphere(GetWorld(),HandPosition,(bLeft ? LeftHand : RightHand)->GetScaledSphereRadius(),10,Color,false,-1,0,0.25f);
			DrawDebugLine(GetWorld(),Grab->GetShoulderWorldLocation(bLeft),HandPosition,Color,false,-1,0,0.25f);
			if (Grab->IsGripping(bLeft)) DrawDebugPoint(GetWorld(),Grab->GetGripWorldLocation(bLeft),6,FColor::Yellow);
		}
	}
}
void ACatPhysicsPrototypePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController); PhysicalBody->BeginControlEpochFromAuthority();
}
void ACatPhysicsPrototypePawn::UnPossessed()
{
	PhysicalBody->ReleaseConnectionsFromAuthority(TEXT("Unpossessed")); PhysicalBody->BeginControlEpochFromAuthority(); Super::UnPossessed();
}
void ACatPhysicsPrototypePawn::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	PhysicalBody->ReleaseConnectionsFromAuthority(TEXT("EndPlay")); Super::EndPlay(EndPlayReason);
}
void ACatPhysicsPrototypePawn::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	(void)DeltaTime;
	const FRotator CameraRotation = IsLocallyControlled() ? GetPrototypeView() : FRotator(-15.0, GetActorRotation().Yaw, 0.0);
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
