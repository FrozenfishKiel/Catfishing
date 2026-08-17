#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Environment/CatWaterTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "CatFishingFightRunner.generated.h"

class ACatFishEncounterActor;
class ACatFishingRodActor;
class ACatFishingSession;
class UCatAbilitySystemComponent;
class UCatEquipmentComponent;

struct CATFISHING_API FCatFishingFightRunnerInit
{
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<UCatEquipmentComponent> Equipment;
	FGuid FishingSessionId;
	FCatWaterRegionHandle WaterRegion;
	FBox FrozenWaterBounds = FBox(ForceInit);
	FCatFightSimulationConfig Config;
	FCatFightSimulationState InitialState;
	FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	uint64 RandomSeed = 0;
};

/** Authority-only fixed-step owner of fight simulation and resource side effects. */
UCLASS()
class CATFISHING_API UCatFishingFightRunner : public UObject
{
	GENERATED_BODY()
public:
	bool InitializeFromAuthority(const FCatFishingFightRunnerInit& Init);
	bool Start();
	void Stop();
	bool IsRunning() const { return bRunning; }
	bool SetReeling(int64 InputSequence, bool bInReeling);

private:
	void HandleFixedStep();
	void SelectNextMotionIntent();
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<UCatEquipmentComponent> Equipment;
	FGuid FishingSessionId;
	FCatWaterRegionHandle WaterRegion;
	FBox FrozenWaterBounds = FBox(ForceInit);
	FCatFightSimulationConfig Config;
	FCatFightSimulationState State;
	FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	FRandomStream Random;
	double MotionSecondsRemaining = 0.0;
	int64 LastInputSequence = 0;
	int64 WearSequence = 0;
	FTimerHandle FixedStepTimer;
	bool bInitialized = false;
	bool bRunning = false;
};
