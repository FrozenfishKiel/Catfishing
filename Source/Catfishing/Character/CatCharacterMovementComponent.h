#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
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

/** Upright capsule locomotion. The existing authority input/snapshot channel schedules one CMC step. */
UCLASS()
class CATFISHING_API UCatCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	UCatCharacterMovementComponent();
	/** Passive ground contact resistance, independent of voluntary fishing strength. */
	UPROPERTY(EditAnywhere, Category="Catfishing|Movement", meta=(ClampMin="0"))
	float GroundResistanceNewtons = 0.8f;
	void AdvanceFromAuthority(float DeltaSeconds);
	void QueueExternalImpulse(FVector Impulse) { QueuedExternalImpulse += Impulse; }
	void ClearQueuedExternalImpulse() { QueuedExternalImpulse = MovementExternalForce = FVector::ZeroVector; }
	FCatCMCMotionPrediction CaptureMotionPrediction();
	static void AdvanceMotionPrediction(FCatCMCMotionPrediction& Sample, const FVector& LineForceNewtons, double Seconds);
	double GetExternalTractionTravelLimit(const FVector& Direction, double MaximumDistance) const;
	void ObserveSnapshot(const FVector& ObservedVelocity, const FVector& ObservedIntent);
	virtual bool IsFalling() const override;
	virtual bool IsMovingOnGround() const override;
	void UpdatePeerPushContacts();
	virtual void StopMovementImmediately() override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual bool IsWalkable(const FHitResult& Hit) const override;
	virtual void InitCollisionParams(FCollisionQueryParams& OutParams, FCollisionResponseParams& OutResponseParam) const override;
private:
	double NextModelContactLogSeconds = 0;
	FVector QueuedExternalImpulse = FVector::ZeroVector;
	FVector MovementExternalForce = FVector::ZeroVector;
};
