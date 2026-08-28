#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Environment/CatWaterTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"
#include "CatFishingFightRunner.generated.h"

class ACatFishEncounterActor;
class ACatFishingRodActor;
class ACatFishingSession;
class UCatAbilitySystemComponent;
class UCatWaterQuerySubsystem;
class UStateTree;

struct CATFISHING_API FCatFishingFightRunnerInit
{
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	FCatWaterRegionHandle WaterRegion;
	FBox FrozenWaterBounds = FBox(ForceInit);
	FCatFightSimulationConfig Config;
	FCatFightSimulationState InitialState;
	/** 该玩家服务器已确认的最新连续输入序号；新 Runner 从此序号继续拒绝旧边沿。 */
	int64 InitialInputSequence = 0;
	/** 进入本场搏斗时物理左/右键是否仍被按住；收线优先级仍由 RefreshCatAction 统一裁决。 */
	bool bInitialPullHeld = false;
	bool bInitialSlackHeld = false;
	/** 向内游（休息）时长区间。 */
	FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	/** 向外游（发力）时长区间。 */
	FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	/** 鱼体力低于该比例后休息期乘以 LowStaminaRestMultiplier（规格 4.6 临时口径）。 */
	double LowStaminaRestThreshold = 0.5;
	double LowStaminaRestMultiplier = 1.5;
	FCatFishSteeringConfig SteeringConfig;
	TObjectPtr<UStateTree> BehaviorStateTree = nullptr;
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
	/** 左键按住/松开；收线优先于松开线杯。 */
	bool SetReeling(int64 InputSequence, bool bInReeling);
	/** 右键按住/松开线杯；按住期间鱼可在最大线长内自由带线。 */
	bool SetSlacking(int64 InputSequence, bool bInSlacking);
	/** 操作手离开竿位时清除两种持续输入，不终止 Runner，也不回退输入序号。 */
	void ClearOperatorInputFromAuthority();
	ECatFightCatAction GetCatAction() const { return State.CatAction; }
	/** StateTree 状态入口的唯一行为意图写口；返回本状态应持续的服务器秒数。 */
	bool BeginBehaviorStateFromStateTree(ECatFishMotionIntent MotionIntent, double& OutDurationSeconds);

private:
	void HandleFixedStep();
	void RefreshCatAction();
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	FCatWaterRegionHandle WaterRegion;
	FBox FrozenWaterBounds = FBox(ForceInit);
	FCatFightSimulationConfig Config;
	FCatFightSimulationState State;
	FVector2D CalmDurationRangeSeconds = FVector2D::ZeroVector;
	FVector2D StruggleDurationRangeSeconds = FVector2D::ZeroVector;
	double LowStaminaRestThreshold = 0.5;
	double LowStaminaRestMultiplier = 1.5;
	double InitialFishStamina = 0.0;
	FCatFishSteeringConfig SteeringConfig;
	FCatFishSteeringState SteeringState;
	UPROPERTY(Transient)
	TObjectPtr<UStateTree> BehaviorStateTree = nullptr;
	FRandomStream Random;
	FRandomStream SteeringRandom;
	int64 LastInputSequence = 0;
	bool bPullHeld = false;
	bool bSlackHeld = false;
	FTimerHandle FixedStepTimer;
	bool bInitialized = false;
	bool bRunning = false;
};
