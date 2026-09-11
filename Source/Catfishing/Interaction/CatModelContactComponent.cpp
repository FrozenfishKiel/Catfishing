#include "Interaction/CatModelContactComponent.h"

#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "Chaos/ChaosEngineInterface.h"

UCatModelContactBody::UCatModelContactBody()
{
    SetMobility(EComponentMobility::Movable);
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetCollisionObjectType(ECC_WorldDynamic);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    SetGenerateOverlapEvents(false);
    CanCharacterStepUpOn = ECB_No;
    SetCanEverAffectNavigation(false);
}

void UCatModelContactBody::Initialize(USkeletalBodySetup* Setup) { AuthoredBody = Setup; }
FName UCatModelContactBody::GetBoneName() const { return AuthoredBody ? AuthoredBody->BoneName : NAME_None; }
UBodySetup* UCatModelContactBody::GetBodySetup() { return AuthoredBody; }
FBoxSphereBounds UCatModelContactBody::CalcBounds(const FTransform& LocalToWorld) const
{
    return AuthoredBody ? FBoxSphereBounds(AuthoredBody->AggGeom.CalcAABB(LocalToWorld))
        : FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0);
}

UCatModelContactComponent::UCatModelContactComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

bool UCatModelContactComponent::Initialize(UPoseableMeshComponent* FinalPose)
{
    if (HasModelContacts()) return Pose == FinalPose;
    const USkeletalMesh* Mesh = FinalPose ? Cast<USkeletalMesh>(FinalPose->GetSkinnedAsset()) : nullptr;
    UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
    // Respect the actual character Blueprint's PhysicsAssetOverride as well as the mesh default.
    auto* Visual = GetOwner()->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
    if (Visual && Visual->GetAnimationSource()) Asset = Visual->GetAnimationSource()->GetPhysicsAsset();
    if (!Asset)
    {
        if (Mesh) UE_LOG(LogCatPhysicsGrab, Warning, TEXT("Event=model_contact_unavailable World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Mesh=%s Reason=MissingPhysicsAsset Result=LegacyFallback"),
            *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Mesh));
        return false;
    }
    Pose = FinalPose;
    for (int32 Index = 0; Index < Asset->SkeletalBodySetups.Num(); ++Index)
    {
        auto* Setup = Asset->SkeletalBodySetups[Index].Get();
        if (!Setup || Setup->AggGeom.GetElementCount() == 0 || Pose->GetBoneIndex(Setup->BoneName) == INDEX_NONE) continue;
        const FName Name(*FString::Printf(TEXT("ModelContact_%d_%s"), Index, *Setup->BoneName.ToString()));
        auto* Contact = NewObject<UCatModelContactBody>(GetOwner(), Name);
        Contact->Initialize(Setup);
        GetOwner()->AddInstanceComponent(Contact);
        Contact->SetupAttachment(GetOwner()->GetRootComponent());
        Contact->SetWorldTransform(Pose->GetBoneTransformByName(Setup->BoneName, EBoneSpaces::WorldSpace));
        Contact->RegisterComponent();
        if (!Contact->GetBodyInstance()->IsValidBodyInstance())
        {
            UE_LOG(LogCatPhysicsGrab, Warning, TEXT("Event=model_contact_body_rejected World=%s NetMode=%d Authority=%d Actor=%s Bone=%s Reason=NoQueryGeometry"),
                *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(), *GetNameSafe(GetOwner()), *Setup->BoneName.ToString());
            Contact->DestroyComponent();
            continue;
        }
        Bodies.Add(Contact);
    }
    if (Visual)
        PrimaryComponentTick.AddPrerequisite(Visual, Visual->PrimaryComponentTick);
    SetComponentTickEnabled(HasModelContacts());
    if (HasModelContacts())
        if (auto* Cat = Cast<ACatCharacter>(GetOwner()))
            for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
                if (*It != Cat && UsesModelContacts(*It))
                {
                    Cat->GetCapsuleComponent()->IgnoreActorWhenMoving(*It, true);
                    It->GetCapsuleComponent()->IgnoreActorWhenMoving(Cat, true);
                }
    UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=model_contact_ready World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Mesh=%s PhysicsAsset=%s Bodies=%d Result=%s"),
        *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()),
        *GetNameSafe(GetOwner()), *GetNameSafe(Mesh), *GetNameSafe(Asset), Bodies.Num(), HasModelContacts() ? TEXT("ModelSurfaces") : TEXT("LegacyFallback"));
    return HasModelContacts();
}

void UCatModelContactComponent::RefreshPose()
{
    if (!Pose) return;
    for (UCatModelContactBody* Contact : Bodies)
        Contact->SetWorldTransform(Pose->GetBoneTransformByName(Contact->GetBoneName(), EBoneSpaces::WorldSpace),
            false, nullptr, ETeleportType::TeleportPhysics);
}

void UCatModelContactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    RefreshPose();
}

bool UCatModelContactComponent::UsesModelContacts(const AActor* Actor)
{
    const auto* Contacts = Actor ? Actor->FindComponentByClass<UCatModelContactComponent>() : nullptr;
    return Contacts && Contacts->HasModelContacts();
}

bool UCatModelContactComponent::IsLegacyContactProxy(const UPrimitiveComponent* Component)
{
    const auto* Cat = Component ? Cast<ACatCharacter>(Component->GetOwner()) : nullptr;
    if (!Cat || !UsesModelContacts(Cat)) return false;
    const auto* Body = Cat->GetPhysicalBodyComponent();
    return Component == Cat->GetCapsuleComponent() || Component == Body->GetBody()
        || Component == Body->GetHand(true) || Component == Body->GetHand(false);
}

bool UCatModelContactComponent::HasTractionConnectionWith(const UCatModelContactComponent* Other) const
{
    if (!Other) return false;
    for (const auto* Side : {this, Other})
    {
        const AActor* Peer = Side == this ? Other->GetOwner() : GetOwner();
        const auto* Grab = Side->GetOwner()->FindComponentByClass<UCatPhysicsGrabComponent>();
        const auto* PeerBody = Peer->FindComponentByClass<UCatPhysicalBodyComponent>();
        if (Grab) for (bool bLeft : {true, false})
            if (Grab->IsGripping(bLeft) && (Grab->GetGripTarget(bLeft) == Peer
                || (PeerBody && Grab->GetTractionReceiver(bLeft) == PeerBody))) return true;
    }
    return false;
}

bool UCatModelContactComponent::FindPeerContact(const UCatModelContactComponent* Other, FVector& Normal, double& SeparationTravelCm, double MarginCm) const
{
    Normal = FVector::ZeroVector; SeparationTravelCm = 0;
    if (!Other || !HasModelContacts() || !Other->HasModelContacts()) return false;
    const FVector Difference = Other->GetOwner()->GetActorLocation()-GetOwner()->GetActorLocation();
    const FVector Approach = Difference.GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER,
        GetOwner()->GetUniqueID() < Other->GetOwner()->GetUniqueID() ? FVector::ForwardVector : -FVector::ForwardVector);
    const FVector SurfaceDirection = FVector(Approach.X, Approach.Y, FMath::Clamp(Difference.Z / FMath::Max(1.0,Difference.Size2D()), -1.0, 1.0));
    const bool bConstrained = HasTractionConnectionWith(Other);
    const double Margin = !bConstrained && FMath::IsFinite(MarginCm) ? FMath::Clamp(MarginCm,0.0,3.0) : 0.0;
    // Authored surfaces gate contact; upright roots supply one stable pair axis.
    // Bone normals otherwise change with each walking pose and turn a straight push sideways.
    for (UCatModelContactBody* A : Bodies) for (UCatModelContactBody* B : Other->Bodies)
    {
        if (!A->Bounds.GetBox().ExpandBy(Margin*1.5).Intersect(B->Bounds.GetBox())) continue;
        const auto* BI = A->GetBodyInstance();
        const auto* OtherBI = B->GetBodyInstance();
        if (!BI || !OtherBI || !BI->IsValidBodyInstance() || !OtherBI->IsValidBodyInstance()) continue;
        FPhysicsCommand::ExecuteRead(BI->GetPhysicsActor(), OtherBI->GetPhysicsActor(),
            [&](const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)
        {
            PhysicsInterfaceTypes::FInlineShapeArray Shapes;
            FPhysicsInterface::GetAllShapes_AssumedLocked(ActorB, Shapes);
            for (const auto& Shape : Shapes)
            {
                if (!Shape.GetGeometry().IsConvex()) continue;
                FMTDResult MTD;
                if (FPhysicsInterface::Overlap_Geom(BI, FPhysicsInterface::GetGeometryCollection(Shape),
                    FTransform(B->GetComponentQuat(), B->GetComponentLocation()), &MTD))
                {
                    const double HorizontalNormal = FMath::Abs(FVector::DotProduct(MTD.Direction, SurfaceDirection));
                    // Estimate horizontal travel along the supported ground direction, including
                    // the height change between two roots standing on a slope.
                    if (MTD.Distance <= 0) continue;
                    const double HorizontalDepth = bConstrained ? MTD.Distance * MTD.Direction.Size2D()
                        : MTD.Distance / FMath::Max(.2, HorizontalNormal);
                    if (HorizontalDepth > SeparationTravelCm)
                    {
                        SeparationTravelCm = HorizontalDepth;
                        Normal = bConstrained ? MTD.Direction.GetSafeNormal2D() : Approach;
                    }
                }
                else if (Margin > 0 && Normal.IsNearlyZero()
                    && FPhysicsInterface::Overlap_Geom(BI, FPhysicsInterface::GetGeometryCollection(Shape),
                        FTransform(B->GetComponentQuat(), B->GetComponentLocation()-SurfaceDirection*Margin), &MTD))
                {
                    // This is contact persistence only, never penetration or a positional correction.
                    Normal = Approach;
                }
            }
        });
    }
    return !Normal.IsNearlyZero();
}

void UCatModelContactComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld() && GetOwner())
    {
    for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
        It->GetCapsuleComponent()->IgnoreActorWhenMoving(GetOwner(), false);
    if (GetOwner()->HasAuthority())
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
            if (auto* Grab = It->FindComponentByClass<UCatPhysicsGrabComponent>()) Grab->ReleaseTargetFromAuthority(GetOwner(), TEXT("ModelContactEndPlay"));
    }
    for (UCatModelContactBody* Contact : Bodies) if (IsValid(Contact)) Contact->DestroyComponent();
    Bodies.Reset(); Pose = nullptr;
    Super::EndPlay(Reason);
}
