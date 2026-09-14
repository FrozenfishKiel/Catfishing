#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Effects/CatFightStaminaRegenEffect.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

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

// 墓碑（2026-09-14）：删除持续力竭、抓握准入及耗尽自动松手；
// Knowledge/Design/设计修改记录.md 2026-09-13 裁决⑥：双段归零只使出力为零，不禁止抓握。
void UCatPhysicalEffortComponent::SettleMovementFromAuthority(const FCatBodyDriveSample& Drive,
	const FVector& IntendedDisplacement, const FVector& ActualDisplacement, double Seconds, bool bGrounded)
{
	LastPaid = 0;
	LastResult = {};
	if (!GetOwner()->HasAuthority() || !FMath::IsFinite(Seconds) || Seconds <= 0) return;
	// The primary's runner is the sole writer of its movement + rod bill, including slack recovery.
	// 墓碑（2026-09-14）：Drive.bFishing 只是持竿电机标记，不能让空竿/等咬钩停回体。
	// 搏斗中不自然恢复；真实主控竿已发布的搏斗状态不依赖 Service 注册或 Runner 查询成功。
	const auto* Pawn = Cast<APawn>(GetOwner());
	auto* Fishing = Drive.bFishing && GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const auto* Rod = Fishing && Pawn ? Fishing->FindRodOperatedBy(Pawn->GetPlayerState()) : nullptr;
	if (Drive.bFishing && Pawn)
		if (const auto* Grab = GetOwner()->FindComponentByClass<UCatPhysicsGrabComponent>())
			for (const bool bLeft : {true, false})
			{
				const auto* HeldRod = Cast<ACatFishingRodActor>(Grab->GetGripTarget(bLeft));
				if (Grab->IsGripping(bLeft) && IsValid(HeldRod)
					&& HeldRod->IsPrimaryOperator(Pawn->GetPlayerState())
					&& HeldRod->GetHolderPawnFromAuthority() == Pawn)
				{
					Rod = HeldRod;
					break;
				}
			}
	const auto* Session = Rod && Fishing ? Fishing->FindActiveSessionByRod(Rod) : nullptr;
	if ((Rod && Rod->GetCarrierConstraintState().bFightActive) || (Session && Session->IsFightRunnerRunning()))
	{
		bWasConnected = false; bRecoveryBlockedByLoad = false;
		SetNaturalRecoveryActive(false);
		return;
	}
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
	const bool bActive = Drive.bCooperative && Drive.bLocomotion && bGrounded && Drive.MaxForce > 0;
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
	const bool bCanRest = !bExerting && !Drive.bUnderLoad;
	// 恢复不再按帧写属性：本组件只裁决「此刻该不该恢复」，5 点/秒的速率和写口都在周期 GE 上。
	const double Requested = FMath::Min(Before, LastResult.StaminaDrain);
	if (Requested != 0 && !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Requested)))
	{ LogState(TEXT("physical_effort_rejected"), TEXT("AbilityWriteFailed")); return; }
	if (GetOwner()->IsActorBeingDestroyed() || !IsValid(ASC)) return;
	const double After = ASC->GetTotalFightStamina();
	LastPaid = Before - After;
	// 墓碑（2026-09-14）：删除 bCooperative 起手、bRecoveryPending、IdleSeconds／RecoveryDelaySeconds
	// 和 ExhaustionResumeRatio 三道恢复闸及再入比例。Knowledge/Design/设计修改记录.md
	// 2026-09-13 裁决⑥：搏斗外不出力就按 5 点/秒回绿，无姿势或静止等待；bUnderLoad 保留。
	const bool bRecovering = bCanRest && After < Maximum;
	SetNaturalRecoveryActive(bRecovering);
	if (Requested != 0 && GetWorld()->GetTimeSeconds() >= NextLogSeconds)
	{
		NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
		LogState(TEXT("physical_effort_settled"), Drive.MoveIntent.IsNearlyZero() ? TEXT("Support") : TEXT("Movement"));
	}
}

// 墓碑（2026-09-14）：裁决⑥取消「先花过才恢复」，NotifyStaminaSpentFromAuthority 回执随之退役。
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

void UCatPhysicalEffortComponent::LogState(FName Event, FName Result) const
{
	const auto* Body = GetOwner()->FindComponentByClass<UCatPhysicalBodyComponent>();
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* Player = Pawn ? Pawn->GetPlayerState() : nullptr;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	const FString Record = FString::Printf(
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s PlayerId=%d BodyId=%s Step=%llu ForceBudgetUE=%.3f IntendedCm=%.6f ProgressCm=%.6f MissingCm=%.6f Paid=%.6f TotalStamina=%.6f Loaded=%d Result=%s"),
		*Event.ToString(), *GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()),
		*GetNameSafe(GetOwner()), Player ? Player->GetPlayerId() : INDEX_NONE, Body ? *Body->GetBodyId().ToString() : TEXT("None"), SettlementSequence,
		GetMaximumForceKgCmS2(), LastResult.IntendedDistanceCentimeters, LastResult.ActualProgressCentimeters,
		LastResult.UnfulfilledDistanceCentimeters, LastPaid, ASC ? ASC->GetTotalFightStamina() : 0,
		bRecoveryBlockedByLoad, *Result.ToString());
	if (Event == TEXT("physical_effort_rejected")) { UE_LOG(LogCatPhysicsGrab, Warning, TEXT("%s"), *Record); }
	else { UE_LOG(LogCatPhysicsGrab, Log, TEXT("%s"), *Record); }
}

void UCatPhysicalEffortComponent::ObserveStaminaFromReplication(double PreviousTotalStamina)
{
	if (!GetOwner() || GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->GetTimeSeconds() < NextLogSeconds) return;
	NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
	const auto* ASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	LastPaid = ASC ? PreviousTotalStamina - ASC->GetTotalFightStamina() : 0;
	LogState(TEXT("physical_effort_stamina_observed"), TEXT("ReplicatedPersonalBalance"));
}
