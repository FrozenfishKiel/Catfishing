#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
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
	if (Drive.bFishing) { IdleSeconds = 0; bWasConnected = false; bRecoveryBlockedByLoad = false; return; }
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
	if (!bCanRest) IdleSeconds = 0;
	const double PreviousIdle = IdleSeconds;
	if (bCanRest) IdleSeconds += Seconds;
	const double RecoverySeconds = FMath::Clamp(IdleSeconds - FMath::Max(PreviousIdle, Settings->RecoveryDelaySeconds), 0.0, Seconds);
	const double Recovery = bRecoveryPending && bCanRest
		? FMath::Min(Maximum - Before, Settings->RecoveryPerSecond * RecoverySeconds) : 0;
	const double Requested = FMath::Min(Before, LastResult.StaminaDrain) - Recovery;
	if (Requested != 0 && !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Requested)))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("AbilityWriteFailed")); return; }
	if (GetOwner()->IsActorBeingDestroyed() || !IsValid(ASC)) return;
	const double After = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	LastPaid = Before - After;
	if (LastPaid > 0 && After <= 0) { IdleSeconds = 0; SetExhausted(true); }
	else if (bExhausted && After >= Maximum * Settings->ExhaustionResumeRatio) SetExhausted(false);
	if (After >= Maximum) bRecoveryPending = false;
	if (Requested != 0 && GetWorld()->GetTimeSeconds() >= NextLogSeconds)
	{
		NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		LogState(TEXT("physical_effort_settled"), Recovery > 0 ? TEXT("Recovery") : Drive.MoveIntent.IsNearlyZero() ? TEXT("Support") : TEXT("Movement"));
	}
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
