#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Character/CatCharacterNetworkPrediction.h"
#include "CatCharacterMovementComponent.generated.h"

struct CATFISHING_API FCatCMCMotionPrediction
{
    FCatBodyDriveSample Drive;
    FVector Position = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FVector ExternalForce = FVector::ZeroVector;
    double MassKg = 4;
    double GroundResistanceNewtons = .8;
    double GravityZ = -980;
    bool bGrounded = true;
    bool bAcceptVerticalLineForce = false;
};

/** Upright force-limited motor using CMC saved moves, authority validation and network smoothing. */
UCLASS()
class CATFISHING_API UCatCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	UCatCharacterMovementComponent();
	/** Passive ground contact resistance, independent of voluntary fishing strength. */
	UPROPERTY(EditAnywhere, Category="Catfishing|Movement", meta=(ClampMin="0"))
	float GroundResistanceNewtons = 0.8f;
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp,
		FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewBase, FName BaseBoneName,
		bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;
	void ResetControlPrediction();
	void SetSprintIntent(bool bSprint) { bWantsSprint = bSprint; }
	bool WantsSprint() const { return bWantsSprint; }
	FVector GetLastExternalForce() const { return LastExternalForce; }
	void QueueExternalImpulse(FVector Impulse)
	{
		QueuedExternalImpulse += Impulse;
		bQueuedExternalLoad |= !Impulse.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER);
	}
	void ClearQueuedExternalImpulse() { QueuedExternalImpulse = MovementExternalForce = FVector::ZeroVector; bQueuedExternalLoad = false; }
	bool HasExternalLoad() const { return bQueuedExternalLoad || !MovementExternalForce.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER); }
	FCatCMCMotionPrediction CaptureMotionPrediction();
	static void AdvanceMotionPrediction(FCatCMCMotionPrediction& Sample, const FVector& LineForceNewtons, double Seconds);
	double GetExternalTractionTravelLimit(const FVector& Direction, double MaximumDistance) const;
	void UpdatePeerPushContacts();
	/** Passive shape separation only; also used after final animation, without advancing the motor. */
	void ResolveModelPeerPenetration();
	virtual void StopMovementImmediately() override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual bool IsWalkable(const FHitResult& Hit) const override;
	virtual void InitCollisionParams(FCollisionQueryParams& OutParams, FCollisionResponseParams& OutResponseParam) const override;
	virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation) override;
	FVector GetTotalMotionCorrection() const { return TotalMotionCorrection; }
private:
	friend class FCatSavedMove;
	FCatNetworkMoveDataContainer NetworkMoves;
	FCatBodyDriveSample ActiveDrive;
	FVector ActiveExternalForce = FVector::ZeroVector;
	FVector LastExternalForce = FVector::ZeroVector;
	bool bWantsSprint = false;
	bool bReplayPolicy = false;
	uint32 ObservedControlEpoch = 0;
	double NextPredictionLogSeconds = 0;
	double NextCorrectionLogSeconds = 0;
	double NextMoveRejectLogSeconds = 0;
	uint32 CorrectionCount = 0;
	double MaxCorrectionCm = 0;
	FVector TotalMotionCorrection = FVector::ZeroVector;
	double NextModelContactLogSeconds = 0;
	double NextPeerSeparationLogSeconds = 0;
	FVector QueuedExternalImpulse = FVector::ZeroVector;
	bool bQueuedExternalLoad = false;
	FVector MovementExternalForce = FVector::ZeroVector;
};
