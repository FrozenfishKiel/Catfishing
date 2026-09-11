#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Physics/Simulation/CatIntentMotionModel.h"
#include "CatPhysicalEffortComponent.generated.h"

struct FCatBodyDriveSample;

/** Personal physical-assistance resource boundary. Never joins a fishing session or pays a rod bill. */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicalEffortComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatPhysicalEffortComponent();
	double GetMaximumForceKgCmS2() const;
	bool CanGripFromAuthority() const;
	bool IsExhausted() const { return bExhausted; }
	/** Called once after the actual CMC step, never from candidate motion prediction. */
	void SettleMovementFromAuthority(const FCatBodyDriveSample& Drive, const FVector& IntendedDisplacement,
		const FVector& ActualDisplacement, double Seconds, bool bGrounded);
	const FCatIntentMotionResult& GetLastResult() const { return LastResult; }
	double GetLastPaid() const { return LastPaid; }
	uint64 GetSettlementSequence() const { return SettlementSequence; }
	void ObserveStaminaFromReplication(float PreviousStamina);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION() void OnRep_Exhausted();
	void SetExhausted(bool bValue);
	void LogState(FName Event, FName Result) const;
	UPROPERTY(ReplicatedUsing=OnRep_Exhausted) bool bExhausted = false;
	bool bRecoveryPending = false;
	bool bWasConnected = false;
	bool bRecoveryBlockedByLoad = false;
	double IdleSeconds = 0;
	double NextLogSeconds = 0;
	double LastPaid = 0;
	uint64 SettlementSequence = 0;
	FCatIntentMotionResult LastResult;
};
