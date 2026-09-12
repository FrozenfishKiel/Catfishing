#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Effects/CatFightStaminaRegenEffect.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

UCatPhysicalEffortComponent::UCatPhysicalEffortComponent()
{
	SetIsReplicatedByDefault(true);
}

double UCatPhysicalEffortComponent::GetMaximumForceKgCmS2() const
{
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const auto* Settings = GetDefault<UCatPhysicalEffortSettings>();
	const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
	if (!ASC || !Settings->IsValid() || !Balance || bExhausted) return 0;
	const double Stamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double Force = Strength * Balance->ForcePerStrengthNewtons * 100.0;
	return FMath::IsFinite(Stamina) && Stamina > 0 && FMath::IsFinite(Force) && Force > 0 ? Force : 0;
}

bool UCatPhysicalEffortComponent::CanGripFromAuthority() const
{
	return !bExhausted;
}

void UCatPhysicalEffortComponent::SettleMovementFromAuthority(const FCatBodyDriveSample& Drive,
	const FVector& IntendedDisplacement, const FVector& ActualDisplacement, double Seconds, bool bGrounded)
{
	LastPaid = 0;
	LastResult = {};
	if (!GetOwner()->HasAuthority() || !FMath::IsFinite(Seconds) || Seconds <= 0) return;
	// The primary's runner is the sole writer of its movement + rod bill, including slack recovery.
	// 搏斗中一律不恢复（钓鱼规则 §6.2）：进搏斗就摘掉周期回体 GE，放线回体仍只由 Runner 结算。
	if (Drive.bFishing)
	{
		IdleSeconds = 0; bWasConnected = false; bRecoveryBlockedByLoad = false;
		SetNaturalRecoveryActive(false);
		return;
	}
	auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const auto* Settings = GetDefault<UCatPhysicalEffortSettings>();
	if (!ASC || !ASC->GetAvatarActor() || !Settings->IsValid()) return;
	const double Before = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double Maximum = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
	if (!FMath::IsFinite(Before) || !FMath::IsFinite(Maximum) || Maximum <= 0 || Before < 0 || Before > Maximum)
	{
		if (GetWorld()->GetTimeSeconds() >= NextLogSeconds)
		{ NextLogSeconds = GetWorld()->GetTimeSeconds() + 1; LogState(TEXT("physical_effort_rejected"), TEXT("InvalidAttributes")); }
		return;
	}
	++SettlementSequence;
	if (Drive.bCooperative != bWasConnected)
	{
		bWasConnected = Drive.bCooperative;
		LogState(TEXT("physical_effort_connection"), bWasConnected ? TEXT("Connected") : TEXT("Released"));
	}
	if (Drive.bCooperative && Before <= 0) { bRecoveryPending = true; SetExhausted(true); }
	FCatIntentMotionInput Input;
	const bool bActive = Drive.bCooperative && Drive.bLocomotion && bGrounded && !bExhausted && Drive.MaxForce > 0;
	Input.IntendedDisplacementCentimeters = bActive ? IntendedDisplacement : FVector::ZeroVector;
	Input.ActualDisplacementCentimeters = ActualDisplacement;
	Input.StaminaPerUnfulfilledMeter = Settings->StaminaPerUnfulfilledMeter;
	if (!FCatIntentMotionModel::ComputeDrain(Input, LastResult))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("InvalidMotion")); return; }
	const bool bExerting = bActive && !IntendedDisplacement.IsNearlyZero();
	if (bExerting) bRecoveryPending = true;
	if (Drive.bUnderLoad != bRecoveryBlockedByLoad)
	{
		bRecoveryBlockedByLoad = Drive.bUnderLoad;
		LogState(TEXT("physical_effort_recovery_gate"), Drive.bUnderLoad ? TEXT("Loaded") : TEXT("Unloaded"));
	}
	const bool bCanRest = !bExerting && !Drive.bUnderLoad && Drive.bLocomotion;
	if (!bCanRest) IdleSeconds = 0; else IdleSeconds += Seconds;
	// 恢复不再按帧写属性：本组件只裁决「此刻该不该恢复」，5 点/秒的速率和写口都在周期 GE 上。
	const double Requested = FMath::Min(Before, LastResult.StaminaDrain);
	if (Requested != 0 && !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Requested)))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("AbilityWriteFailed")); return; }
	if (GetOwner()->IsActorBeingDestroyed() || !IsValid(ASC)) return;
	const double After = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	LastPaid = Before - After;
	if (LastPaid > 0 && After <= 0) { IdleSeconds = 0; SetExhausted(true); }
	else if (bExhausted && After >= Maximum * Settings->ExhaustionResumeRatio) SetExhausted(false);
	if (After >= Maximum) bRecoveryPending = false;
	// 三道闸门（bCooperative 起手、bRecoveryPending、连续静止满 RecoveryDelaySeconds）设计里都没有，
	// 2026-09-11 明确仍未裁，这里原样保留，只是从「算一笔恢复」改成「开关同一条恢复通道」。
	const bool bRecovering = bRecoveryPending && bCanRest && After < Maximum
		&& IdleSeconds >= Settings->RecoveryDelaySeconds;
	SetNaturalRecoveryActive(bRecovering);
	if (Requested != 0 && GetWorld()->GetTimeSeconds() >= NextLogSeconds)
	{
		NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		LogState(TEXT("physical_effort_settled"), Drive.MoveIntent.IsNearlyZero() ? TEXT("Support") : TEXT("Movement"));
	}
}

// 统一出力池的扣体回执：钓鱼搏斗把体力扣在 Runner 的写口上，本组件看不到那笔账，
// 所以由 Runner 扣成后回调一次，武装与抓握同一条恢复闸；三道闸门本身不在这里改。
void UCatPhysicalEffortComponent::NotifyStaminaSpentFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	bRecoveryPending = true;
}

// 周期回体开关流程：
// 1. 只有 authority 持有这条通道；速率非正或不该恢复时摘掉已有效果并让句柄失效。
// 2. 已挂着就不重复提交，避免每帧开关制造新的 ActiveGameplayEffect 和复制流量。
// 3. 提交时按「点/秒 × 周期」折算成每周期点数，速率仍只有 RecoveryPerSecond 一处来源。
void UCatPhysicalEffortComponent::SetNaturalRecoveryActive(const bool bActive)
{
	auto* ASC = GetOwner() ? GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>() : nullptr;
	if (!IsValid(ASC) || !GetOwner()->HasAuthority())
	{
		NaturalRecoveryHandle.Invalidate();
		return;
	}
	const auto* Settings = GetDefault<UCatPhysicalEffortSettings>();
	const double PerSecond = Settings->IsValid() ? Settings->RecoveryPerSecond : 0.0;
	if (!bActive || PerSecond <= 0.0)
	{
		if (NaturalRecoveryHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(NaturalRecoveryHandle);
			NaturalRecoveryHandle.Invalidate();
			LogState(TEXT("physical_effort_recovery_channel"), TEXT("Stopped"));
		}
		return;
	}
	if (NaturalRecoveryHandle.IsValid()) return;
	if (!ASC->GetOwnerActor() || !ASC->GetAvatarActor()) return;
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UCatGE_FightStaminaRegen::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid()) return;
	Spec.Data->SetSetByCallerMagnitude(UCatGE_FightStaminaRegen::GetRegenPerPeriodTag(),
		static_cast<float>(PerSecond * UCatGE_FightStaminaRegen::PeriodSeconds));
	NaturalRecoveryHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	LogState(TEXT("physical_effort_recovery_channel"),
		NaturalRecoveryHandle.IsValid() ? TEXT("Started") : TEXT("StartRejected"));
}

void UCatPhysicalEffortComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetNaturalRecoveryActive(false);
	Super::EndPlay(EndPlayReason);
}

void UCatPhysicalEffortComponent::SetExhausted(bool bValue)
{
	if (bExhausted == bValue) return;
	bExhausted = bValue;
	if (bValue)
		if (auto* Grab = GetOwner()->FindComponentByClass<UCatPhysicsGrabComponent>())
			Grab->ReleaseAllFromAuthority(TEXT("PhysicalEffortExhausted"));
	GetOwner()->ForceNetUpdate();
	LogState(TEXT("physical_effort_state"), bValue ? TEXT("Exhausted") : TEXT("Ready"));
}

void UCatPhysicalEffortComponent::LogState(FName Event, FName Result) const
{
	const auto* Body = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* Player = Pawn ? Pawn->GetPlayerState() : nullptr;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const FString Record = FString::Printf(
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s PlayerId=%d BodyId=%s Step=%llu ForceBudgetUE=%.3f IntendedCm=%.6f ProgressCm=%.6f MissingCm=%.6f Paid=%.6f Stamina=%.6f Exhausted=%d Loaded=%d RestSeconds=%.3f Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), Player ? Player->GetPlayerId() : INDEX_NONE, Body ? *Body->GetBodyId().ToString() : TEXT("None"), SettlementSequence,
		GetMaximumForceKgCmS2(), LastResult.IntendedDistanceCentimeters, LastResult.ActualProgressCentimeters,
		LastResult.UnfulfilledDistanceCentimeters, LastPaid, ASC ? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0,
		bExhausted, bRecoveryBlockedByLoad, IdleSeconds, *Result.ToString());
	if (Event == TEXT("physical_effort_rejected")) { UE_LOG(LogCatPhysicsGrab, Warning, TEXT("%s"), *Record); }
	else { UE_LOG(LogCatPhysicsGrab, Log, TEXT("%s"), *Record); }
}

void UCatPhysicalEffortComponent::OnRep_Exhausted()
{
	LogState(TEXT("physical_effort_observed"), bExhausted ? TEXT("Exhausted") : TEXT("Ready"));
}

void UCatPhysicalEffortComponent::ObserveStaminaFromReplication(float PreviousStamina)
{
	if (!GetOwner() || GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->GetTimeSeconds() < NextLogSeconds) return;
	NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	LastPaid = ASC ? PreviousStamina - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0;
	LogState(TEXT("physical_effort_stamina_observed"), TEXT("ReplicatedPersonalBalance"));
}

void UCatPhysicalEffortComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCatPhysicalEffortComponent, bExhausted);
}
