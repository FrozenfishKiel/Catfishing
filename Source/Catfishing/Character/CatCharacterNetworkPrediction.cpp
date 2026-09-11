#include "Character/CatCharacterNetworkPrediction.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Engine/World.h"

namespace
{
class FCatClientPredictionData final : public FNetworkPredictionData_Client_Character
{
public:
    explicit FCatClientPredictionData(const UCharacterMovementComponent& Movement)
        : FNetworkPredictionData_Client_Character(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FCatSavedMove()); }
};
}

void FCatSavedMove::Clear()
{
    Super::Clear();
    Drive = {}; ExternalForce = FVector::ZeroVector; ViewIntent = FRotator::ZeroRotator;
    WalkSpeed = 0; ControlEpoch = 0; bSprint = false;
}

uint8 FCatSavedMove::GetCompressedFlags() const
{
    return Super::GetCompressedFlags() | (bSprint ? FLAG_Custom_0 : 0);
}

void FCatSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& Accel,
    FNetworkPredictionData_Client_Character& ClientData)
{
    Super::SetMoveFor(Character, InDeltaTime, Accel, ClientData);
    const auto* Cat = CastChecked<ACatCharacter>(Character);
    auto* Body = Cat->GetPhysicalBodyComponent();
    const auto* Movement = CastChecked<UCatCharacterMovementComponent>(Character->GetCharacterMovement());
    Drive = Body->GetReplicatedDrive();
    ExternalForce = Body->GetReplicatedExternalForce();
    ViewIntent = Body->GetViewIntent();
    WalkSpeed = Body->MaxMovementSpeedCmS;
    ControlEpoch = Body->GetControlEpoch();
    bSprint = Movement->bWantsSprint;
    // Force integration depends on the step duration. Combining would alter acceleration/braking
    // and could consume an impulse across a policy change. Packed dual moves still batch RPCs.
    bForceNoCombine = true;
}

void FCatSavedMove::PrepMoveFor(ACharacter* Character)
{
    Super::PrepMoveFor(Character);
    auto* Movement = CastChecked<UCatCharacterMovementComponent>(Character->GetCharacterMovement());
    Movement->ActiveDrive = Drive;
    Movement->ActiveExternalForce = ExternalForce;
    Movement->bReplayPolicy = true;
    Movement->bWantsSprint = bSprint;
    CastChecked<ACatCharacter>(Character)->GetPhysicalBodyComponent()->SetMovementSpeed(WalkSpeed);
    CastChecked<ACatCharacter>(Character)->GetPhysicalBodyComponent()->SetViewIntent(ViewIntent);
}

bool FCatSavedMove::CanCombineWith(const FSavedMovePtr&, ACharacter*, float) const { return false; }

void FCatNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type)
{
    Super::ClientFillNetworkMoveData(Move, Type);
    ControlEpoch = static_cast<const FCatSavedMove&>(Move).ControlEpoch;
    // Fishing can supply rod-facing aim rather than the free camera's rotation. Preserve
    // the validated physical view contract in the native CMC rotation payload.
    ControlRotation = static_cast<const FCatSavedMove&>(Move).ViewIntent;
}

bool FCatNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type)
{
    const bool bSuccess = Super::Serialize(Movement, Ar, Map, Type);
    Ar.SerializeIntPacked(ControlEpoch);
    return bSuccess && !Ar.IsError();
}

FCatNetworkMoveDataContainer::FCatNetworkMoveDataContainer()
{
    NewMoveData = &Moves[0]; PendingMoveData = &Moves[1]; OldMoveData = &Moves[2];
}

FNetworkPredictionData_Client* UCatCharacterMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
        const_cast<UCatCharacterMovementComponent*>(this)->ClientPredictionData = new FCatClientPredictionData(*this);
    return ClientPredictionData;
}

void UCatCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    if (auto* Controller = Cast<ACatfishingPlayerController>(CharacterOwner->GetController()))
    {
        if (Controller->IsDayTransitionInputBlocked()) bWantsSprint = false;
        // A flag selects server configuration; the client never supplies a speed or force budget.
        if (!bReplayPolicy)
            CastChecked<ACatCharacter>(CharacterOwner)->GetPhysicalBodyComponent()->SetMovementSpeed(
                Controller->GetConfiguredMovementSpeed(CharacterOwner, bWantsSprint));
    }
}

void UCatCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
    const auto* Cat = Cast<ACatCharacter>(CharacterOwner);
    const auto* Body = Cat ? Cat->GetPhysicalBodyComponent() : nullptr;
    const auto& Move = static_cast<const FCatNetworkMoveData&>(MoveData);
    if (!Body || Move.ControlEpoch != Body->GetControlEpoch())
    {
        if (Body && GetWorld()->GetTimeSeconds() >= NextMoveRejectLogSeconds)
        {
            NextMoveRejectLogSeconds = GetWorld()->GetTimeSeconds() + 1;
            UE_LOG(LogCatPhysicsGrab, Warning, TEXT("Event=cmc_move_rejected World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u MoveEpoch=%u MoveTime=%.3f Result=StaleControlEpoch"),
                *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Cat->GetLocalRole()), *GetNameSafe(Cat),
                *Body->GetBodyId().ToString(), Body->GetControlEpoch(), Move.ControlEpoch, Move.TimeStamp);
        }
        return;
    }
    const float PreviousTime = GetPredictionData_Server_Character()->CurrentClientTimeStamp;
    Super::ServerMove_PerformMovement(MoveData);
    if (GetPredictionData_Server_Character()->CurrentClientTimeStamp != PreviousTime
        && (!Velocity.IsNearlyZero(3) || !MoveData.Acceleration.IsNearlyZero())
        && GetWorld()->GetTimeSeconds() >= NextPredictionLogSeconds)
    {
        NextPredictionLogSeconds = GetWorld()->GetTimeSeconds() + 1;
        UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=cmc_move_accepted World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u MoveTime=%.3f Location=%s Velocity=%s Result=AuthoritativeSimulation"),
            *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Cat->GetLocalRole()), *GetNameSafe(Cat),
            *Body->GetBodyId().ToString(), Body->GetControlEpoch(), MoveData.TimeStamp,
            *Cat->GetActorLocation().ToCompactString(), *Velocity.ToCompactString());
    }
}

void UCatCharacterMovementComponent::ResetControlPrediction()
{
    // Preserve the CMC timestamp clock. Recreating ClientPredictionData resets it to zero,
    // which the server would reject as stale after an ordinary teleport/control change.
    if (CharacterOwner && !CharacterOwner->HasAuthority() && ClientPredictionData)
    {
        ClientPredictionData->SavedMoves.Reset();
        ClientPredictionData->PendingMove.Reset();
        ClientPredictionData->LastAckedMove.Reset();
        ClientPredictionData->bUpdatePosition = false;
    }
    bReplayPolicy = false;
    ConsumeInputVector();
    if (CharacterOwner) CharacterOwner->StopJumping();
}

bool UCatCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
    auto* Body = CastChecked<ACatCharacter>(CharacterOwner)->GetPhysicalBodyComponent();
    const double LiveSpeed = Body->MaxMovementSpeedCmS;
    const FRotator LiveView = Body->GetViewIntent();
    const bool bLiveSprint = bWantsSprint;
    const bool bUpdated = Super::ClientUpdatePositionAfterServerUpdate();
    Body->SetMovementSpeed(LiveSpeed);
    Body->SetViewIntent(LiveView);
    bWantsSprint = bLiveSprint;
    bReplayPolicy = false;
    return bUpdated;
}

void UCatCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
    float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewBase,
    FName BaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
    Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase,
        BaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
    ++CorrectionCount;
    // Compare the same acknowledged move, not today's predicted position against a past server pose.
    if (ClientData.LastAckedMove.IsValid())
        MaxCorrectionCm = FMath::Max(MaxCorrectionCm, FVector::Dist(ClientData.LastAckedMove->SavedLocation, NewLocation));
    if (GetWorld()->GetTimeSeconds() < NextCorrectionLogSeconds || !CharacterOwner->IsLocallyControlled()) return;
    NextCorrectionLogSeconds = GetWorld()->GetTimeSeconds() + 1;
    const auto* Body = CastChecked<ACatCharacter>(CharacterOwner)->GetPhysicalBodyComponent();
    UE_LOG(LogCatPhysicsGrab, Log, TEXT("Event=cmc_prediction_corrected World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s ControlEpoch=%u MoveTime=%.3f Corrections=%u MaxErrorCm=%.3f PendingMoves=%d Result=CMCReconcile"),
        *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), CharacterOwner->HasAuthority(), int32(CharacterOwner->GetLocalRole()),
        *GetNameSafe(CharacterOwner), *Body->GetBodyId().ToString(), Body->GetControlEpoch(), TimeStamp, CorrectionCount, MaxCorrectionCm, ClientData.SavedMoves.Num());
    CorrectionCount = 0; MaxCorrectionCm = 0;
}
