#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Environment/CatWaterTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
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
struct FCatFishMotionSolveResult;

struct CATFISHING_API FCatFishingFightRunnerInit
{
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<APlayerState> PrimaryPlayerState;
	FCatWaterRegionHandle WaterRegion;
	FCatFightSimulationConfig Config;
	FCatFightSimulationState InitialState;
	/** 该玩家服务器已确认的最新连续输入序号；新 Runner 从此序号继续拒绝旧边沿。 */
	int64 InitialInputSequence = 0;
	/** 进入本场搏斗时物理左/右键是否仍被按住；RefreshCatAction 统一裁决，右键优先。 */
	bool bInitialPullHeld = false;
	bool bInitialSlackHeld = false;
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
	/** 有正体力时等于当前基础力量，体力归零时停止主动出力；不按体力比例衰减。 */
	double ActiveFishingStrength = 0.0;
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
	/** 左键按住/松开；同时按右键时暂停收线，右键释放后恢复。 */
	bool SetReeling(APlayerState* InputPlayerState, int64 InputSequence, bool bInReeling);
	/** 右键按住/松开线杯并免耗体回体；零体力强制拖拽仍优先。 */
	bool SetSlacking(APlayerState* InputPlayerState, int64 InputSequence, bool bInSlacking);
	/** 读取本场已接受的右键状态；区别于 CommandComponent 在拒绝请求后仍保留的物理按键事实。 */
	bool IsSlackInputHeldForAuthority(APlayerState* InputPlayerState) const;
	/** 主操作手离竿后进入无人值守松线；Runner 继续推进，但不再读写旧玩家的力量或体力。 */
	bool BeginUnattendedSlackFromAuthority();
	/** 鱼力竭关闭 AI 与鱼端驱动力并立即清除猫端牵引；固定步和同一线长约束继续负责收近。 */
	bool SetFishExhaustedFromAuthority();
	bool IsFishExhaustedForAuthority() const { return State.bFishExhausted; }
	/** 鱼当前是否接触真实干地；水岸转换由连续表面查询裁决，不永久锁在某一种表面。 */
	bool IsFishBeachedForAuthority() const { return bFishBeached; }
	/** 搏斗接力时原子迁移 ASC、力量、体力上限/当前值与新玩家自己的输入序号域。 */
	bool TransferOperatorFromAuthority(APlayerState* NewPlayerState, UCatAbilitySystemComponent* NewAbilitySystem,
		double NewCatStrength, double NewCatStaminaMaximum, double NewCatStamina,
		int64 InitialInputSequence, bool bInitialPullHeld, bool bInitialSlackHeld);
	ECatFightCatAction GetCatAction() const { return State.CatAction; }
	bool IsOperatorPresentForAuthority() const { return State.bOperatorPresent; }
	/** StateTree 是策略选择唯一入口；耗体与运动仍由固定步模型裁决。 */
	bool BeginFishBehaviorFromStateTree(ECatFishBehavior Behavior);
	bool TestFishBehaviorConditionFromStateTree(ECatFishBehaviorCondition Condition) const;

private:
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatFishingExhaustedPickupHandoffTest;
	friend class FCatFishingSurfaceTraversalTest;
	friend class FCatFishingParticipantStrengthTest;
	friend class FCatFishingMotionDiagnosticTest;
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	void HandleFixedStep();
	void RefreshCatAction();
	bool UpdateFishBehaviorForCurrentOperator(bool bRodHeld);
	bool RefreshParticipantsFromRod();
	bool AddParticipantFromAuthority(APlayerState* PlayerState, bool bPrimary,
		bool bInitialPullHeld, bool bInitialSlackHeld, int64 InitialInputSequence);
	FCatFightParticipantRuntime* FindParticipant(APlayerState* PlayerState);
	FCatFightParticipantRuntime* FindPrimaryParticipant();
	bool UpdateParticipantIntentAndProperties();
	bool ApplyHelperStaminaChanges(double TotalGroupDrain);
	bool TryResolveGroundedFishPosition(const FVector& DesiredPosition,
		FVector& OutGroundedPosition, FVector& OutSurfaceNormal, AActor*& OutSurfaceActor) const;
	FCatFishMotionSolveResult ResolveFishSurfaceFromAuthority(FCatFightStepResult& Step,
		const FCatFightRodConstraintInput& RodConstraint, FCatWaterSpatialResult& OutWater,
		bool& bOutBeachedThisStep, FVector& OutGroundNormal, AActor*& OutGroundActor,
		FCatFishingRodResistanceResult& OutRotationResistance);
	TWeakObjectPtr<ACatFishingSession> Session;
	TWeakObjectPtr<ACatFishEncounterActor> FishActor;
	TWeakObjectPtr<ACatFishingRodActor> RodActor;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	TMap<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime> Participants;
	FCatWaterRegionHandle WaterRegion;
	FCatFightSimulationConfig Config;
	FCatFightSimulationState State;
	double InitialFishStamina = 0.0;
	FCatFishSteeringConfig SteeringConfig;
	FCatFishSteeringState SteeringState;
	/** 上一步已结算的物理反馈；不持有第二份体力或费用。 */
	double PreviousFishLineTensionNewtons = 0.0;
	FVector PreviousFishEffortDirection = FVector::ForwardVector;
	double PreviousFishExpectedSwimSpeedCentimetersPerSecond = 0.0;
	FCatFishingRodEffortSampler RotationEffortSampler;
	UPROPERTY(Transient)
	TObjectPtr<UStateTree> BehaviorStateTree = nullptr;
	FRandomStream SteeringRandom;
	FTimerHandle FixedStepTimer;
	double NextConstraintDiagnosticWorldSeconds = 0.0;
	uint64 DiagnosticFixedStepSequence = 0;
	double LastFixedStepDiagnosticWorldSeconds = -1.0;
	double NextPowerDiagnosticWorldSeconds = 0.0;
	mutable double NextGroundSurfaceRejectedDiagnosticWorldSeconds = 0.0;
	double NextSurfaceTowDiagnosticWorldSeconds = 0.0;
	double NextShoreContactDiagnosticWorldSeconds = 0.0;
	bool bLastShoreContactDiagnosticActive = false;
	bool bLastConstraintDiagnosticActive = false;
	bool bLastExhaustedCatEscapeDiagnosticActive = false;
	bool bFishBeached = false;
	bool bInitialized = false;
	bool bRunning = false;
};
