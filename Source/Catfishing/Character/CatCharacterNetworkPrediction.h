#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"

/** Saved motor policy is replayed locally; only input flags/epoch go to the authority. */
class FCatSavedMove final : public FSavedMove_Character
{
public:
    using Super = FSavedMove_Character;
    FCatBodyDriveSample Drive;
    FVector ExternalForce = FVector::ZeroVector;
    FRotator ViewIntent = FRotator::ZeroRotator;
    float WalkSpeed = 0;
    uint32 ControlEpoch = 0;
    double PolicyServerSeconds = 0;
    bool bSprint = false;
    virtual void Clear() override;
    virtual uint8 GetCompressedFlags() const override;
    virtual void SetMoveFor(ACharacter* Character, float DeltaTime, const FVector& Accel,
        FNetworkPredictionData_Client_Character& ClientData) override;
    virtual void PrepMoveFor(ACharacter* Character) override;
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
};

struct FCatNetworkMoveData final : FCharacterNetworkMoveData
{
    using Super = FCharacterNetworkMoveData;
    uint32 ControlEpoch = 0;
    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override;
};

struct FCatNetworkMoveDataContainer final : FCharacterNetworkMoveDataContainer
{
    FCatNetworkMoveData Moves[3];
    FCatNetworkMoveDataContainer();
};

/** Server-only observed load, associated with the exact move acknowledged by CMC. */
struct FCatMovePolicy
{
    FCatBodyDriveSample Drive;
    FVector ExternalForce = FVector::ZeroVector;
    uint32 ControlEpoch = 0;
    double ServerSeconds = 0;
    float MoveTime = 0;
};

struct FCatMoveResponseDataContainer final : FCharacterMoveResponseDataContainer
{
    FCatMovePolicy Policy;
    bool bHasPolicy = false;
    virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
};
