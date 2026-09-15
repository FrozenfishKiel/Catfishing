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
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"

UCatPhysicalEffortComponent::UCatPhysicalEffortComponent()
{
	SetIsReplicatedByDefault(true);
}

double UCatPhysicalEffortComponent::GetMaximumForceKgCmS2() const
{
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const auto* Settings = GetDefault<UCatPhysicalEffortSettings>();
	const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
	if (!ASC || !Settings->IsValid() || !Balance) return 0;
	const double Stamina = ASC->GetTotalFightStamina();
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double Force = Strength * Balance->ForcePerStrengthNewtons * 100.0;
	return FMath::IsFinite(Stamina) && Stamina > 0 && FMath::IsFinite(Force) && Force > 0 ? Force : 0;
}

void UCatPhysicalEffortComponent::SettleMovementFromAuthority(const FCatBodyDriveSample& Drive,
	const FVector& IntendedDisplacement, const FVector& ActualDisplacement, double Seconds, bool bGrounded)
{
	LastPaid = 0;
	LastResult = {};
	if (!GetOwner()->HasAuthority() || !FMath::IsFinite(Seconds) || Seconds <= 0) return;
	// The primary's runner is the sole writer of its movement + rod bill, including slack recovery.
	// 持竿/等待咬钩不等于搏斗；同时检查已发布竿状态和实际 Runner，覆盖注册和阶段交接窗口。
	const auto* Pawn = Cast<APawn>(GetOwner());
	auto* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const ACatFishingRodActor* Rod = Fishing && Pawn ? Fishing->FindRodOperatedBy(Pawn->GetPlayerState()) : nullptr;
	if (const auto* Grab = GetOwner()->FindComponentByClass<UCatPhysicsGrabComponent>(); Grab && Pawn)
		for (bool Left : {true, false})
			if (const auto* HeldRod = Cast<ACatFishingRodActor>(Grab->GetGripTarget(Left));
				Grab->IsGripping(Left) && IsValid(HeldRod) && HeldRod->IsPrimaryOperator(Pawn->GetPlayerState())
				&& HeldRod->GetHolderPawnFromAuthority() == Pawn) { Rod = HeldRod; break; }
	const auto* Session = Rod && Fishing ? Fishing->FindActiveSessionByRod(Rod) : nullptr;
	const bool bInFight = (Rod && Rod->GetCarrierConstraintState().bFightActive) || (Session && Session->IsFightRunnerRunning());
	if (bInFight != bWasInFight)
	{
		bWasInFight = bInFight;
		LogState(TEXT("physical_effort_fight_gate"), bInFight ? TEXT("FightOwnsPayment") : TEXT("OutsideFight"));
	}
	if (bInFight) return;
	auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const auto* Settings = GetDefault<UCatPhysicalEffortSettings>();
	if (!ASC || !ASC->GetAvatarActor() || !Settings->IsValid()) return;
	const double Before = ASC->GetTotalFightStamina();
	const double Maximum = ASC->GetTotalFightStaminaCapacity();
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
	FCatIntentMotionInput Input;
	const bool bActive = !Drive.bFishing && Drive.bCooperative && Drive.bLocomotion && bGrounded && Drive.MaxForce > 0;
	Input.IntendedDisplacementCentimeters = bActive ? IntendedDisplacement : FVector::ZeroVector;
	Input.ActualDisplacementCentimeters = ActualDisplacement;
	Input.StaminaPerUnfulfilledMeter = Settings->StaminaPerUnfulfilledMeter;
	if (!FCatIntentMotionModel::ComputeDrain(Input, LastResult))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("InvalidMotion")); return; }
	const bool bExerting = bActive && !IntendedDisplacement.IsNearlyZero();
	if (Drive.bUnderLoad != bRecoveryBlockedByLoad)
	{
		bRecoveryBlockedByLoad = Drive.bUnderLoad;
		LogState(TEXT("physical_effort_recovery_gate"), Drive.bUnderLoad ? TEXT("Loaded") : TEXT("Unloaded"));
	}
	// 2026-09-13 裁决⑥：没有起手、姿势、等待或再入阈值；受力语义继续阻止恢复。
	const bool bCanRest = !bExerting && !Drive.bUnderLoad;
	const double Recovery = bCanRest ? FMath::Min(Maximum - Before, Settings->RecoveryPerSecond * Seconds) : 0.0;

	const double Requested = FMath::Min(Before, LastResult.StaminaDrain) - Recovery;
	if (Requested != 0 && !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Requested)))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("AbilityWriteFailed")); return; }
	if (GetOwner()->IsActorBeingDestroyed() || !IsValid(ASC)) return;
	const double After = ASC->GetTotalFightStamina();
	LastPaid = Before - After;
	if (Requested != 0 && GetWorld()->GetTimeSeconds() >= NextLogSeconds)
	{
		NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		LogState(TEXT("physical_effort_settled"), Recovery > 0 ? TEXT("Recovery") : Drive.MoveIntent.IsNearlyZero() ? TEXT("Support") : TEXT("Movement"));
	}
}

void UCatPhysicalEffortComponent::LogState(FName Event, FName Result) const
{
	const auto* Body = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* Player = Pawn ? Pawn->GetPlayerState() : nullptr;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const FString Record = FString::Printf(
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s PlayerId=%d BodyId=%s Step=%llu ForceBudgetUE=%.3f IntendedCm=%.6f ProgressCm=%.6f MissingCm=%.6f Paid=%.6f TotalStamina=%.6f GreenStamina=%.6f YellowStamina=%.6f Loaded=%d InFight=%d Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), Player ? Player->GetPlayerId() : INDEX_NONE, Body ? *Body->GetBodyId().ToString() : TEXT("None"), SettlementSequence,
		GetMaximumForceKgCmS2(), LastResult.IntendedDistanceCentimeters, LastResult.ActualProgressCentimeters,
		LastResult.UnfulfilledDistanceCentimeters, LastPaid, ASC ? ASC->GetTotalFightStamina() : 0,
		ASC ? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0,
		ASC ? ASC->GetYellowFightStamina() : 0, bRecoveryBlockedByLoad, bWasInFight, *Result.ToString());
	if (Event == TEXT("physical_effort_rejected")) { UE_LOG(LogCatPhysicsGrab, Warning, TEXT("%s"), *Record); }
	else { UE_LOG(LogCatPhysicsGrab, Log, TEXT("%s"), *Record); }
}

void UCatPhysicalEffortComponent::ObserveStaminaFromReplication(double PreviousStamina)
{
	if (!GetOwner() || GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->GetTimeSeconds() < NextLogSeconds) return;
	NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	LastPaid = ASC ? PreviousStamina - ASC->GetTotalFightStamina() : 0;
	LogState(TEXT("physical_effort_stamina_observed"), TEXT("ReplicatedPersonalBalance"));
}
