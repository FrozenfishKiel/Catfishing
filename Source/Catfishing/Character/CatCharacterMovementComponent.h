#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CatCharacterMovementComponent.generated.h"

/** Keeps the existing BP/ABP component identity and movement configuration. Chaos owns motion. */
UCLASS()
class CATFISHING_API UCatCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	/** Read-only animation bridge; does not perform a CMC step or predict another body. */
	void RefreshPhysicalObservation();
	virtual bool IsFalling() const override;
	virtual bool IsMovingOnGround() const override;
	virtual void StopMovementImmediately() override;
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
};
