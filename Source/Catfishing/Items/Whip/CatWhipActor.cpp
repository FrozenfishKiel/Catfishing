#include "Items/Whip/CatWhipActor.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Inventory/CatInventoryComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Controller.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

ACatWhipActor::ACatWhipActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    PickupCollision->SetBoxExtent(FVector(60,5,5));
    SkeletalMesh->SetRelativeLocation(FVector(-50,0,0));
    SkeletalMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SkeletalMesh->bEnableUpdateRateOptimizations = false;
}

void ACatWhipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ThisClass, SwingOwner);
    DOREPLIFETIME(ThisClass, SwingRequestId);
    DOREPLIFETIME(ThisClass, SourceItemId);
    DOREPLIFETIME(ThisClass, SwingStartServerTime);
    DOREPLIFETIME(ThisClass, SwingYaw);
}

void ACatWhipActor::InitializeActorSpawnConfig()
{
    Super::InitializeActorSpawnConfig();
    // The ground mesh is a fixed rest pose; hidden inventory carriers never sweep.
    if (!bPresentationStarted)
    {
        SkeletalMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        SkeletalMesh->SetAnimation(nullptr);
    }
}

FCatInventoryReceiveBatch ACatWhipActor::GetPickupInventory() const
{
    if (SwingOwner) return {};
    if (bHasInventoryPayload) return Super::GetPickupInventory();
    FCatInventoryReceiveBatch Batch;
    if (ItemDefinition)
    {
        auto& Row = Batch.DefinitionEntries.AddDefaulted_GetRef();
        Row.ItemDefinition = ItemDefinition; Row.Count = 1;
    }
    return Batch;
}

bool ACatWhipActor::CanInteract_Implementation(AController* Controller) const
{
    return !SwingOwner && Super::CanInteract_Implementation(Controller);
}

float ACatWhipActor::GetSwingDuration() const { return AttackAnimation ? AttackAnimation->GetPlayLength() : 0.f; }

bool ACatWhipActor::IsWhipConfigurationReady() const
{
    if (!SkeletalMesh->GetSkeletalMeshAsset() || !AttackAnimation || AttackAnimation->GetSkeleton() != SkeletalMesh->GetSkeletalMeshAsset()->GetSkeleton()
        || !FMath::IsFinite(HitWindowStart) || !FMath::IsFinite(HitWindowEnd) || HitWindowStart < 0 || HitWindowEnd <= HitWindowStart
        || HitWindowEnd > GetSwingDuration() || !FMath::IsFinite(TraceRadiusCm) || TraceRadiusCm < 1 || TraceRadiusCm > 20
        || !FMath::IsFinite(ImpulseNewtonSeconds) || ImpulseNewtonSeconds < 0 || ImpulseNewtonSeconds > 30
        || !FMath::IsFinite(UpwardImpulseNewtonSeconds) || UpwardImpulseNewtonSeconds < 0 || UpwardImpulseNewtonSeconds > 30
        || !FMath::IsFinite(MaxTargetDistanceCm) || MaxTargetDistanceCm <= 0 || MaxTargetDistanceCm > 300
        || GripOffsetCm.ContainsNaN() || MeshRotationOffset.ContainsNaN()) return false;
    for (int32 Index=1; Index<=16; ++Index)
        if (SkeletalMesh->GetBoneIndex(FName(*FString::Printf(TEXT("lash_%02d"), Index))) == INDEX_NONE) return false;
    return true;
}

double ACatWhipActor::ServerTime() const
{
    const auto* State = GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool ACatWhipActor::StartSwingFromAuthority(ACatCharacter* Character, FGuid RequestId, FGuid ItemId)
{
    if (!HasAuthority() || SwingOwner || !Character || !RequestId.IsValid() || !ItemId.IsValid() || !IsWhipConfigurationReady()) return false;
    SwingOwner = Character; SwingRequestId = RequestId; SourceItemId = ItemId;
    // Freeze the body's actual facing at activation; looking around must not aim the whip.
    SwingStartServerTime = ServerTime(); SwingYaw = Character->GetActorRotation().Yaw;
    SetOwner(Character); SetInstigator(Character); SetReplicateMovement(false);
    HitTargets.Reset(); OccludedTargets.Reset(); LastSampleTime = 0;
    OnRep_Swing(); SetLifeSpan(GetSwingDuration()+1.f); ForceNetUpdate();
    UE_LOG(LogCatSocial, Log, TEXT("Event=whip_swing_started RequestId=%s Item=%s Actor=%s Player=%s World=%s NetMode=%d Authority=1 LocalRole=%d Duration=%.3f Window=%.3f:%.3f ImpulseNs=%.3f BodyYaw=%.2f ViewYaw=%.2f SwingYaw=%.2f"),
        *SwingRequestId.ToString(), *SourceItemId.ToString(), *GetName(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole(), GetSwingDuration(), HitWindowStart, HitWindowEnd, ImpulseNewtonSeconds,
        Character->GetActorRotation().Yaw, Character->GetControlRotation().Yaw, SwingYaw);
    return true;
}

void ACatWhipActor::OnRep_Swing()
{
    if (!IsValid(SwingOwner) || bPresentationStarted) return;
    bPresentationStarted = true;
    SetActorEnableCollision(false);
    SkeletalMesh->SetRelativeTransform(FTransform(MeshRotationOffset));
    SkeletalMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SkeletalMesh->SetAnimation(AttackAnimation);
    SkeletalMesh->bPauseAnims = true;
    SetActorTickEnabled(true);
    if (auto* Visual = SwingOwner->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>()) AddTickPrerequisiteComponent(Visual);
    LastGripTransform = GetGripTransform();
    EvaluatePose(0, LastGripTransform); GetTracePoints(PreviousPoints);
    UE_LOG(LogCatSocial, Log, TEXT("Event=whip_swing_observed RequestId=%s Item=%s Actor=%s Player=%s World=%s NetMode=%d Authority=%d LocalRole=%d Elapsed=%.3f"),
        *SwingRequestId.ToString(), *SourceItemId.ToString(), *GetName(), *GetNameSafe(SwingOwner), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), ServerTime()-SwingStartServerTime);
}

FTransform ACatWhipActor::GetGripTransform() const
{
    FVector Location = SwingOwner->GetActorLocation();
    if (auto* Visual = SwingOwner->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>(); Visual && Visual->GetVisualMesh()) Location = Visual->GetVisualHandWorldLocation(false);
    else if (auto* Body = SwingOwner->GetPhysicalBodyComponent()) if (auto* Hand = Body->GetHand(false)) Location = Hand->GetComponentLocation();
    const FRotator Rotation(0,SwingYaw,0);
    return FTransform(Rotation, Location + Rotation.RotateVector(GripOffsetCm));
}

void ACatWhipActor::EvaluatePose(double Time, const FTransform& Grip)
{
    SetActorTransform(Grip);
    SkeletalMesh->SetPosition(float(Time), false);
    SkeletalMesh->TickAnimation(0.f, false);
    SkeletalMesh->RefreshBoneTransforms();
    SkeletalMesh->UpdateComponentToWorld();
}

void ACatWhipActor::GetTracePoints(TArray<FVector>& Points) const
{
    Points.Reset();
    for (int32 Index=1; Index<=16; ++Index)
    {
        const FVector Point = SkeletalMesh->GetBoneLocation(FName(*FString::Printf(TEXT("lash_%02d"),Index)));
        if (!Points.IsEmpty()) Points.Add((Points.Last()+Point)*.5);
        Points.Add(Point);
    }
    // The last bone has no tail node in FBX; extrapolate its rest-space length in its own local frame.
    const auto& Ref = SkeletalMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const int32 Last = Ref.FindBoneIndex(TEXT("lash_16"));
    FTransform LastRef = FTransform::Identity, PrevRef = FTransform::Identity;
    for (int32 I=Last; I!=INDEX_NONE; I=Ref.GetParentIndex(I)) LastRef *= Ref.GetRefBonePose()[I];
    for (int32 I=Ref.GetParentIndex(Last); I!=INDEX_NONE; I=Ref.GetParentIndex(I)) PrevRef *= Ref.GetRefBonePose()[I];
    const FVector TailLocal = LastRef.InverseTransformPosition(2*LastRef.GetLocation()-PrevRef.GetLocation());
    Points.Add(SkeletalMesh->GetBoneTransform(Last).TransformPosition(TailLocal));
}

void ACatWhipActor::Sweep(const FVector& Start, const FVector& End)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CatWhip), false, this); Query.AddIgnoredActor(SwingOwner);
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(TraceRadiusCm), Query);
    for (const FHitResult& Hit : Hits)
    {
        auto* Target = Cast<ACatCharacter>(Hit.GetActor());
        if (!Target || Target == SwingOwner || HitTargets.Contains(Target) || !Target->GetPhysicalBodyComponent()
            || FVector::DistSquared(Target->GetActorLocation(),SwingOwner->GetActorLocation()) > FMath::Square(MaxTargetDistanceCm)) continue;
        FCollisionQueryParams Sight = Query; Sight.AddIgnoredActor(Target);
        FHitResult Wall;
        const FVector Contact = Hit.bStartPenetrating ? Hit.Location : Hit.ImpactPoint;
        // Interaction targeting volumes block Visibility but are not solid obstacles.
        // Match body-blocking geometry without changing the shared interaction profiles.
        if (GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation(),Contact,ECC_Pawn,Sight))
        {
            if (!OccludedTargets.Contains(Target))
            {
                OccludedTargets.Add(Target);
                UE_LOG(LogCatSocial, Log, TEXT("Event=whip_hit_occluded RequestId=%s Target=%s Blocker=%s Component=%s World=%s NetMode=%d Authority=1 Result=Occluded"),
                    *SwingRequestId.ToString(), *GetNameSafe(Target), *GetNameSafe(Wall.GetActor()), *GetNameSafe(Wall.GetComponent()), *GetNameSafe(GetWorld()), GetNetMode());
            }
            continue;
        }
        FVector Direction = (End-Start).GetSafeNormal2D();
        if (Direction.IsNearlyZero()) Direction = (Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
        if (Direction.IsNearlyZero()) Direction = FRotator(0,SwingYaw,0).Vector();
        HitTargets.Add(Target); // Record before applying the one authoritative external impulse.
        const FVector Impulse = (Direction*ImpulseNewtonSeconds+FVector::UpVector*UpwardImpulseNewtonSeconds)*100.f;
        Target->GetPhysicalBodyComponent()->AddExternalImpulseFromAuthority(Impulse,true);
        UE_LOG(LogCatSocial, Log, TEXT("Event=whip_hit_applied RequestId=%s Item=%s Actor=%s Player=%s Target=%s World=%s NetMode=%d Authority=1 LocalRole=%d ImpulseNs=%.3f UpwardImpulseNs=%.3f Direction=%s Result=Applied"),
            *SwingRequestId.ToString(), *SourceItemId.ToString(), *GetName(), *GetNameSafe(SwingOwner), *GetNameSafe(Target), *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole(), ImpulseNewtonSeconds, UpwardImpulseNewtonSeconds, *Direction.ToCompactString());
        MulticastHitObserved(Target);
    }
}

void ACatWhipActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bPresentationStarted) OnRep_Swing();
    if (!IsValid(SwingOwner)) { if (HasAuthority()) Destroy(); return; }
    const double Elapsed = FMath::Clamp(ServerTime()-SwingStartServerTime,0.0,double(GetSwingDuration()));
    const FTransform Grip = GetGripTransform();
    if (HasAuthority())
    {
        auto* Inventory = SwingOwner->GetInventoryComponent();
        if (!SwingOwner->GetController() || SwingOwner->GetController()->GetPawn()!=SwingOwner || !Inventory
            || Inventory->FindInventorySlotIndexFromInstanceId(SourceItemId)==INDEX_NONE
            || SwingOwner->GetCatAbilitySystemComponent()->HasMatchingGameplayTag(CatStateTags::Downed))
        {
            UE_LOG(LogCatSocial, Warning, TEXT("Event=whip_swing_aborted RequestId=%s Player=%s World=%s NetMode=%d Authority=1 Result=SourceOrOwnerLost"),
                *SwingRequestId.ToString(),*GetNameSafe(SwingOwner),*GetNameSafe(GetWorld()),GetNetMode());
            Destroy(); return;
        }
        const double Gap = Elapsed-LastSampleTime;
        const bool bDiscontinuous = Gap>.3 || FVector::DistSquared(Grip.GetLocation(),LastGripTransform.GetLocation())>FMath::Square(150.f);
        if (bDiscontinuous)
        {
            UE_LOG(LogCatSocial, Warning, TEXT("Event=whip_sweep_skipped RequestId=%s World=%s NetMode=%d Authority=1 Gap=%.3f Result=Discontinuity"),
                *SwingRequestId.ToString(),*GetNameSafe(GetWorld()),GetNetMode(),Gap);
        }
        else
        {
            const int32 Steps = FMath::Clamp(FMath::CeilToInt(Gap*60),1,18);
            for (int32 Step=1; Step<=Steps; ++Step)
            {
                const double Alpha = double(Step)/Steps;
                const double T = FMath::Lerp(LastSampleTime,Elapsed,Alpha);
                FTransform Sample; Sample.Blend(LastGripTransform,Grip,Alpha);
                EvaluatePose(T,Sample);
                TArray<FVector> Points; GetTracePoints(Points);
                if (T>=HitWindowStart && T<=HitWindowEnd && PreviousPoints.Num()==Points.Num())
                    for (int32 I=0; I<Points.Num(); ++I)
                    {
                        Sweep(PreviousPoints[I],Points[I]);
                        if (I>0) Sweep(Points[I-1],Points[I]);
                    }
                PreviousPoints=MoveTemp(Points);
            }
        }
        EvaluatePose(Elapsed,Grip); GetTracePoints(PreviousPoints);
        LastSampleTime=Elapsed; LastGripTransform=Grip;
        if (Elapsed>=GetSwingDuration()) Destroy();
    }
    else EvaluatePose(Elapsed,Grip);
}

void ACatWhipActor::MulticastHitObserved_Implementation(ACatCharacter* Target)
{
    UE_LOG(LogCatSocial, Log, TEXT("Event=whip_hit_observed RequestId=%s Item=%s Actor=%s Player=%s Target=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=Observed"),
        *SwingRequestId.ToString(),*SourceItemId.ToString(),*GetName(),*GetNameSafe(SwingOwner),*GetNameSafe(Target),*GetNameSafe(GetWorld()),GetNetMode(),HasAuthority(),GetLocalRole());
}

void ACatWhipActor::EndPlay(EEndPlayReason::Type Reason)
{
    SetActorTickEnabled(false); PreviousPoints.Reset();
    if (bPresentationStarted)
        UE_LOG(LogCatSocial, Log, TEXT("Event=whip_swing_ended RequestId=%s Item=%s Actor=%s World=%s NetMode=%d Authority=%d LocalRole=%d Hits=%d Reason=%d"),
            *SwingRequestId.ToString(),*SourceItemId.ToString(),*GetName(),*GetNameSafe(GetWorld()),GetNetMode(),HasAuthority(),GetLocalRole(),HitTargets.Num(),int32(Reason));
    Super::EndPlay(Reason);
}
