#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Environment/CatWaterTypes.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"
#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"
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
	/** 进入本场搏斗时物理左/右键是否仍被按住；RefreshCatAction 按线杯容量统一裁决。 */
	bool bInitialPullHeld = false;
	bool bInitialSlackHeld = false;
	FCatFishSteeringConfig SteeringConfig;
	TObjectPtr<UStateTree> BehaviorStateTree = nullptr;
	uint64 RandomSeed = 0;
};

/** 一段服务器已接受移动的观察量；按真实采样时间分给固定步，不能重复消费身体位移。 */
struct FCatFightOperatorMovementSample
{
	double DurationSeconds = 0.0;
	FVector MoveIntentWorld = FVector::ZeroVector;
	FVector ActualDisplacementCentimeters = FVector::ZeroVector;
	double MaximumMoveSpeedCentimetersPerSecond = 0.0;
};

/** 一名鱼竿操作者在本场搏斗中的服务器私有意图/体力绑定。 */
struct CATFISHING_API FCatFightOperatorRuntime
{
	TWeakObjectPtr<APlayerState> PlayerState;
	TWeakObjectPtr<ACatCharacter> Character;
	TWeakObjectPtr<UCatAbilitySystemComponent> AbilitySystem;
	double BaseFishingStrength = 0.0;
	/** 有正体力时使用本人的基础力量，归零时停止出力；不按体力比例衰减。 */
	double ActiveFishingStrength = 0.0;
	int64 LastInputSequence = 0;
	bool bPullHeld = false;
	bool bSlackHeld = false;
	uint32 ControlEpoch = 0;
	double StaminaMaximum = 0.0;
	FVector LastSampledPosition = FVector::ZeroVector;
	double LastMovementSampleWorldSeconds = 0.0;
	uint32 LastBodyResetEpoch = 0;
	TArray<FCatFightOperatorMovementSample> PendingMovementSamples;
	bool bHasSampledPosition = false;
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
	/** 左键按住/松开；有线杯容量时右键优先，满线或右键释放后恢复收线。 */
	bool SetReeling(APlayerState* InputPlayerState, int64 InputSequence, bool bInReeling);
	/** 记录右键；尚有线杯容量才放线并免耗回体，满线按其余输入锁线或收线。 */
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
	/** 原物品主人明确取回操控时，重新绑定本人 ASC 与输入序号域。 */
	bool ResumeOwnerFromAuthority(APlayerState* NewPlayerState, UCatAbilitySystemComponent* NewAbilitySystem,
		double NewCatStrength, double NewCatStaminaMaximum, double NewCatStamina,
		int64 InitialInputSequence, bool bInitialPullHeld, bool bInitialSlackHeld);
	ECatFightCatAction GetCatAction() const { return State.CatAction; }
	bool IsOperatorPresentForAuthority() const { return State.bOperatorPresent; }
	/** StateTree 是策略选择唯一入口；耗体与运动仍由固定步模型裁决。 */
	bool BeginFishBehaviorFromStateTree(ECatFishBehavior Behavior);
	bool TestFishBehaviorConditionFromStateTree(ECatFishBehaviorCondition Condition) const;

private:
	virtual void BeginDestroy() override;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatFishingExhaustedPickupHandoffTest;
	friend class FCatFishingSurfaceTraversalTest;
	friend class FCatFishingParticipantStrengthTest;
	friend class FCatFishingOperatorRunnerIntegrationTest;
	friend class FCatFishingPhysicalCouplingTest;
	friend class FCatFishingFormalPhysicalRunnerTest;
	friend class FCatFishingMotionDiagnosticTest;
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	void HandleFixedStep();
	void HandlePhysicsFrame(float DeltaSeconds);
	void HandlePhysicsReceiverUnavailable();
	void RefreshCatAction();
	bool UpdateFishBehaviorForCurrentOperator(bool bRodHeld);
	bool RefreshPrimaryOperatorFromRod();
	bool BindPrimaryOperatorFromAuthority(APlayerState* PlayerState, bool bInitialPullHeld, bool bInitialSlackHeld, int64 InitialInputSequence);
	FCatFightOperatorRuntime* GetPrimaryOperator();
	bool UpdateOperatorIntentAndProperties();
	bool ApplyOperatorStaminaChanges(const FCatFightStepResult& Step);
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
	FCatFightOperatorRuntime OperatorState;
	TArray<FCatFightOperatorMovementSample> FrozenOperatorMovementSamples;
	TWeakObjectPtr<UCatAbilitySystemComponent> FrozenOperatorAbilitySystem;
	double FrozenOperatorStamina = 0.0;
	double FrozenOperatorStaminaMaximum = 0.0;
	double OperatorSupportAlignment = 1.0;
	double LastOperatorStaminaDrain = 0.0;
	double NextStaminaDiagnosticSeconds = 0.0;
	bool bOperatorSettlementPending = false;
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
	FDelegateHandle PhysicsFrameHandle;
	FDelegateHandle PhysicsReceiverUnavailableHandle;
	double PendingFixedStepSeconds = 0.0;
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
