#include "Character/CatCharacterMovementComponent.h"

#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "GameFramework/Actor.h"

void UCatCharacterMovementComponent::RefreshPhysicalObservation()
{
	const UCatPhysicalBodyComponent* Physical = GetOwner()
		? GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
	if (!Physical) return;
	Velocity = Physical->GetVelocity();
	Acceleration = Physical->GetMoveIntent() * GetMaxAcceleration();
}

bool UCatCharacterMovementComponent::IsFalling() const
{
	const UCatPhysicalBodyComponent* Physical = GetOwner()
		? GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
	return Physical && Physical->HasMovementSample() && !Physical->IsGrounded();
}

bool UCatCharacterMovementComponent::IsMovingOnGround() const
{
	const UCatPhysicalBodyComponent* Physical = GetOwner()
		? GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr;
	return Physical && Physical->IsGrounded();
}

void UCatCharacterMovementComponent::StopMovementImmediately()
{
	if (UCatPhysicalBodyComponent* Physical = GetOwner()
		? GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>() : nullptr)
	{
		Physical->ClearControlIntent(TEXT("MovementStopped"));
	}
	RefreshPhysicalObservation();
}

void UCatCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	RefreshPhysicalObservation();
}

void UCatCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction,
	bool bFluid, float BrakingDeceleration)
{
	RefreshPhysicalObservation();
}
