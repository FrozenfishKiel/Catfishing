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
class ACatCharacter;
class AActor;
class APlayerState;
class UCatAbilitySystemComponent;
class UCatWaterQuerySubsystem;
class UStateTree;

struct CATFISHING_API FCatFishingFightRunnerInit
{
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<APlayerState> PrimaryPlayerState;
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

/** 一名鱼竿操作者在本场搏斗中的服务器私有意图/体力绑定。 */
struct CATFISHING_API FCatFightParticipantRuntime
{
	TWeakObjectPtr<APlayerState> PlayerState;
	TWeakObjectPtr<ACatCharacter> Character;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	double BaseFishingStrength = 0.0;
	double ActiveFishingStrength = 0.0;
	double StaminaMaximum = 0.0;
	int64 LastInputSequence = 0;
	bool bPullHeld = false;
	bool bSlackHeld = false;
	bool bPrimary = false;
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
	bool SetReeling(APlayerState* InputPlayerState, int64 InputSequence, bool bInReeling);
	/** 右键按住/松开线杯；按住期间鱼可在最大线长内自由带线。 */
	bool SetSlacking(APlayerState* InputPlayerState, int64 InputSequence, bool bInSlacking);
	/** 主操作手离竿后进入无人值守松线；Runner 继续推进，但不再读写旧玩家的力量或体力。 */
	bool BeginUnattendedSlackFromAuthority();
	/** 鱼力竭关闭 AI 与鱼端驱动力并立即清除猫端牵引；固定步和同一线长约束继续负责收近。 */
	bool SetFishExhaustedFromAuthority();
	bool IsFishExhaustedForAuthority() const { return State.bFishExhausted; }
	/** 鱼是否已经越过真实岸线；一旦成立，后续拖拽始终走地面吸附，不再回到水面高度。 */
	bool IsFishBeachedForAuthority() const { return bFishBeached; }
	/** 搏斗接力时原子迁移 ASC、力量、体力上限/当前值与新玩家自己的输入序号域。 */
	bool TransferOperatorFromAuthority(APlayerState* NewPlayerState, UCatAbilitySystemComponent* NewAbilitySystem,
		double NewCatStrength, double NewCatStaminaMaximum, double NewCatStamina,
		int64 InitialInputSequence, bool bInitialPullHeld, bool bInitialSlackHeld);
	ECatFightCatAction GetCatAction() const { return State.CatAction; }
	bool IsOperatorPresentForAuthority() const { return State.bOperatorPresent; }
	/** StateTree 状态入口的唯一行为意图写口；返回本状态应持续的服务器秒数。 */
	bool BeginBehaviorStateFromStateTree(ECatFishMotionIntent MotionIntent, double& OutDurationSeconds);

private:
	void HandleFixedStep();
	void RefreshCatAction();
	bool RefreshParticipantsFromRod();
	bool AddParticipantFromAuthority(APlayerState* PlayerState, bool bPrimary,
		bool bInitialPullHeld, bool bInitialSlackHeld, int64 InitialInputSequence);
	FCatFightParticipantRuntime* FindParticipant(APlayerState* PlayerState);
	FCatFightParticipantRuntime* FindPrimaryParticipant();
	bool UpdateParticipantIntentAndProperties();
	bool ApplyHelperStaminaChanges(double TotalGroupDrain);
	bool TryResolveGroundedFishPosition(const FVector& DesiredPosition,
		FVector& OutGroundedPosition, FVector& OutSurfaceNormal, AActor*& OutSurfaceActor) const;
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TMap<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime> Participants;
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
	FTimerHandle FixedStepTimer;
	double NextConstraintDiagnosticWorldSeconds = 0.0;
	double NextPowerDiagnosticWorldSeconds = 0.0;
	mutable double NextGroundSurfaceRejectedDiagnosticWorldSeconds = 0.0;
	double NextBeachingDeferredDiagnosticWorldSeconds = 0.0;
	bool bLastConstraintDiagnosticActive = false;
	bool bFishBeached = false;
	bool bInitialized = false;
	bool bRunning = false;
};
