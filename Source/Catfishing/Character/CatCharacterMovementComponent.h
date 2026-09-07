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
};

/** 外力进入 CMC 的速度积分和碰撞流程，并随 SavedMove 重放。 */
UCLASS()
class CATFISHING_API UCatCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	void SetExternalTraction(const UObject* Source, const FCatExternalTractionInput& Input);
	void ClearExternalTraction(const UObject* Source);
	FCatExternalTractionInput GetExternalTraction() const { return TractionSource.IsValid() ? LiveTraction : FCatExternalTractionInput{}; }
	void RestoreTractionForSavedMove(const FCatExternalTractionInput& Input);
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
private:
	TWeakObjectPtr<const UObject> TractionSource;
	FCatExternalTractionInput LiveTraction;
	FCatExternalTractionInput MovementTraction;
	bool bUseSavedTraction = false;
	double NextTractionDiagnosticSeconds = 0.0;
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
