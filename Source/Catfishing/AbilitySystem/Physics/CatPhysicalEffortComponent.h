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
	bool CanGripFromAuthority() const;
	bool IsExhausted() const { return bExhausted; }
	/** Called once after the actual CMC step, never from candidate motion prediction. */
	void SettleMovementFromAuthority(const FCatBodyDriveSample& Drive, const FVector& IntendedDisplacement,
		const FVector& ActualDisplacement, double Seconds, bool bGrounded);
	const FCatIntentMotionResult& GetLastResult() const { return LastResult; }
	double GetLastPaid() const { return LastPaid; }
	uint64 GetSettlementSequence() const { return SettlementSequence; }
	void ObserveStaminaFromReplication(float PreviousStamina);
	/**
	 * 统一出力池的扣体回执（2026-09-11 裁决④：抓、推、爬与搏斗花同一条体力）。
	 * 钓鱼搏斗的扣体走 Runner 自己的写口，不经过本组件，所以由 Runner 在扣成后回调一次，
	 * 让搏斗花掉的体力和抓握花掉的体力武装同一条恢复闸。
	 */
	void NotifyStaminaSpentFromAuthority();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION() void OnRep_Exhausted();
	void SetExhausted(bool bValue);
	/** 开关搏斗外周期回体 GE；速率与写口都归 GE，本组件只裁决什么时候该恢复。 */
	void SetNaturalRecoveryActive(bool bActive);
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
	/** 当前挂着的搏斗外周期回体 GE；空句柄表示此刻不恢复。 */
	FActiveGameplayEffectHandle NaturalRecoveryHandle;
};
