#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"

#include "AbilitySystem/Tags/CatStateTags.h"
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

// 构造流程：设置组件复制资格；体力属性由 ASC 管理，本组件不另建复制余额。
UCatPhysicalEffortComponent::UCatPhysicalEffortComponent()
{
	SetIsReplicatedByDefault(true);
}

// 推力读取流程：先要求属性和配置有效，再将力量对应的牛顿值换算为引擎力单位；无体力或非法力值均返回零。
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

// 结算流程：清空上次诊断结果，拒绝非权威或非法步长；钓鱼主控仍由原 Runner 独占支付。
// 其余角色校验 ASC 余额后，以实际位移计算未完成运动的消耗，并把用力或承重写为本组件的恢复阻挡来源。
// 恢复资格读取所有来源聚合后的 Tag；随后合并消耗与恢复，经 GE 写入余额，失败仅记拒绝，成功更新结算读数及限频日志。
void UCatPhysicalEffortComponent::SettleMovementFromAuthority(const FCatBodyDriveSample& Drive,
	const FVector& IntendedDisplacement, const FVector& ActualDisplacement, double Seconds, bool bGrounded)
{
	LastPaid = 0;
	LastResult = {};
	if (!GetOwner()->HasAuthority() || !FMath::IsFinite(Seconds) || Seconds <= 0) return;
	// 主控 Runner 独占运动与鱼竿成本结算，也负责松竿恢复。
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
	// 没有起手、姿势、等待或再入阈值；撤销自身的用力来源后，其他来源持有的恢复阻挡仍然有效。
	ASC->SetStateTagsFromAuthority(TEXT("PhysicalEffort.Recovery"), bExerting || Drive.bUnderLoad
		? FGameplayTagContainer(CatStateTags::RecoveryBlocked) : FGameplayTagContainer());
	const bool bCanRest = !ASC->HasMatchingGameplayTag(CatStateTags::RecoveryBlocked);
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

// 日志流程：读取当前身体、玩家和 ASC 余额，与最近运动读数组成同一条关联记录；拒绝事件使用 Warning，其余使用 Log。
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

// 属性观察流程：仅客户端记录本次属性变化量；每秒至多保留一次通知，其间变化不会累计到 LastPaid，日志余额仍读取 ASC 当前值。
void UCatPhysicalEffortComponent::HandleStaminaChanged(const FOnAttributeChangeData& Change)
{
	if (!GetOwner() || GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->GetTimeSeconds() < NextLogSeconds) return;
	NextLogSeconds = GetWorld()->GetTimeSeconds() + 1;
	LastPaid = double(Change.OldValue) - double(Change.NewValue);
	LogState(TEXT("physical_effort_stamina_observed"), TEXT("ReplicatedPersonalBalance"));
}

// 订阅流程：在身体组件开始运行时分别监听绿、黄属性；同一回调不再依赖 AttributeSet 中的业务转发。
void UCatPhysicalEffortComponent::BeginPlay()
{
	Super::BeginPlay();
	ObservedASC = GetOwner()->FindComponentByClass<UCatAbilitySystemComponent>();
	if (auto* ASC = ObservedASC.Get())
	{
		GreenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleStaminaChanged);
		YellowChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleStaminaChanged);
	}
}

// 退出流程：从绑定时的 ASC 移除两个委托，再撤销本组件的限制来源；不删除其他效果持有的恢复限制。
void UCatPhysicalEffortComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ASC = ObservedASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(GreenChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()).Remove(YellowChangedHandle);
		if (GetOwner()->HasAuthority()) ASC->SetStateTagsFromAuthority(TEXT("PhysicalEffort.Recovery"), FGameplayTagContainer());
	}
	Super::EndPlay(Reason);
}
