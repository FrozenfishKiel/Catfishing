#include "Character/Animation/CatForceReactionComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

bool FCatForceReactionGate::Step(double Load, double Dt, double Trigger, double Release, double Rearm)
{
    if (!FMath::IsFinite(Load) || !FMath::IsFinite(Dt) || Dt <= 0) return false;
    if (!bLoaded)
    {
        if (Load < Trigger) return false;
        bLoaded = true; ReleasedSeconds = 0; return true;
    }
    ReleasedSeconds = Load <= Release ? ReleasedSeconds + Dt : 0;
    if (ReleasedSeconds >= Rearm) { bLoaded = false; ReleasedSeconds = 0; }
    return false;
}

ECatForceReactionDirection FCatForceReactionGate::Direction(const FVector& Forward, const FVector& Force)
{
    const FVector F = Forward.GetSafeNormal2D(), D = Force.GetSafeNormal2D();
    const double Front = FVector::DotProduct(F, D), Right = FVector::CrossProduct(F, D).Z;
    if (FMath::Abs(Front) >= FMath::Abs(Right))
        return Front >= 0 ? ECatForceReactionDirection::Forward : ECatForceReactionDirection::Backward;
    return Right >= 0 ? ECatForceReactionDirection::Right : ECatForceReactionDirection::Left;
}

UCatForceReactionComponent::UCatForceReactionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    SetIsReplicatedByDefault(true);
}

void UCatForceReactionComponent::BeginPlay()
{
    Super::BeginPlay();
    Body = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
    if (Body) PrimaryComponentTick.AddPrerequisite(Body, Body->GetPostMovementTick());
    if (bEnabled && (!Body || !FMath::IsFinite(TriggerNewtons) || !FMath::IsFinite(ReleaseNewtons)
        || !FMath::IsFinite(RearmSeconds) || ReleaseNewtons < 0 || TriggerNewtons <= ReleaseNewtons
        || RearmSeconds <= 0 || DirectionalMontages.Num() != 4 || DirectionalMontages.Contains(nullptr)))
    {
        UE_LOG(LogCatCharacter, Warning, TEXT("Event=force_reaction_config_rejected World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s Result=InvalidThresholdsOrMontages"),
            *GetNameSafe(GetWorld()), int32(GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()));
        bEnabled = false;
    }
    SetComponentTickEnabled(bEnabled && GetOwner()->HasAuthority());
}

void UCatForceReactionComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt, Type, Tick);
    if (!bEnabled || !Body || !GetOwner()->HasAuthority()) return;
    FVector DirectionForce;
    const double Load = Body->GetCharacterInteractionLoadFromAuthority(DirectionForce);
    const bool WasLoaded = Gate.bLoaded;
    const bool Triggered = Gate.Step(Load, Dt, TriggerNewtons, ReleaseNewtons, RearmSeconds);
    if (WasLoaded && !Gate.bLoaded) LogEvent(TEXT("force_reaction_rearmed"), AuthorityEvent, ObservedDirection, Load, TEXT("Unloaded"));
    if (!Triggered) return;
    ++AuthorityEvent;
    const auto Direction = FCatForceReactionGate::Direction(GetOwner()->GetActorForwardVector(), DirectionForce);
    // Consume the onset even while downed; restoring locomotion under the same load cannot replay it.
    const auto* Condition = GetOwner()->FindComponentByClass<UCatConditionPresentationComponent>();
    if (!Body->IsLocomotionEnabled() || (Condition && Condition->GetObservedPosePhase() != TEXT("Locomotion")))
    {
        LogEvent(TEXT("force_reaction_suppressed"), AuthorityEvent, Direction, Load, TEXT("ConditionPose")); return;
    }
    LogEvent(TEXT("force_reaction_started"), AuthorityEvent, Direction, Load, TEXT("AuthorityConfirmed"));
    MulticastReact(AuthorityEvent, Direction, float(Load));
}

void UCatForceReactionComponent::MulticastReact_Implementation(uint32 EventId, ECatForceReactionDirection Direction, float Load)
{
    if (!bEnabled || EventId <= ObservedEvent) return;
    ObservedEvent = EventId; ObservedDirection = Direction;
    if (GetNetMode() == NM_DedicatedServer) return;
    auto* Cat = Cast<ACatCharacter>(GetOwner());
    auto* Anim = Cat && Cat->GetMesh() ? Cat->GetMesh()->GetAnimInstance() : nullptr;
    auto* Visual = GetOwner()->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
    const auto* Condition = GetOwner()->FindComponentByClass<UCatConditionPresentationComponent>();
    if (!Body || !Body->IsLocomotionEnabled() || (Condition && Condition->GetObservedPosePhase() != TEXT("Locomotion")))
    { LogEvent(TEXT("force_reaction_observed"), EventId, Direction, Load, TEXT("SkippedConditionPose")); return; }
    // Do not interrupt casting, rescue, fishing outcomes, or another still-playing reaction. Never queue a retry.
    if (Anim && Anim->IsAnyMontagePlaying())
    { LogEvent(TEXT("force_reaction_observed"), EventId, Direction, Load, TEXT("SkippedActiveMontage")); return; }
    UAnimMontage* Source = DirectionalMontages.IsValidIndex(int32(Direction)) ? DirectionalMontages[int32(Direction)] : nullptr;
    PlayingMontage = Visual ? Cast<UAnimMontage>(Visual->ResolveAnimationAsset(Source)) : Source;
    if (Anim && PlayingMontage && Cat->PlayAnimMontage(Source) > 0)
    { ++PlayedCount; LogEvent(TEXT("force_reaction_observed"), EventId, Direction, Load, TEXT("Played")); }
    else
    {
        LogEvent(TEXT("force_reaction_observed"), EventId, Direction, Load, TEXT("PlaybackFailed"));
        UE_LOG(LogCatCharacter, Warning, TEXT("Event=force_reaction_playback_failed World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s EventId=%u Montage=%s Result=MissingConsumerOrAsset"),
            *GetNameSafe(GetWorld()), int32(GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), EventId, *GetPathNameSafe(Source));
    }
}

void UCatForceReactionComponent::LogEvent(const TCHAR* Event, uint32 EventId, ECatForceReactionDirection Direction, double Load, const TCHAR* Result) const
{
    UE_LOG(LogCatCharacter, Log, TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s EventId=%u Direction=%s LoadN=%.3f Result=%s"),
        Event, *GetNameSafe(GetWorld()), int32(GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
        Body ? *Body->GetBodyId().ToString() : TEXT("None"), EventId, *UEnum::GetValueAsString(Direction), Load, Result);
}

void UCatForceReactionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Cat = Cast<ACatCharacter>(GetOwner()); Cat && PlayingMontage)
        if (auto* Anim = Cat->GetMesh()->GetAnimInstance()) Anim->Montage_Stop(.1f, PlayingMontage);
    Gate = {}; PlayingMontage = nullptr;
    Super::EndPlay(Reason);
}
