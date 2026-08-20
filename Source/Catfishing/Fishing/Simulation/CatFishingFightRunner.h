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
class UCatWaterQuerySubsystem;

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
	/** 向内游（休息）时长区间。 */
	FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	/** 向外游（发力）时长区间。 */
	FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	/** 鱼体力低于该比例后休息期乘以 LowStaminaRestMultiplier（规格 4.6 临时口径）。 */
	double LowStaminaRestThreshold = 0.5;
	double LowStaminaRestMultiplier = 1.5;
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
	/** 左键按住/松开；拖优先于放线。 */
	bool SetReeling(int64 InputSequence, bool bInReeling);
	/** 右键按住/松开；仅在未拖时生效。 */
	bool SetSlacking(int64 InputSequence, bool bInSlacking);
	ECatFightCatAction GetCatAction() const { return State.CatAction; }

private:
	void HandleFixedStep();
	void SelectNextMotionIntent();
	void RefreshCatAction();
	/** 翻肚/碾压后把鱼沿竿方向贴到近岸水面内（岸上态尚未实现，TODO 规格 5.3）。 */
	FVector SnapFishTowardShore(const FVector& FishPosition, const FVector& RodTip, const UCatWaterQuerySubsystem& Water) const;
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
	double LowStaminaRestThreshold = 0.5;
	double LowStaminaRestMultiplier = 1.5;
	double InitialFishStamina = 0.0;
	FRandomStream Random;
	double MotionSecondsRemaining = 0.0;
	int64 LastInputSequence = 0;
	int64 WearSequence = 0;
	bool bPullHeld = false;
	bool bSlackHeld = false;
	FTimerHandle FixedStepTimer;
	bool bInitialized = false;
	bool bRunning = false;
};
