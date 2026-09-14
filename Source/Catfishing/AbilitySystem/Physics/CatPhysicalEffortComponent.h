#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
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
	// 墓碑（2026-09-14）：持续力竭与抓握门已按设计修改记录 2026-09-13 裁决⑥退役。
	/** Called once after the actual CMC step, never from candidate motion prediction. */
	void SettleMovementFromAuthority(const FCatBodyDriveSample& Drive, const FVector& IntendedDisplacement,
		const FVector& ActualDisplacement, double Seconds, bool bGrounded);
	const FCatIntentMotionResult& GetLastResult() const { return LastResult; }
	double GetLastPaid() const { return LastPaid; }
	uint64 GetSettlementSequence() const { return SettlementSequence; }
	void ObserveStaminaFromReplication(double PreviousTotalStamina);
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	/** 开关搏斗外周期回体 GE；速率与写口都归 GE，本组件只裁决什么时候该恢复。 */
	void SetNaturalRecoveryActive(bool bActive);
	void LogState(FName Event, FName Result) const;
	bool bWasConnected = false;
	bool bRecoveryBlockedByLoad = false;
	double NextLogSeconds = 0;
	double LastPaid = 0;
	uint64 SettlementSequence = 0;
	FCatIntentMotionResult LastResult;
	/** 当前挂着的搏斗外周期回体 GE；空句柄表示此刻不恢复。 */
	FActiveGameplayEffectHandle NaturalRecoveryHandle;
};
