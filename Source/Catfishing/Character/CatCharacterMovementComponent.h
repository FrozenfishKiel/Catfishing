#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CatCharacterMovementComponent.generated.h"

/** 已由服务器裁决的外部牵引。只改变移动，不包含体力、耐久或会话状态。 */
struct FCatExternalTractionInput
{
	FGuid SourceId;
	FVector Direction = FVector::ZeroVector;
	double AccelerationCentimetersPerSecondSquared = 0.0;
	/** 已扣除鱼线拉力后剩余的支撑减速度，只能减缓向鱼运动，不能把静止角色推离鱼。 */
	double BrakingDecelerationCentimetersPerSecondSquared = 0.0;
	double SpeedLimitCentimetersPerSecond = 0.0;
	bool bActive = false;
	/** 同竿组驱动代替个人普通行走；原始 Acceleration 仍由网络移动入口接收。 */
	bool bGroupDriven = false;
	/** 成员身份已确认，受力快照尚未齐全；保留惯性/制动，但禁止个人加速和旧组外力。 */
	bool bWaitingForGroupSolve = false;
	/** 占竿但未搏斗：使用服务器积分的共同水平速度，仍保留 CMC 重力与碰撞。 */
	bool bUnloadedMovement = false;
	uint32 RosterVersion = 0;
	uint32 ControlEpoch = 0;
	uint32 MembershipEpoch = 0;
	uint32 AimInputEpoch = 0;
	FVector GroupDesiredVelocity = FVector::ZeroVector;
	/** 无鱼载荷时的共同实际速度，cm/s；不同于力量加权的目标速度。 */
	FVector GroupUnloadedVelocity = FVector::ZeroVector;
	FVector GroupLateralAcceleration = FVector::ZeroVector;
	FVector FormationCorrectionVelocity = FVector::ZeroVector;
};

/** 外力进入 CMC 的速度积分和碰撞流程，并随 SavedMove 重放。 */
UCLASS()
class CATFISHING_API UCatCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	void SetExternalTraction(const UObject* Source, const FCatExternalTractionInput& Input);
	void ClearExternalTraction(const UObject* Source);
	/** 只忽略本竿共同移动的身体；退出时恢复本接口新增的忽略项，不覆盖其它系统。 */
	void SetFishingGroupCollisionPeers(const UObject* Source, const TArray<AActor*>& Peers);
	FCatExternalTractionInput GetExternalTraction() const { return TractionSource.IsValid() ? LiveTraction : FCatExternalTractionInput{}; }
	void RestoreTractionForSavedMove(const FCatExternalTractionInput& Input);
	/** 只读胶囊探测，供外力求解约束下一步可移动距离；实际落位仍由 CMC 完成。 */
	double GetExternalTractionTravelLimit(const FVector& Direction, double MaximumDistance, bool bAllowStepUp = false) const;
	FVector GetAcceptedFishingMoveIntent() const;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
private:
	friend class FCatFishingGroupRunnerIntegrationTest;
	friend class FCatFishingGroupMovementContinuityTest;
	friend class FCatFishingGroupMovementEpochTest;
	friend class FCatFishingGroupWaitingTest;
	friend class FCatFishingGroupUnloadedMovementTest;
	friend class FCatFishingGroupUnloadedCollisionTest;
	TArray<TWeakObjectPtr<AActor>> FishingAddedCollisionIgnores;
	TWeakObjectPtr<const UObject> TractionSource;
	FCatExternalTractionInput LiveTraction;
	FCatExternalTractionInput MovementTraction;
	bool bUseSavedTraction = false;
	double NextTractionDiagnosticSeconds = 0.0;
	double NextReplayDiagnosticSeconds = 0.0;
	double NextSourceConflictDiagnosticSeconds = 0.0;
	FGuid LastTractionDiagnosticSourceId;
	bool bLastTractionActive = false;
};

class FCatSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	FCatExternalTractionInput Traction;
	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
};
