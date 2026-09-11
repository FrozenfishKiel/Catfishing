#include "AbilitySystem/Config/CatPhysicalEffortSettings.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"

#include "Condition/CatConditionComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "Framework/Game/CatfishingPlayerController.h"

#include "GameFramework/PlayerState.h"

#include "FishContainers/CatFishPickupSettings.h"
#include "FishContainers/World/CatWorldSurfaceResolver.h"
#include "Logging/CatLog.h"
#include "Misc/ScopeExit.h"
#include "Fishing/Debug/CatFishingMotionDiagnostics.h"

namespace
{
	const TCHAR* SimulationRejectReasonName(const ECatFightSimulationRejectReason Reason)
	{
		switch (Reason)
		{
		case ECatFightSimulationRejectReason::InvalidConfig: return TEXT("InvalidConfig");
		case ECatFightSimulationRejectReason::InvalidState: return TEXT("InvalidState");
		case ECatFightSimulationRejectReason::InvalidRodConstraint: return TEXT("InvalidRodConstraint");
		case ECatFightSimulationRejectReason::InvalidFishDirection: return TEXT("InvalidFishDirection");
		case ECatFightSimulationRejectReason::InvalidResolvedResult: return TEXT("InvalidResolvedResult");
		default: return TEXT("None");
		}
	}
}

bool UCatFishingFightRunner::InitializeFromAuthority(const FCatFishingFightRunnerInit& Init)
{
	ACatFishingSession* SessionActor = Init.Session.Get();
	// 一次性初始化守卫：已初始化过、Session 无效/非权威、任何依赖弱引用失效、配置非法都直接拒绝。
	if (bInitialized || !SessionActor || !SessionActor->HasAuthority() || !Init.FishActor.IsValid()
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid() || !Init.PrimaryPlayerState.IsValid()
		|| !Init.WaterRegion.IsValid()
		|| !Init.Config.IsValid() || Init.RandomSeed == 0
		|| Init.InitialInputSequence < 0
		|| !Init.SteeringConfig.IsValid() || !Init.BehaviorStateTree)
	{
		return false;
	}
	// 逐一拷贝依赖引用与配置到成员变量，Runner 从此持有自己的一份快照，不再依赖调用方保留 Init 结构体。
	Session = Init.Session;
	FishActor = Init.FishActor;
	RodActor = Init.RodActor;
	AbilitySystem = Init.AbilitySystem;
	WaterRegion = Init.WaterRegion;
	Config = Init.Config;
	State = Init.InitialState;
	DiagnosticFixedStepSequence = 0;
	LastFixedStepDiagnosticWorldSeconds = -1.0;
	State.bOperatorPresent = true;
	bFishBeached = false;
	SteeringConfig = Init.SteeringConfig;
	BehaviorStateTree = Init.BehaviorStateTree;
	// 记录鱼的初始体力（至少为一个极小正数，避免后面用它做分母时除零），用于低体力判定的比例基准。
	InitialFishStamina = FMath::Max(State.FishStamina, UE_DOUBLE_SMALL_NUMBER);
	// 用服务器分配的种子初始化随机流，保证同一次搏斗在权威端是确定性可复算的。
	// 同一固定步行为随机流冻结目标出力、时长和侧向；诊断不得消耗随机数。
	SteeringRandom.Initialize(static_cast<int32>(Init.RandomSeed ^ 0x9E3779B9u));
	// StateTree 的第一个可选状态同样是向外发力；这里先放一个合法初值，StartLogic 同步进入状态时会从唯一写口覆盖。
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	if (!BindPrimaryOperatorFromAuthority(Init.PrimaryPlayerState.Get(),
		Init.bInitialPullHeld, Init.bInitialSlackHeld, Init.InitialInputSequence))
	{
		return false;
	}
	Config.PrimaryOperatorCatStrength = 0.0;
	RefreshCatAction();
	bInitialized = true;
	return true;
}

bool UCatFishingFightRunner::Start()
{
	ACatFishingSession* SessionActor = Session.Get();
	UWorld* World = SessionActor ? SessionActor->GetWorld() : nullptr;
	// 必须先 InitializeFromAuthority 成功、尚未在跑、World 有效且是服务器权威，否则拒绝启动。
	if (!bInitialized || bRunning || !World || !SessionActor->HasAuthority()) return false;
	bRunning = true;
	// 行为 StateTree 必须先成功接管高层意图，固定步模拟才允许启动。树只在服务器运行；客户端消费复制后的 MotionIntent。
	ACatFishEncounterActor* Encounter = FishActor.Get();
	if (!Encounter || !Encounter->StartFishBehaviorFromAuthority(BehaviorStateTree, this))
	{
		bRunning = false;
		return false;
	}
	// 从搏斗启动这一帧起就把控制器旋转视为“意图”，避免首个固定步到来前竿尖仍瞬移跟随。
	ACatFishingRodActor* Rod = RodActor.Get();
	const FVector InitialPullDirection = Rod
		? Encounter->GetActorLocation() - Rod->GetRodTipWorldTransform().GetLocation()
		: FVector::ZeroVector;
	if (!Rod || (Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held
		&& !Rod->SetFightConstraintObservationFromAuthority(InitialPullDirection, 0.0, 0.0, true, 0.0, Config.PrimaryOperatorCatStrength, InitialPullDirection.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector))))
	{
		Encounter->StopFishBehaviorFromAuthority();
		bRunning = false;
		return false;
	}
	// Produce each fixed-step force before the same frame's Chaos integration consumes it.
	// A post-physics timer would permanently delay catch-up force by an entire hitch.
	RotationEffortSampler.Reset(Rod->GetAuthoritativeRotationEffortSnapshot());
	auto* PhysicalRod = Rod->GetPhysicalRodComponent();
	if (!PhysicalRod || !PhysicalRod->IsReady())
	{
		Encounter->StopFishBehaviorFromAuthority(); bRunning = false; return false;
	}
	PendingFixedStepSeconds = 0;
	PhysicalRod->RefreshInputTickPrerequisites();
	PhysicsFrameHandle = PhysicalRod->BeforePhysicsForces.AddUObject(this, &ThisClass::HandlePhysicsFrame);
	PhysicsReceiverUnavailableHandle = PhysicalRod->PhysicsReceiverUnavailable.AddUObject(this, &ThisClass::HandlePhysicsReceiverUnavailable);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fight_scheduler_started SessionId=%s RodActorId=%s FixedStepSeconds=%.6f Scheduler=PhysicalRodPrePhysics World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*SessionActor->GetSnapshot().FishingSessionId.ToString(), *Rod->GetPresentationState().RodActorId.ToString(),
		Config.FixedStepSeconds, *GetNameSafe(World), int32(World->GetNetMode()), int32(SessionActor->GetLocalRole()));
	return true;
}

void UCatFishingFightRunner::HandlePhysicsFrame(const float DeltaSeconds)
{
	if (!bRunning || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0) return;
	PendingFixedStepSeconds += DeltaSeconds;
	while (bRunning && PendingFixedStepSeconds + 1.e-8 >= Config.FixedStepSeconds)
	{
		PendingFixedStepSeconds = FMath::Max(0.0, PendingFixedStepSeconds - Config.FixedStepSeconds);
		HandleFixedStep();
	}
}

void UCatFishingFightRunner::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

void UCatFishingFightRunner::HandlePhysicsReceiverUnavailable()
{
	if (!bRunning) return;
	Stop();
	if (ACatFishingSession* SessionActor = Session.Get(); SessionActor && !SessionActor->IsTerminal())
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("PhysicalRodReceiverUnavailable"));
}

void UCatFishingFightRunner::Stop()
{
	// FinalizeSession 和稍后的 EndPlay 都会到这里；旧会话不能再次撤销同竿新一场发布的载荷。
	if (!bRunning) return;
	bRunning = false; // 外部清理回调之前关闭运行态，重入 Stop 也只能执行一次。
	if (ACatFishEncounterActor* Encounter = FishActor.Get())
	{
		Encounter->StopFishBehaviorFromAuthority();
	}
	// Unbind before any external teardown callback. There is exactly one scheduler for this runner.
	if (ACatFishingRodActor* Rod = RodActor.Get())
	{
		if (auto* PhysicalRod = Rod->GetPhysicalRodComponent())
		{
			PhysicalRod->BeforePhysicsForces.Remove(PhysicsFrameHandle);
			PhysicalRod->PhysicsReceiverUnavailable.Remove(PhysicsReceiverUnavailableHandle);
		}
	}
	PhysicsFrameHandle.Reset();
	PhysicsReceiverUnavailableHandle.Reset();
	PendingFixedStepSeconds = 0;
	if (ACatFishingRodActor* Rod = RodActor.Get())
	{
		Rod->ClearFightConstraintAndLoadFromAuthority(Session.IsValid() ? Session->GetSnapshot().FishingSessionId : FGuid());
	}
	const ACatFishingSession* SessionActor = Session.Get();
	const ACatFishingRodActor* Rod = RodActor.Get();
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_fight_runner_stopped SessionId=%s RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=Stopped Scheduler=PhysicalRodPrePhysics"),
		SessionActor ? *SessionActor->GetSnapshot().FishingSessionId.ToString() : TEXT("None"),
		Rod ? *Rod->GetPresentationState().RodActorId.ToString() : TEXT("None"),
		*GetNameSafe(SessionActor ? SessionActor->GetWorld() : nullptr), SessionActor ? int32(SessionActor->GetNetMode()) : INDEX_NONE,
		SessionActor && SessionActor->HasAuthority(), SessionActor ? int32(SessionActor->GetLocalRole()) : INDEX_NONE);
}

FCatFightOperatorRuntime* UCatFishingFightRunner::GetPrimaryOperator()
{
	return State.bOperatorPresent && OperatorState.PlayerState.IsValid() && OperatorState.Character.IsValid() ? &OperatorState : nullptr;
}

bool UCatFishingFightRunner::BindPrimaryOperatorFromAuthority(APlayerState* PlayerState,
	const bool bInitialPullHeld, const bool bInitialSlackHeld, const int64 InitialInputSequence)
{
	auto* Character = PlayerState ? Cast<ACatCharacter>(PlayerState->GetPawn()) : nullptr;
	auto* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	auto* Physical = Character ? Character->GetPhysicalBodyComponent() : nullptr;
	const double Strength = ASC ? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()) : 0.0;
	const double Stamina = ASC ? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0.0;
	const double Maximum = ASC ? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()) : 0.0;
	if (!PlayerState || !Character || !ASC || !Physical || !Physical->GetBody() || InitialInputSequence < 0
		|| !FMath::IsFinite(Strength) || Strength < 0.0 || !FMath::IsFinite(Stamina) || Stamina < 0.0
		|| !FMath::IsFinite(Maximum) || Maximum <= 0.0 || Stamina > Maximum) return false;
	OperatorState = {};
	OperatorState.PlayerState = PlayerState; OperatorState.Character = Character; OperatorState.AbilitySystem = ASC;
	OperatorState.BaseFishingStrength = Strength; OperatorState.ActiveFishingStrength = Stamina > 0.0 ? Strength : 0.0;
	OperatorState.StaminaMaximum = Maximum; OperatorState.LastInputSequence = InitialInputSequence;
	OperatorState.bPullHeld = bInitialPullHeld; OperatorState.bSlackHeld = bInitialSlackHeld;
	OperatorState.LastSampledPosition = Physical->GetBody()->GetComponentLocation();
	if (const auto* Movement = Cast<UCatCharacterMovementComponent>(Character->GetCharacterMovement()))
		OperatorState.LastSampledMotionCorrection = Movement->GetTotalMotionCorrection();
	OperatorState.LastMovementSampleWorldSeconds = Character->GetWorld()->GetTimeSeconds();
	OperatorState.LastBodyResetEpoch = Physical->GetResetEpoch(); OperatorState.bHasSampledPosition = true;
	OperatorState.ControlEpoch = RodActor.IsValid() ? RodActor->GetControlEpoch() : 0;
	AbilitySystem = ASC;
	return true;
}

bool UCatFishingFightRunner::RefreshPrimaryOperatorFromRod()
{
	const auto* Rod = RodActor.Get();
	if (!Rod || !Session.IsValid()) return false;
	APlayerState* Primary = Rod->GetPresentationState().OperatorPlayerState;
	if (!Primary)
	{
		State.bOperatorPresent = false; OperatorState = {}; AbilitySystem.Reset();
		return true;
	}
	// Only Service's explicit owner command may bind or resume the session. Physical helpers are never inspected.
	return Primary == Rod->GetPresentationState().OwnerPlayerState && Primary == OperatorState.PlayerState
		&& Primary->GetPawn() == OperatorState.Character && State.bOperatorPresent;
}

bool UCatFishingFightRunner::UpdateOperatorIntentAndProperties()
{
	bOperatorSettlementPending = false;
	FrozenOperatorMovementSamples.Reset(); FrozenOperatorAbilitySystem.Reset();
	FrozenOperatorStamina = FrozenOperatorStaminaMaximum = 0.0;
	bFrozenOperatorUnderLoad = false;
	Config.PrimaryOperatorCatStrength = 0.0;
	OperatorSupportAlignment = 0.0;
	if (!State.bOperatorPresent)
	{
		State.CatStamina = 0.0; RefreshCatAction(); bOperatorSettlementPending = true;
		return Config.IsValid();
	}
	auto* ASC = OperatorState.AbilitySystem.Get();
	auto* Character = OperatorState.Character.Get();
	const auto* Physical = Character ? Character->GetPhysicalBodyComponent() : nullptr;
	if (!ASC || !Physical || !Physical->GetBody()) return false;
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	FrozenOperatorStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	FrozenOperatorStaminaMaximum = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
	if (!FMath::IsFinite(Strength) || Strength < 0.0 || !FMath::IsFinite(FrozenOperatorStamina) || FrozenOperatorStamina < 0.0
		|| !FMath::IsFinite(FrozenOperatorStaminaMaximum) || FrozenOperatorStaminaMaximum <= 0.0 || FrozenOperatorStamina > FrozenOperatorStaminaMaximum) return false;
	OperatorState.BaseFishingStrength = Strength;
	OperatorState.ActiveFishingStrength = Physical->IsLocomotionEnabled() && FrozenOperatorStamina > 0.0 ? Strength : 0.0;
	OperatorState.StaminaMaximum = FrozenOperatorStaminaMaximum;
	Config.PrimaryOperatorCatStrength = OperatorState.ActiveFishingStrength;
	Config.PrimaryOperatorMassKilograms = Physical->GetBody()->GetBodyInstance()->GetBodyMass();
	Config.CatStaminaMaximum = FrozenOperatorStaminaMaximum;
	State.CatStamina = FrozenOperatorStamina;
	FrozenOperatorAbilitySystem = ASC;
	bFrozenOperatorUnderLoad = Physical->HasExternalLoadFromAuthority();
	const FVector Intent = FVector(Physical->GetMoveIntent().X, Physical->GetMoveIntent().Y, 0.0).GetClampedToMaxSize(1.0);
	const FVector ResistanceDirection = ((RodActor.IsValid() ? RodActor->GetRodTipWorldTransform().GetLocation() : FVector::ZeroVector)
		- State.FishWorldPosition).GetSafeNormal2D(UE_DOUBLE_SMALL_NUMBER, -FVector::ForwardVector);
	OperatorSupportAlignment = 1.0 - Intent.Size() + FVector::DotProduct(Intent, ResistanceDirection);
	const FVector Position = Physical->GetBody()->GetComponentLocation();
	const auto* Movement = Cast<UCatCharacterMovementComponent>(Character->GetCharacterMovement());
	const FVector Correction = Movement ? Movement->GetTotalMotionCorrection() : FVector::ZeroVector;
	const double Now = Character->GetWorld()->GetTimeSeconds();
	if (!OperatorState.bHasSampledPosition || OperatorState.LastBodyResetEpoch != Physical->GetResetEpoch())
	{
		OperatorState.LastSampledPosition = Position; OperatorState.LastBodyResetEpoch = Physical->GetResetEpoch();
		OperatorState.LastSampledMotionCorrection = Correction;
		OperatorState.PendingMovementSamples.Reset(); OperatorState.LastMovementSampleWorldSeconds = Now;
		OperatorState.bHasSampledPosition = true;
	}
	else if (Now > OperatorState.LastMovementSampleWorldSeconds)
	{
		auto& Sample = OperatorState.PendingMovementSamples.AddDefaulted_GetRef();
		Sample.DurationSeconds = Now - OperatorState.LastMovementSampleWorldSeconds;
		Sample.MoveIntentWorld = Intent; Sample.MaximumMoveSpeedCentimetersPerSecond = Physical->MaxMovementSpeedCmS;
		if (!Intent.IsNearlyZero())
		{
			const FVector Direction = Intent.GetSafeNormal();
			Sample.ActualDisplacementCentimeters = Direction * FVector::DotProduct(Position - OperatorState.LastSampledPosition
				- (Correction - OperatorState.LastSampledMotionCorrection), Direction);
		}
		OperatorState.LastSampledPosition = Position; OperatorState.LastMovementSampleWorldSeconds = Now;
		OperatorState.LastSampledMotionCorrection = Correction;
	}
	double RemainingSeconds = Config.FixedStepSeconds;
	while (RemainingSeconds > UE_DOUBLE_SMALL_NUMBER && !OperatorState.PendingMovementSamples.IsEmpty())
	{
		auto& Pending = OperatorState.PendingMovementSamples[0];
		const double SliceSeconds = FMath::Min(RemainingSeconds, Pending.DurationSeconds);
		auto Slice = Pending; Slice.DurationSeconds = SliceSeconds;
		Slice.ActualDisplacementCentimeters *= SliceSeconds / Pending.DurationSeconds;
		FrozenOperatorMovementSamples.Add(Slice);
		Pending.ActualDisplacementCentimeters -= Slice.ActualDisplacementCentimeters;
		Pending.DurationSeconds -= SliceSeconds; RemainingSeconds -= SliceSeconds;
		if (Pending.DurationSeconds <= UE_DOUBLE_SMALL_NUMBER) OperatorState.PendingMovementSamples.RemoveAt(0);
	}
	RefreshCatAction();
	bOperatorSettlementPending = Config.IsValid();
	return bOperatorSettlementPending;
}

bool UCatFishingFightRunner::ApplyOperatorStaminaChanges(const FCatFightStepResult& Step)
{
	if (!bOperatorSettlementPending) return false;
	bOperatorSettlementPending = false;
	LastOperatorStaminaDrain = 0.0;
	if (!State.bOperatorPresent) return true;
	auto* ASC = FrozenOperatorAbilitySystem.Get();
	if (!IsValid(ASC) || !ASC->GetOwner() || ASC->GetOwner()->IsActorBeingDestroyed()
		|| ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) != FrozenOperatorStamina) return false;
	const bool bFreeEffort = Step.bSlackRecoveryActive || State.bFishExhausted;
	double MovementDrain = 0.0;
	if (!bFreeEffort)
		for (const auto& Sample : FrozenOperatorMovementSamples)
		{
			FCatFightOperatorMovementCostInput Cost;
			Cost.MoveIntentWorld = Sample.MoveIntentWorld; Cost.ActualDisplacementCentimeters = Sample.ActualDisplacementCentimeters;
			Cost.MaximumMoveSpeedCentimetersPerSecond = Sample.MaximumMoveSpeedCentimetersPerSecond; Cost.FixedStepSeconds = Sample.DurationSeconds;
			Cost.ActiveStrength = OperatorState.ActiveFishingStrength;
			Cost.StaminaPerUnfulfilledMeter = GetDefault<UCatPhysicalEffortSettings>()->StaminaPerUnfulfilledMeter;
			Cost.MovementStaminaMultiplier = Config.CatMovementStaminaMultiplier;
			FCatFightOperatorMovementCostResult Result;
			if (!FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(Cost, Result)) return false;
			MovementDrain += Result.StaminaDrain;
		}
	const double RodDrain = bFreeEffort ? 0.0 : Step.GetRodActionStaminaDrain();
	const double RequestedDrain = MovementDrain + RodDrain;
	if (!FMath::IsFinite(RequestedDrain) || RequestedDrain < 0.0) return false;
	const double Paid = FMath::Min(FrozenOperatorStamina, RequestedDrain);
	const bool bRecoveryLoaded = bFrozenOperatorUnderLoad || !Step.RodLineForceNewtons.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER);
	const double Recovery = Step.bSlackRecoveryActive && !bRecoveryLoaded
		? FMath::Min(FrozenOperatorStaminaMaximum - FrozenOperatorStamina, Config.SlackStaminaRegenPerSecond * Config.FixedStepSeconds) : 0.0;
	LastOperatorStaminaDrain = Paid - Recovery;
	const float AttributeDelta = static_cast<float>(-LastOperatorStaminaDrain);
	if (AttributeDelta != 0.0f && !ASC->ApplyFishingStaminaDelta(AttributeDelta)) return false;
	if (!IsValid(ASC) || !ASC->GetOwner() || ASC->GetOwner()->IsActorBeingDestroyed()) return true;
	State.CatStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	LastOperatorStaminaDrain = FrozenOperatorStamina - State.CatStamina;
	if (auto* OwnerSession = Session.Get())
	{
		OwnerSession->PublishPrimarySummaryFromAuthority(OperatorState.ActiveFishingStrength, State.CatStamina,
			FrozenOperatorStaminaMaximum, true);
		if ((MovementDrain > 0.0 || RodDrain > 0.0 || Recovery > 0.0 || (Step.bSlackRecoveryActive && bRecoveryLoaded)) && OwnerSession->GetWorld()->GetTimeSeconds() >= NextStaminaDiagnosticSeconds)
		{
			NextStaminaDiagnosticSeconds = OwnerSession->GetWorld()->GetTimeSeconds() + 1.0;
			UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_primary_stamina_settled SessionId=%s PlayerId=%d MovementDrain=%.5f RodDrain=%.5f ActualPaid=%.5f Recovery=%.5f RecoveryLoaded=%d Remaining=%.5f World=%s NetMode=%d Authority=1 LocalRole=%d"),
				*OwnerSession->GetSnapshot().FishingSessionId.ToString(), OperatorState.PlayerState.IsValid() ? OperatorState.PlayerState->GetPlayerId() : INDEX_NONE,
				MovementDrain, RodDrain, Paid, Recovery, bRecoveryLoaded, State.CatStamina, *GetNameSafe(OwnerSession->GetWorld()), int32(OwnerSession->GetNetMode()), int32(OwnerSession->GetLocalRole()));
		}
	}
	return true;
}
void UCatFishingFightRunner::RefreshCatAction()
{
	const FCatFightOperatorRuntime* Primary = GetPrimaryOperator();
	State.CatAction = !State.bOperatorPresent || !Primary
		? ECatFightCatAction::Slack
		: Primary->bSlackHeld && !FCatFishingFightSimulator::IsLineAtMaximum(Config, State.LineLengthCentimeters)
			? ECatFightCatAction::Slack
		: Primary->bPullHeld ? ECatFightCatAction::Pull : ECatFightCatAction::None;
}

bool UCatFishingFightRunner::UpdateFishBehaviorForCurrentOperator(const bool bRodHeld)
{
	const bool bEscape = FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Config, State, bRodHeld);
	RefreshCatAction();
	State.MotionIntent = State.bFishExhausted ? ECatFishMotionIntent::AutoHauling
		: bEscape ? ECatFishMotionIntent::StrugglingOutward : State.MotionIntent;
	if (bEscape) State.CatAction = ECatFightCatAction::None;
	if (bEscape != bLastExhaustedCatEscapeDiagnosticActive)
	{
		if (const ACatFishingSession* SessionActor = Session.Get())
		{
			const FCatFightOperatorRuntime* Primary = GetPrimaryOperator();
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_exhausted_cat_escape_changed SessionId=%s RodActor=%s CatActor=%s PlayerId=%d "
					"Active=%s CatStamina=%.6f CombinedStrength=%.3f FishStamina=%.3f SpeedMultiplier=%.3f "
					"Behavior=%s EffectiveIntent=%s Result=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
				*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(RodActor.Get()), *GetNameSafe(Primary ? Primary->Character.Get() : nullptr),
				Primary && Primary->PlayerState.IsValid() ? Primary->PlayerState->GetPlayerId() : INDEX_NONE,
				bEscape ? TEXT("true") : TEXT("false"), State.CatStamina, Config.PrimaryOperatorCatStrength,
				State.FishStamina, Config.ExhaustedCatEscapeSpeedMultiplier,
				*UEnum::GetValueAsString(SteeringState.Behavior), *UEnum::GetValueAsString(State.MotionIntent),
				bEscape ? TEXT("OutwardRushWithLockedLine") : TEXT("EscapeOverrideReleased"),
				*GetNameSafe(SessionActor->GetWorld()), static_cast<int32>(SessionActor->GetNetMode()),
				static_cast<int32>(SessionActor->GetLocalRole()));
		}
		bLastExhaustedCatEscapeDiagnosticActive = bEscape;
	}
	return bEscape;
}

bool UCatFishingFightRunner::SetReeling(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bInReeling)
{
	FCatFightOperatorRuntime* Participant = GetPrimaryOperator();
	if (!Participant && RefreshPrimaryOperatorFromRod())
	{
		Participant = GetPrimaryOperator();
	}
	if (!bInitialized || !bRunning || !Participant || Participant->PlayerState != InputPlayerState || InputSequence <= Participant->LastInputSequence)
	{
		return false;
	}
	Participant->LastInputSequence = InputSequence;
	Participant->bPullHeld = bInReeling;
	RefreshCatAction(); // 按最新的拖/放状态重新计算本步的猫动作
	return true;
}

bool UCatFishingFightRunner::SetSlacking(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bInSlacking)
{
	FCatFightOperatorRuntime* Participant = GetPrimaryOperator();
	if (!Participant && RefreshPrimaryOperatorFromRod())
	{
		Participant = GetPrimaryOperator();
	}
	if (!bInitialized || !bRunning || !Participant || Participant->PlayerState != InputPlayerState
		|| InputSequence <= Participant->LastInputSequence)
	{
		return false;
	}
	Participant->LastInputSequence = InputSequence;
	Participant->bSlackHeld = bInSlacking;
	RefreshCatAction();
	return true;
}

bool UCatFishingFightRunner::IsSlackInputHeldForAuthority(APlayerState* InputPlayerState) const
{
	return State.bOperatorPresent && OperatorState.PlayerState == InputPlayerState && OperatorState.bSlackHeld;
}

bool UCatFishingFightRunner::BeginUnattendedSlackFromAuthority()
{
	if (!bInitialized) return false;
	State.bOperatorPresent = false;
	OperatorState = {};
	State.StrongConfrontationBuildUpSeconds = 0.0;
	AbilitySystem.Reset();
	RefreshCatAction();
	return true;
}

bool UCatFishingFightRunner::SetFishExhaustedFromAuthority()
{
	if (!bInitialized || !bRunning)
	{
		return false;
	}
	// 力竭仍属于同一场搏斗：立即撤销鱼的驱动力，但保留主控瞄准域，不能转成前战操竿。
	if (ACatFishingRodActor* Rod = RodActor.Get())
	{
		if (Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held)
		{
			const FCatFishingCarrierConstraintState Current = Rod->GetCarrierConstraintState();
			if (!Rod->SetFightConstraintObservationFromAuthority(Current.PullDirection, 0.0, 0.0, true, 0.0, Current.CatTorqueCapacityStrengthMeters, Current.RodPullAxis))
			{
				const ACatFishingSession* SessionActor = Session.Get();
				UE_LOG(LogCatFishing, Warning,
					TEXT("Event=fishing_exhausted_constraint_rejected SessionId=%s RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=StateUnchanged"),
					SessionActor ? *SessionActor->GetSnapshot().FishingSessionId.ToString() : TEXT("None"),
					*Rod->GetPresentationState().RodActorId.ToString(), *GetNameSafe(Rod->GetWorld()), int32(Rod->GetNetMode()),
					Rod->HasAuthority(), int32(Rod->GetLocalRole()));
				return false;
			}
		}
		else
		{
			Rod->ClearFightConstraintAndLoadFromAuthority(Session.IsValid() ? Session->GetSnapshot().FishingSessionId : FGuid());
		}
	}
	State.bFishExhausted = true;
	State.FishEffortRatio = 0.0;
	State.FishStamina = 0.0;
	State.FishVelocityCentimetersPerSecond = FVector::ZeroVector;
	State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	State.StrongConfrontationBuildUpSeconds = 0.0;
	if (ACatFishEncounterActor* Encounter = FishActor.Get())
	{
		Encounter->StopFishBehaviorFromAuthority();
		if (!Encounter->ApplyFightStepFromAuthority(ECatFishMotionIntent::AutoHauling,
			State.LineLengthCentimeters, Encounter->GetActorLocation(),
			static_cast<float>(Config.FixedStepSeconds), 0.0f, 0.0f, 0.0f, false,
			bFishBeached, Encounter->GetPresentationState().GroundNormal))
		{
			return false;
		}
	}
	return true;
}

bool UCatFishingFightRunner::ResumeOwnerFromAuthority(APlayerState* NewPlayerState, UCatAbilitySystemComponent* NewAbilitySystem,
	const double NewCatStrength, const double NewCatStaminaMaximum, const double NewCatStamina,
	const int64 InitialInputSequence, const bool bInitialPullHeld, const bool bInitialSlackHeld)
{
	const auto* Rod = RodActor.Get();
	if (!bInitialized || !Rod || !NewPlayerState || NewPlayerState != Rod->GetPresentationState().OwnerPlayerState
		|| !Rod->IsPrimaryOperator(NewPlayerState) || !NewAbilitySystem || InitialInputSequence < 0
		|| !FMath::IsFinite(NewCatStrength) || NewCatStrength < 0.0 || !FMath::IsFinite(NewCatStaminaMaximum) || NewCatStaminaMaximum <= 0.0
		|| !FMath::IsFinite(NewCatStamina) || NewCatStamina < 0.0 || NewCatStamina > NewCatStaminaMaximum) return false;
	if (!BindPrimaryOperatorFromAuthority(NewPlayerState, bInitialPullHeld, bInitialSlackHeld, InitialInputSequence)) return false;
	if (OperatorState.AbilitySystem != NewAbilitySystem) return false;
	State.bOperatorPresent = true; State.CatStamina = NewCatStamina; State.StrongConfrontationBuildUpSeconds = 0.0;
	Config.CatStaminaMaximum = NewCatStaminaMaximum;
	Config.PrimaryOperatorCatStrength = NewCatStamina > 0.0 ? NewCatStrength : 0.0;
	RefreshCatAction();
	return true;
}
bool UCatFishingFightRunner::BeginFishBehaviorFromStateTree(const ECatFishBehavior Behavior)
{
	if (!bInitialized || !bRunning || State.bFishExhausted) return false;
	const ACatFishingRodActor* Rod = RodActor.Get();
	const ACatFishingSession* SessionActor = Session.Get();
	if (!Rod || !SessionActor || !SessionActor->HasAuthority()) return false;
	FVector Outward = (State.FishWorldPosition - Rod->GetRodTipWorldTransform().GetLocation()).GetSafeNormal2D();
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector;
	const ECatFishBehavior PreviousBehavior = SteeringState.Behavior;
	// 进入行为会清除累计计时，诊断必须保留选边之前的事实，不能把重置后的零当作原因。
	const double PreviousElapsed = SteeringState.BehaviorElapsedSeconds;
	const double PreviousDuration = SteeringState.BehaviorDurationSeconds;
	const double PreviousBlocked = SteeringState.BlockedSeconds;
	const double PreviousBoutElapsed = SteeringState.ActiveBoutElapsedSeconds;
	const double PreviousBoutDuration = SteeringState.ActiveBoutDurationSeconds;
	const double StaminaRatio = FMath::Clamp(State.FishStamina / InitialFishStamina, 0.0, 1.0);
	const bool bAccepted = SteeringState.bInitialized
		? FCatFishSteeringModel::BeginBehavior(SteeringConfig, Outward, Behavior, StaminaRatio, SteeringRandom, SteeringState)
		: FCatFishSteeringModel::Initialize(SteeringConfig, Outward, Behavior, StaminaRatio, SteeringRandom, SteeringState);
	const UWorld* World = SessionActor->GetWorld();
	const FString Fields = FString::Printf(
		TEXT("Event=fishing_behavior_phase_entered SessionId=%s RodActorId=%s PreviousBehavior=%s Behavior=%s "
			"DurationSeconds=%.4f TargetEffort=%.4f ActualEffort=%.4f BlockedSeconds=%.4f LineLoad=%.4f "
			"PreviousElapsedSeconds=%.4f PreviousDurationSeconds=%.4f ActiveBoutElapsedSeconds=%.4f ActiveBoutDurationSeconds=%.4f "
			"PreviousBoutElapsedSeconds=%.4f PreviousBoutDurationSeconds=%.4f FishStamina=%.4f CatStamina=%.4f "
			"Reason=StateTreeTransition Result=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
		*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(PreviousBehavior), *UEnum::GetValueAsString(Behavior),
		SteeringState.BehaviorDurationSeconds, SteeringState.TargetEffortRatio, SteeringState.CurrentEffortRatio,
		PreviousBlocked, SteeringState.SmoothedLineLoad, PreviousElapsed, PreviousDuration,
		SteeringState.ActiveBoutElapsedSeconds, SteeringState.ActiveBoutDurationSeconds, PreviousBoutElapsed, PreviousBoutDuration,
		State.FishStamina, State.CatStamina,
		bAccepted ? TEXT("Accepted") : TEXT("InvalidBehavior"), *GetNameSafe(World),
		World ? static_cast<int32>(World->GetNetMode()) : -1, static_cast<int32>(SessionActor->GetLocalRole()));
	if (bAccepted) { UE_LOG(LogCatFishing, Log, TEXT("%s"), *Fields); }
	else { UE_LOG(LogCatFishing, Warning, TEXT("%s"), *Fields); }
	return bAccepted;
}

bool UCatFishingFightRunner::TestFishBehaviorConditionFromStateTree(const ECatFishBehaviorCondition Condition) const
{
	return bInitialized && bRunning && !State.bFishExhausted
		&& FCatFishSteeringModel::TestCondition(SteeringConfig, SteeringState, Condition);
}
bool UCatFishingFightRunner::TryResolveGroundedFishPosition(const FVector& DesiredPosition,
	FVector& OutGroundedPosition, FVector& OutSurfaceNormal, AActor*& OutSurfaceActor) const
{
	const FVector QueryPosition = DesiredPosition;
	OutGroundedPosition = FVector::ZeroVector;
	OutSurfaceNormal = FVector::UpVector;
	OutSurfaceActor = nullptr;
	UWorld* World = Session.IsValid() ? Session->GetWorld() : nullptr;
	const UCatFishPickupSettings* ItemSettings = GetDefault<UCatFishPickupSettings>();
	if (!World || !ItemSettings || QueryPosition.ContainsNaN())
	{
		return false;
	}

	TArray<const AActor*> IgnoredActors;
	IgnoredActors.Reserve(5);
	IgnoredActors.Add(Session.Get());
	IgnoredActors.Add(FishActor.Get());
	IgnoredActors.Add(RodActor.Get());
	IgnoredActors.Add(Session->GetSnapshot().HookActor.Get());
	if (OperatorState.Character.IsValid()) IgnoredActors.Add(OperatorState.Character.Get());
	const FCatWorldSurfaceResult Surface = FCatWorldSurfaceResolver::ResolveHighestBlockingSurface(
		World, QueryPosition, ItemSettings->LandingGroundTraceChannel, IgnoredActors);
	if (!Surface.bSucceeded)
	{
		return false;
	}
	// 地表射线也可能首先命中湖面的阻挡 Mesh。用水域自身的无限高度查询取得同一 XY 的水面，
	// 只接受真正高于水面的表面；这不依赖 Actor 名称、Mesh 尺寸或岸地的具体高度。
	const UCatWaterQuerySubsystem* Water = World->GetSubsystem<UCatWaterQuerySubsystem>();
	const FCatWaterImmersionResult WaterRelation = Water
		? Water->QueryImmersionAtWorldPoint(Surface.WorldPosition, WaterRegion)
		: FCatWaterImmersionResult{};
	constexpr double MinimumDryGroundHeightCentimeters = 1.0;
	if (!WaterRelation.bSucceeded
		|| Surface.WorldPosition.Z <= WaterRelation.WaterSurfaceWorldPoint.Z
			+ MinimumDryGroundHeightCentimeters)
	{
		const double WorldSeconds = World->GetTimeSeconds();
		if (WorldSeconds >= NextGroundSurfaceRejectedDiagnosticWorldSeconds)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_ground_surface_rejected SessionId=%s Candidate=%s Surface=%s SurfaceActor=%s "
					"WaterSurface=%s Reason=%s World=%s NetMode=%d Authority=true"),
				Session.IsValid()
					? *Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens)
					: TEXT("None"),
				*QueryPosition.ToCompactString(), *Surface.WorldPosition.ToCompactString(),
				*GetNameSafe(Surface.SurfaceActor.Get()),
				WaterRelation.bSucceeded
					? *WaterRelation.WaterSurfaceWorldPoint.ToCompactString() : TEXT("Unavailable"),
				WaterRelation.bSucceeded ? TEXT("SurfaceNotAboveWater") : TEXT("WaterQueryFailed"),
				*GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
			NextGroundSurfaceRejectedDiagnosticWorldSeconds = WorldSeconds + 1.0;
		}
		return false;
	}
	OutGroundedPosition = Surface.WorldPosition;
	OutSurfaceNormal = Surface.SurfaceNormal;
	OutSurfaceActor = Surface.SurfaceActor.Get();
	return true;
}

FCatFishMotionSolveResult UCatFishingFightRunner::ResolveFishSurfaceFromAuthority(
	FCatFightStepResult& Step, const FCatFightRodConstraintInput& RodConstraint,
	FCatWaterSpatialResult& OutWater, bool& bOutBeachedThisStep,
	FVector& OutGroundNormal, AActor*& OutGroundActor, FCatFishingRodResistanceResult& OutRotationResistance)
{
	FCatFishMotionSolveResult Motion;
	OutRotationResistance = FCatFishingRodResistanceResult{};
	bOutBeachedThisStep = false;
	OutGroundNormal = FVector::UpVector;
	OutGroundActor = nullptr;
	UWorld* World = Session.IsValid() ? Session->GetWorld() : nullptr;
	const UCatWaterQuerySubsystem* Water = World ? World->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Water || !Step.bSucceeded || Step.ProposedFishWorldPosition.ContainsNaN()) return Motion;

	// 水域轮廓提供水面与岸向，不再用抛竿的内缩安全点或初始落点包围盒裁剪拖鱼运动。
	// 先投影水面再查岸向，高岸/下坡不会受抛竿高度容差限制。
	const FCatWaterImmersionResult Immersion = Water->QueryImmersionAtWorldPoint(
		Step.ProposedFishWorldPosition, WaterRegion);
	OutWater = Immersion.bSucceeded
		? Water->QueryShoreRelation(Immersion.WaterSurfaceWorldPoint, WaterRegion) : FCatWaterSpatialResult{};
	if (!OutWater.bSucceeded)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_surface_query_failed SessionId=%s Candidate=%s WaterError=%s World=%s NetMode=%d Authority=true"),
			*Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *Step.ProposedFishWorldPosition.ToCompactString(),
			*UEnum::GetValueAsString(Immersion.bSucceeded ? OutWater.Error : Immersion.Error),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
		return Motion;
	}

	FCatFishBeachingIntentInput Intent;
	Intent.CurrentFishWorldPosition = State.FishWorldPosition;
	Intent.CandidateFishWorldPosition = Step.ProposedFishWorldPosition;
	Intent.WaterwardDirection = OutWater.WaterwardDirection;
	Intent.CarrierActualWorldDisplacement = RodConstraint.CarrierVelocityCentimetersPerSecond * Config.FixedStepSeconds;
	Intent.NonCarrierRodTipWorldDisplacement = (RodConstraint.RodTipVelocityCentimetersPerSecond
		- RodConstraint.CarrierVelocityCentimetersPerSecond) * Config.FixedStepSeconds;
	Intent.ActualReelDistanceCentimeters = Step.ActualReelDistanceCentimeters;
	Intent.ReelConstraintDistanceCentimeters = State.CatAction == ECatFightCatAction::Pull
		&& Step.OperatorCatStrength > UE_DOUBLE_SMALL_NUMBER
		? FMath::Max(0.0, Step.FishConstraintCorrectionCentimeters - Step.Trace.FishPositionCorrectionCentimeters) : 0.0;
	Intent.bLineTaut = Step.bLineTaut;
	const bool bCatHaulingFish = FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Intent);
	// 力竭鱼没有自主游动，所有候选位移都来自同一根鱼线，不再为它加活鱼的防甩杆力竭门槛。
	const bool bSurfaceTow = (State.bFishExhausted || bCatHaulingFish)
		&& Step.Outcome != ECatFightStepOutcome::RodBroken
		&& Step.Outcome != ECatFightStepOutcome::Escaped;
	const bool bWasBeached = bFishBeached;
	bool bShoreContactThisStep = false;
	FVector GroundedPosition = FVector::ZeroVector;
	const bool bGroundResolved = (bWasBeached || bSurfaceTow)
		&& TryResolveGroundedFishPosition(Step.ProposedFishWorldPosition, GroundedPosition,
			OutGroundNormal, OutGroundActor);
	bFishBeached = bGroundResolved;
	if (bGroundResolved)
	{
		Motion.bSucceeded = true;
		Motion.FishWorldPosition = GroundedPosition;
		bOutBeachedThisStep = !bWasBeached;
	}
	else if (bSurfaceTow || bWasBeached)
	{
		// 活鱼与鱼干共用连续水面过渡；必须到真实干地才交接，不把烘焙边界当成挡墙。
		Motion.bSucceeded = true;
		Motion.FishWorldPosition = Step.ProposedFishWorldPosition;
		Motion.FishWorldPosition.Z = OutWater.WaterSurfaceWorldPoint.Z;
		const double WorldSeconds = World->GetTimeSeconds();
		if (bWasBeached || (OutWater.Containment != ECatWaterContainment::Inside
			&& WorldSeconds >= NextSurfaceTowDiagnosticWorldSeconds))
		{
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_surface_tow SessionId=%s Lifecycle=%s Candidate=%s ResolvedFish=%s "
					"PreviousSurface=%s Result=ContinueSurfaceTow Reason=DryGroundMissing "
					"ReelDistance=%.3f ReelConstraint=%.3f World=%s NetMode=%d Authority=true"),
				*Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), State.bFishExhausted ? TEXT("Exhausted") : TEXT("Active"),
				*Step.ProposedFishWorldPosition.ToCompactString(), *Motion.FishWorldPosition.ToCompactString(),
				bWasBeached ? TEXT("Ground") : TEXT("Water"), Intent.ActualReelDistanceCentimeters,
				Intent.ReelConstraintDistanceCentimeters, *GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
			NextSurfaceTowDiagnosticWorldSeconds = WorldSeconds + 1.0;
		}
	}
	else
	{
		// 自游/纯甩杆只阻止继续向陆地，实际入水进度与沿岸滑动保留；真实拖拽不经过此分支。
		FCatFishShoreContactInput ShoreInput;
		ShoreInput.CurrentFishWorldPosition = State.FishWorldPosition;
		ShoreInput.CandidateFishWorldPosition = Step.ProposedFishWorldPosition;
		ShoreInput.ResolvedWaterWorldPosition = OutWater.Containment == ECatWaterContainment::Inside
			? OutWater.WaterSurfaceWorldPoint : OutWater.NearestShoreWorldPoint;
		ShoreInput.WaterwardDirection = OutWater.WaterwardDirection;
		ShoreInput.RodTipWorldPosition = RodConstraint.RodTipWorldPosition;
		ShoreInput.PreviousLineLengthCentimeters = State.LineLengthCentimeters;
		ShoreInput.ProposedLineLengthCentimeters = Step.LineLengthCentimeters;
		ShoreInput.MaximumConstraintDistanceCentimeters = FMath::Max3(Step.LineLengthCentimeters,
			FVector::Distance(RodConstraint.RodTipWorldPosition, Step.ProposedFishWorldPosition),
			FVector::Distance(RodConstraint.RodTipWorldPosition, State.FishWorldPosition));
		ShoreInput.bReeling = State.CatAction == ECatFightCatAction::Pull;
		// 开步尚有容量时允许按最终岸线落点校正本步出线；候选放满不等于实际已放满。
		ShoreInput.bSlacking = State.CatAction == ECatFightCatAction::Slack
			&& (!State.bOperatorPresent || !FCatFishingFightSimulator::IsLineAtMaximum(Config, State.LineLengthCentimeters));
		const FCatFishShoreContactResult ShoreContact = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(ShoreInput);
		if (!ShoreContact.bSucceeded) return Motion;
		Motion.bSucceeded = true;
		Motion.FishWorldPosition = ShoreContact.FishWorldPosition;
		Step.LineLengthCentimeters = FMath::Min(Config.MaximumLineLengthCentimeters, ShoreContact.LineLengthCentimeters);
		bShoreContactThisStep = ShoreContact.bShoreContact;
		if (bShoreContactThisStep && !FCatFishSteeringModel::RedirectFromWaterBoundary(
			SteeringConfig, OutWater.WaterwardDirection, SteeringRandom, SteeringState))
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_shore_recovery_rejected SessionId=%s FishActor=%s Waterward=%s "
					"Result=InvalidSteering World=%s NetMode=%d Authority=true LocalRole=%d"),
				*Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(FishActor.Get()), *OutWater.WaterwardDirection.ToCompactString(),
				*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Session->GetLocalRole()));
			return FCatFishMotionSolveResult{};
		}
	}

	// 状态边沿必落盘，持续岸线接触最多每秒一次；记录实际进度以区分卡岸和鱼线牵制。
	const double WorldSeconds = World->GetTimeSeconds();
	if (bShoreContactThisStep != bLastShoreContactDiagnosticActive
		|| (bShoreContactThisStep && WorldSeconds >= NextShoreContactDiagnosticWorldSeconds))
	{
		const FCatFightOperatorRuntime* Primary = GetPrimaryOperator();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_shore_recovery SessionId=%s FishActor=%s RodActor=%s PlayerState=%s RegionId=%s "
				"Result=%s Containment=%s SignedShoreDistanceCm=%.3f Fish=%s Candidate=%s ResolvedFish=%s "
				"Waterward=%s CandidateWaterwardCm=%.3f ResolvedWaterwardCm=%.3f CatAction=%s LineLengthCm=%.3f "
				"World=%s NetMode=%d Authority=true LocalRole=%d"),
			*Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(FishActor.Get()), *GetNameSafe(RodActor.Get()),
			*GetNameSafe(Primary ? Primary->PlayerState.Get() : nullptr), *WaterRegion.RegionId.ToString(),
			bShoreContactThisStep ? TEXT("ContactResolved") : TEXT("ContactEnded"),
			*UEnum::GetValueAsString(OutWater.Containment), OutWater.SignedDistanceToShoreCm,
			*State.FishWorldPosition.ToCompactString(), *Step.ProposedFishWorldPosition.ToCompactString(),
			*Motion.FishWorldPosition.ToCompactString(), *OutWater.WaterwardDirection.ToCompactString(),
			FVector::DotProduct(Step.ProposedFishWorldPosition - State.FishWorldPosition, OutWater.WaterwardDirection),
			FVector::DotProduct(Motion.FishWorldPosition - State.FishWorldPosition, OutWater.WaterwardDirection),
			State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
				: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None"), Step.LineLengthCentimeters,
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Session->GetLocalRole()));
		NextShoreContactDiagnosticWorldSeconds = WorldSeconds + 1.0;
	}
	bLastShoreContactDiagnosticActive = bShoreContactThisStep;

	// 地面落点只结算一次，不能随后被水面候选覆盖。坡面改变后的真实距离供复制和下一步共同使用。
	Step.StraightLineDistanceCentimeters = FVector::Distance(RodConstraint.RodTipWorldPosition, Motion.FishWorldPosition);
	Step.SlackLineLengthCentimeters = FMath::Max(0.0, Step.LineLengthCentimeters - Step.StraightLineDistanceCentimeters);
	// 求解输出包含双方同一时间步末的负载；只有地形确实改变候选落点，才按新几何撤销负载。
	const bool bSurfaceChangedCandidate = !Motion.FishWorldPosition.Equals(Step.ProposedFishWorldPosition, 0.001);
	Step.bLineTaut = (!bSurfaceChangedCandidate && Step.LineTensionNewtons > UE_DOUBLE_SMALL_NUMBER)
		|| Step.SlackLineLengthCentimeters <= UE_DOUBLE_KINDA_SMALL_NUMBER;
	if (!Step.bLineTaut)
	{
		Step.TensionCentimeters = 0.0;
		Step.LineTensionNewtons = 0.0;
		Step.RodLineForceNewtons = FVector::ZeroVector;
		Step.NormalizedTension = 0.0;
		Step.ConstraintErrorCentimeters = 0.0;
		Step.RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	}
	if (bOutBeachedThisStep)
	{
		Step.bFishBeached = true;
		if (!State.bFishExhausted && Step.Outcome != ECatFightStepOutcome::RodBroken
			&& Step.Outcome != ECatFightStepOutcome::Escaped)
		{
			Step.FishStaminaDrain = State.FishStamina;
			Step.Outcome = ECatFightStepOutcome::FishExhausted;
		}
	}
	Step.ResolvedFishVelocityCentimetersPerSecond += (Motion.FishWorldPosition - Step.ProposedFishWorldPosition) / Config.FixedStepSeconds;
	Step.ResolvedFishVelocityCentimetersPerSecond.Z = 0.0;
	Step.ProposedFishWorldPosition = Motion.FishWorldPosition;
	// 费用以最终线长/地形落点重新计算。仍只有 HandleFixedStep 随后向 ASC 和装备各写一次。
	if (!FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, RodConstraint, Step))
	{
		UE_LOG(LogCatFishing, Error, TEXT("Event=fishing_final_work_rejected SessionId=%s World=%s NetMode=%d Authority=true Result=InvalidResolvedWork"),
			*Session->GetSnapshot().FishingSessionId.ToString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
		return FCatFishMotionSolveResult{};
	}
	// 地形可改变距离、解除张力或直接触发上岸力竭。转杆必须消费最终事实，
	// 不得把地形解析前缓存的负载与解析后的松线/落点一起发布。
	FCatFishingRodResistanceInput RotationInput;
	RotationInput.CatStrength = FMath::Max(0.0, Config.PrimaryOperatorCatStrength * OperatorSupportAlignment);
	RotationInput.LineTensionNewtons = State.bFishExhausted || Step.Outcome != ECatFightStepOutcome::None
		? 0.0 : Step.LineTensionNewtons;
	RotationInput.ForcePerStrengthNewtons = Config.ForcePerStrengthNewtons;
	RotationInput.RodPhysicsLengthCentimeters = Config.RodPhysicsLengthCentimeters;
	RotationInput.RodLineAlignment = FMath::Clamp(FVector::DotProduct(
		RodConstraint.RodForwardWorld.GetSafeNormal(),
		(Motion.FishWorldPosition - RodConstraint.RodTipWorldPosition).GetSafeNormal()), -1.0, 1.0);
	OutRotationResistance = FCatFishingRodResistanceModel::Evaluate(RotationInput);
	if (!OutRotationResistance.bSucceeded)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_rod_resolved_load_rejected SessionId=%s Fish=%s Tension=%.3f LineLoad=%.3f "
				"World=%s NetMode=%d Authority=true LocalRole=%d Result=InvalidResolvedLoad"),
			*Session->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Motion.FishWorldPosition.ToCompactString(), Step.NormalizedTension, Step.NormalizedLineLoad,
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Session->GetLocalRole()));
		return FCatFishMotionSolveResult{};
	}
	return Motion;
}

void UCatFishingFightRunner::HandleFixedStep()
{
	// [FishLogic 总调度]
	// 每 0.05 秒按 1节奏 → 2方向 → 3受力公式 → 4服务器落位/复制 的顺序推进一次。
	// 逐个解析本步需要用到的弱引用，任何一个失效都说明搏斗依赖已经被销毁/离线，走失败收尾。
	ACatFishingSession* SessionActor = Session.Get();
	ACatFishEncounterActor* Encounter = FishActor.Get();
	ACatFishingRodActor* Rod = RodActor.Get();
	if (Rod && Rod->IsUsingPhysicalRod()) Rod->RefreshPrimaryControlFromAuthority();
	UCatAbilitySystemComponent* ASC = AbilitySystem.Get();
	UWorld* World = SessionActor ? SessionActor->GetWorld() : nullptr;
	UCatWaterQuerySubsystem* Water = World
		? World->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!bRunning || !SessionActor || !SessionActor->HasAuthority() || !Encounter || !Rod
		|| (State.bOperatorPresent && !ASC)
		|| !World || !Water)
	{
		Stop();
		if (SessionActor) SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("DependencyResolution"));
		return;
	}
	SessionActor->BeginFixedStepMutationBoundary();
	ON_SCOPE_EXIT { if (IsValid(SessionActor)) SessionActor->EndFixedStepMutationBoundary(); };
	if (!RefreshPrimaryOperatorFromRod()
		|| !UpdateOperatorIntentAndProperties())
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("PrimaryOperatorRefresh"));
		return;
	}
	ASC = AbilitySystem.Get();
	if (State.bOperatorPresent)
	{
		if (!ASC)
		{
			Stop();
			SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("PrimaryAbilityResolution"));
			return;
		}
		State.CatStamina = FMath::Clamp(ASC->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0, Config.CatStaminaMaximum);
	}
	bool bWaterDepartureRequested = false;
	if (State.bOperatorPresent)
	{
		const FCatFightOperatorRuntime* Member = GetPrimaryOperator();
		ACatCharacter* Character = Member ? Member->Character.Get() : nullptr;
		UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
		double ImmersionDepth = 0.0;
		const ECatWaterExposureUpdate Exposure = Condition
			? Condition->UpdateWaterExposureFromAuthority(WaterRegion, Config.FixedStepSeconds, ImmersionDepth)
			: ECatWaterExposureUpdate::Unavailable;
		if (Exposure == ECatWaterExposureUpdate::Unavailable)
		{
			Stop();
			SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("WaterExposureQuery"));
			return;
		}
		if (Exposure == ECatWaterExposureUpdate::DangerousEntered
			|| Condition->GetSnapshot().WaterExposure == ECatWaterExposureState::Dangerous)
		{
			SessionActor->HandleCatEnteredDangerousWaterFromAuthority(ImmersionDepth, Character);
			bWaterDepartureRequested = true;
		}
	}
	// Flush all departing members before the next force snapshot; no old-member bill is applied on this transition.
	if (bWaterDepartureRequested) return;

	// 行为时钟、反馈和树转移共用本固定步；强制拖水期间暂停普通行为的计时和选择。
	// 每步开始都以 Encounter Actor 的实际 Transform 为准同步鱼的位置，避免和上一步的建议位置产生累积误差。
	State.FishWorldPosition = Encounter->GetActorLocation();
	const FVector RodTip = Rod->GetRodTipWorldTransform().GetLocation();
	const bool bRodHeld = Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held;
	const bool bExhaustedCatEscape = UpdateFishBehaviorForCurrentOperator(bRodHeld);
	const APawn* EscapeHolder = Rod->GetHolderPawnFromAuthority();
	// 外冲以猫身体为远离基点，避免力竭时被鱼拉转的竿尖把方向绕回岸边。
	FVector Outward = State.FishWorldPosition - (bExhaustedCatEscape && EscapeHolder ? EscapeHolder->GetActorLocation() : RodTip);
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector;
	FVector DesiredFishDirection = FVector::ZeroVector;
	// 服务器按性格和固定随机种子生成连续游向；客户端不参与随机，只接收最终权威 Transform/表现状态。
	const double FishStaminaRatio = FMath::Clamp(State.FishStamina / InitialFishStamina, 0.0, 1.0);
	if (!State.bFishExhausted && !bExhaustedCatEscape)
	{
		FCatFishBehaviorFeedback Feedback;
		Feedback.NormalizedLineLoad = FMath::Clamp(PreviousFishLineTensionNewtons
			/ (Config.FishStrength * Config.ForcePerStrengthNewtons), 0.0, 1.0);
		Feedback.ActualFishVelocityCentimetersPerSecond = State.FishVelocityCentimetersPerSecond;
		Feedback.ActiveSwimDirection = PreviousFishEffortDirection;
		Feedback.ExpectedFreeSpeedCentimetersPerSecond = PreviousFishExpectedSwimSpeedCentimetersPerSecond;
		Feedback.FishStaminaRatio = FishStaminaRatio;
		Feedback.bLineTaut = PreviousFishLineTensionNewtons > UE_DOUBLE_SMALL_NUMBER;
		if (!FCatFishSteeringModel::AdvanceFeedback(SteeringConfig, Feedback, Config.FixedStepSeconds, SteeringState)
			|| !Encounter->TickFishBehaviorFromAuthority(static_cast<float>(Config.FixedStepSeconds)))
		{
			Stop();
			SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("FishBehaviorFixedStep"));
			return;
		}
	}
	if (!State.bFishExhausted && !FCatFishSteeringModel::Step(SteeringConfig, Outward,
		Config.FixedStepSeconds, SteeringRandom, SteeringState, DesiredFishDirection, bExhaustedCatEscape))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("FishSteering"));
		return;
	}
	State.FishEffortRatio = State.bFishExhausted ? 0.0 : bExhaustedCatEscape ? 1.0 : SteeringState.CurrentEffortRatio;
	// 正式动画仍消费旧三角色枚举，采用实际出力的滞回分类；物理与费用不读取此分类。
	if (!State.bFishExhausted && !bExhaustedCatEscape)
	{
		const bool bWasStrong = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward;
		State.MotionIntent = State.FishEffortRatio >= (bWasStrong ? 0.4 : 0.55)
			? ECatFishMotionIntent::StrugglingOutward : ECatFishMotionIntent::CalmOrInward;
	}
	// 从同一根权威 Rod Actor 读取规范竿尖/竿向/速度；客户端动画 Socket 与客户端自报受力均不参与裁决。
	FCatFightRodConstraintInput RodConstraint;
	RodConstraint.RodTipWorldPosition = RodTip;
	RodConstraint.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
	RodConstraint.RodTipVelocityCentimetersPerSecond = Rod->GetAuthoritativeRodTipVelocity();
	RodConstraint.CarrierVelocityCentimetersPerSecond = Rod->GetAuthoritativeHolderVelocity();
	RodConstraint.bPhysicalRodEndpoint = Rod->IsUsingPhysicalRod();
	if (RodConstraint.bPhysicalRodEndpoint) Rod->GetPhysicalRodComponent()->PopulateEndpointResponse(RodConstraint);
	RodConstraint.CatSupportAlignment = OperatorSupportAlignment;
	RodConstraint.bRodHeld = bRodHeld;
	const FCatFishingRodRotationEffortSnapshot RotationEffort = RotationEffortSampler.Consume(
		Rod->GetAuthoritativeRotationEffortSnapshot(), Config.FixedStepSeconds);
	RodConstraint.CatRodExertionSquaredSeconds = RotationEffort.ExertionSquaredSeconds;
	RodConstraint.CatRodPositiveWorkRadians = RotationEffort.PositiveWorkRadians;
	// 纯模拟器依据鱼游向、实际竿端响应和主控操作，计算唯一线力、鱼位置与本步费用。
	FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, RodConstraint, DesiredFishDirection);
	if (!Step.bSucceeded)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_fight_step_rejected SessionId=%s Stage=%s RejectReason=%s RejectReasonCode=%d InputAccepted=%s FinalizeAccepted=%s "
				"FishExhausted=%s Fish=%s RodTip=%s DesiredFishDirection=%s LineLength=%.3f NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			TEXT("FightSimulation"),
			SimulationRejectReasonName(Step.RejectReason),
			static_cast<int32>(Step.RejectReason),
			Step.Trace.bInputAccepted ? TEXT("true") : TEXT("false"),
			Step.Trace.bFinalizeInputAccepted ? TEXT("true") : TEXT("false"),
			State.bFishExhausted ? TEXT("true") : TEXT("false"),
			*State.FishWorldPosition.ToCompactString(), *RodTip.ToCompactString(),
			*DesiredFishDirection.ToCompactString(), State.LineLengthCentimeters,
			static_cast<int32>(World->GetNetMode()));
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("FightSimulation"));
		return;
	}
	FCatWaterSpatialResult Exact;
	bool bBeachedThisStep = false;
	FVector GroundSurfaceNormal = FVector::UpVector;
	AActor* GroundSurfaceActor = nullptr;
	FCatFishingRodResistanceResult RotationResistance;
	const FCatFishMotionSolveResult Motion = ResolveFishSurfaceFromAuthority(Step, RodConstraint,
		Exact, bBeachedThisStep, GroundSurfaceNormal, GroundSurfaceActor, RotationResistance);
	if (!Motion.bSucceeded)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_surface_resolve_rejected SessionId=%s Fish=%s Candidate=%s RodTip=%s "
				"LineLength=%.3f Exhausted=%s CatAction=%s WaterError=%s World=%s NetMode=%d Authority=true Result=SessionInvalidated"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*State.FishWorldPosition.ToCompactString(), *Step.ProposedFishWorldPosition.ToCompactString(),
			*RodTip.ToCompactString(), Step.LineLengthCentimeters, State.bFishExhausted ? TEXT("true") : TEXT("false"),
			State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull") : State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None"),
			*UEnum::GetValueAsString(Exact.Error), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("FishSurfaceResolve"));
		return;
	}
	if (bBeachedThisStep)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_fish_beached SessionId=%s PreviousLifecycle=%s CatAction=%s "
				"FishStaminaBefore=%.3f Fish=%s GroundNormal=%s GroundActor=%s RodTip=%s "
				"LineLength=%.3f ConstraintError=%.3f Result=GroundedAutoHauling World=%s NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			State.bFishExhausted ? TEXT("ExhaustedReel") : TEXT("ActiveFight"),
			State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
				: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None"),
			State.FishStamina, *Motion.FishWorldPosition.ToCompactString(),
			*GroundSurfaceNormal.ToCompactString(), *GetNameSafe(GroundSurfaceActor),
			*RodTip.ToCompactString(), Step.LineLengthCentimeters, Step.ConstraintErrorCentimeters,
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()));
	}

	// 未被猫端牵引越岸时，靠近岸线仍只是空间事实，鱼会沿岸反射；一旦越岸则复用力竭叶子并切到地面吸附。

	// 鱼线载荷只提交一次；持竿交给主控 CMC，架竿由固定支撑承受。
	if (Rod->IsUsingPhysicalRod())
	{
		const FVector LineDirection = (Motion.FishWorldPosition - RodTip).GetSafeNormal();
		// Publish tension exactly once. No already-netted carrier acceleration or second fish torque reaches physics.
		Rod->GetPhysicalRodComponent()->SetLineLoad(SessionActor->GetSnapshot().FishingSessionId,
			DiagnosticFixedStepSequence, Step.RodLineForceNewtons, Config.FixedStepSeconds, FMath::Max(0.15, Config.FixedStepSeconds * 3.0));
		Rod->SetFightConstraintObservationFromAuthority(LineDirection, Step.NormalizedTension, Step.ConstraintErrorCentimeters, RodConstraint.bRodHeld, RotationResistance.MaximumFishTorqueStrengthMeters, RotationResistance.CatTorqueCapacityStrengthMeters, LineDirection);
	}
	const bool bConstraintActive = RodConstraint.bRodHeld
		&& Step.NormalizedTension > UE_DOUBLE_SMALL_NUMBER;
	const double WorldSeconds = World->GetTimeSeconds();
	const TCHAR* CatActionName = State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
		: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None");
	const TCHAR* SimulationOutcomeName = Step.Outcome == ECatFightStepOutcome::FishExhausted ? TEXT("FishExhausted")
		: Step.Outcome == ECatFightStepOutcome::RodBroken ? TEXT("RodBroken")
			: Step.Outcome == ECatFightStepOutcome::Escaped ? TEXT("Escaped") : TEXT("None");
	const double FishUncappedStaminaDrain = Step.FishUncappedStaminaDrain;
	const double FishStaminaAfterStep = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	const FVector ResolvedFishDelta = Motion.FishWorldPosition - State.FishWorldPosition;
	const bool bFishStaminaTerminalStep = !State.bFishExhausted
		&& Step.Outcome == ECatFightStepOutcome::FishExhausted;
	const bool bFishStaminaSpike = !State.bFishExhausted && !bFishStaminaTerminalStep
		&& Step.FishStaminaDrain >= FMath::Max(5.0, InitialFishStamina * 0.1);
	const bool bLogSimulationTrace = WorldSeconds >= NextPowerDiagnosticWorldSeconds
		|| Step.Outcome != ECatFightStepOutcome::None;
	if (bLogSimulationTrace)
	{
		const FCatFightSimulationTrace& Trace = Step.Trace;
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_simulation_trace SessionId=%s RodActorId=%s FixedStepSeconds=%.5f "
				"DistanceBeforeCm=%.3f HorizontalDistanceCm=%.3f VerticalDistanceCm=%.3f "
				"FishAlignment=%.5f LineLoad=%.5f RodLineAlignment=%.5f RodLeverage=%.5f "
				"OperatorCatStrength=%.3f EffectiveCatStrength=%.3f ActiveFishStrength=%.3f "
				"CatForceN=%.3f FishThrustN=%.3f OperatorBodyMassKg=%.3f "
				"CatDriveAccelerationCmPerSec2=%.3f FishDriveAccelerationCmPerSec2=%.3f "
				"FishFullEffortSpeedCmPerSec=%.3f SwimSpeedCmPerSec=%.3f MobilityCmPerNewton=%.6f "
				"ExistingPositionErrorCm=%.3f RequiredTensionAtCurrentN=%.3f RequiredTensionAtPaidOutN=%.3f "
				"ReelForceLimitN=%.3f FishCorrectionCm=%.3f LineTensionN=%.3f "
				"FishLineForceN=%.3f CatLineForceN=%.3f HorizontalLineFactor=%.5f "
				"FishStaminaDrainBeforeClamp=%.5f FishStaminaDrain=%.5f FishStaminaAfter=%.5f "
				"RodActionEstimatedStaminaDrain=%.5f PrimaryEstimatedStaminaAfter=%.5f WearLoad=%.5f RodWearDelta=%.5f "
				"CatReelWorkUnits=%.5f CatRodWorkUnits=%.5f "
				"CatHoldLoad=%.5f CatRodLoad=%.5f CatRodSupportBeforeHoldDeduction=%.5f "
				"FishEffortRatio=%.5f FishIntendedDistanceCm=%.5f FishActualIntentProgressCm=%.5f FishUnfulfilledDistanceCm=%.5f FishStaminaPerUnfulfilledMeter=%.5f FishDragKgPerSec=%.5f "
				"FreeSpool=%s LineRestraining=%s Reeling=%s Struggling=%s Outcome=%s "
				"InputAccepted=%s FinalizeAccepted=%s NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			Trace.FixedStepSeconds,
			Trace.DistanceBeforeCentimeters, Trace.HorizontalDistanceCentimeters, Trace.VerticalDistanceCentimeters,
			Trace.FishAlignment, Trace.NormalizedLineLoad, Trace.RodLineAlignment, Trace.RodLeverageMultiplier,
			Trace.OperatorCatStrength, Trace.EffectiveCatStrength, Trace.ActiveFishStrength,
			Trace.CatForceNewtons, Trace.FishThrustNewtons, Trace.OperatorBodyMassKilograms,
			Trace.CatDriveAccelerationCentimetersPerSecondSquared,
			Trace.FishDriveAccelerationCentimetersPerSecondSquared,
			Trace.FishFullEffortSpeedCentimetersPerSecond, Trace.SwimSpeedCentimetersPerSecond,
			Trace.MobilityCentimetersPerNewton,
			Trace.ExistingPositionErrorCentimeters, Trace.RequiredTensionAtCurrentLengthNewtons,
			Trace.RequiredTensionAtPaidOutLengthNewtons, Trace.ReelForceLimitNewtons,
			Trace.FishCorrectionCentimeters, Trace.LineTensionNewtons,
			Trace.FishLineForceNewtons, Trace.CatLineForceNewtons, Trace.HorizontalLineFactor,
			Trace.FishStaminaDrainBeforeClamp, Step.FishStaminaDrain, Trace.FishStaminaAfterStep,
			Step.CatStaminaDrain, Trace.CatStaminaAfterStep, Trace.WearLoad, Trace.RodWearDelta,
			Trace.CatReelPositiveWorkUnits, Trace.CatRodPositiveWorkUnits,
			Trace.CatHoldNormalizedLoad, Trace.CatRodNormalizedLoad,
			Trace.CatRodSupportBeforeHoldDeduction,
			Trace.FishEffortRatio, Trace.FishIntendedDistanceCentimeters, Trace.FishActualIntentProgressCentimeters,
			Trace.FishUnfulfilledDistanceCentimeters, Trace.FishStaminaPerUnfulfilledMeter, Trace.FishLinearDragKilogramsPerSecond,
			Trace.bFreeSpool ? TEXT("true") : TEXT("false"),
			Trace.bLineRestraining ? TEXT("true") : TEXT("false"),
			Trace.bReeling ? TEXT("true") : TEXT("false"), Trace.bStruggling ? TEXT("true") : TEXT("false"),
			SimulationOutcomeName, Trace.bInputAccepted ? TEXT("true") : TEXT("false"),
			Trace.bFinalizeInputAccepted ? TEXT("true") : TEXT("false"), static_cast<int32>(World->GetNetMode()));
	}
	const auto LogFishStaminaBreakdown = [&](const TCHAR* EventName, const TCHAR* Trigger)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=%s SessionId=%s RodActorId=%s Trigger=%s CatAction=%s Behavior=%s "
				"FishDrainMode=UnfulfilledIntentDistance FishStaminaBefore=%.4f FishStaminaDrain=%.4f FishStaminaAfter=%.4f "
				"DrainPerSecond=%.4f UncappedDrain=%.4f TargetEffort=%.4f ActualEffort=%.4f IntendedDistanceCm=%.4f ActualIntentProgressCm=%.4f UnfulfilledDistanceCm=%.4f StaminaPerUnfulfilledMeter=%.4f "
				"FullEffortThrustN=%.4f ActualThrustN=%.4f DragKgPerSec=%.4f FixedStepSeconds=%.4f "
				"IntendedSwimSpeedCmPerSec=%.4f FishBefore=%s ResolvedFish=%s ResolvedDelta=%s ResolvedDeltaZCm=%.4f "
				"DesiredFishDirection=%s RodTip=%s LineLengthBefore=%.4f LineLengthAfter=%.4f "
				"LineTensionN=%.4f Alignment=%.4f Beached=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
			EventName, *SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), Trigger, CatActionName,
			*UEnum::GetValueAsString(SteeringState.Behavior), State.FishStamina, Step.FishStaminaDrain, FishStaminaAfterStep,
			Step.FishStaminaDrain / Config.FixedStepSeconds, FishUncappedStaminaDrain,
			SteeringState.TargetEffortRatio, Step.Trace.FishEffortRatio, Step.FishIntendedDistanceCentimeters,
			Step.FishActualIntentProgressCentimeters, Step.FishUnfulfilledDistanceCentimeters,
			Config.FishStaminaPerUnfulfilledMeter, Step.Trace.FishFullEffortThrustNewtons, Step.Trace.FishThrustNewtons,
			Step.Trace.FishLinearDragKilogramsPerSecond, Config.FixedStepSeconds, Step.IntendedSwimSpeedCentimetersPerSecond,
			*State.FishWorldPosition.ToCompactString(), *Motion.FishWorldPosition.ToCompactString(),
			*ResolvedFishDelta.ToCompactString(), ResolvedFishDelta.Z, *DesiredFishDirection.ToCompactString(),
			*RodTip.ToCompactString(), State.LineLengthCentimeters, Step.LineLengthCentimeters,
			Step.LineTensionNewtons, Step.FishLineAlignment, bBeachedThisStep ? TEXT("true") : TEXT("false"),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(SessionActor->GetLocalRole()));
	};
	const bool bLogWorkSample = WorldSeconds >= NextPowerDiagnosticWorldSeconds;
	if (bLogWorkSample)
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_coupled_work_sample SessionId=%s RodActorId=%s "
				"PrimaryStrength=%.3f CombinedStrength=%.3f CatSystemMassKg=%.3f FishMassKg=%.3f MassMode=IndependentCatBodyMass StrengthMode=ConstantWhileStaminaPositive StrengthPerKg=%.3f "
				"CatAcceleration=%.3f FishAcceleration=%.3f "
				"PrimaryStamina=%.3f RodActionEstimatedStaminaDrain=%.3f FishStamina=%.3f FishStaminaDrain=%.3f SlackRecovery=%s CatRecovery=%.4f "
				"MotionIntent=%s CatIntentCm=%.3f CatActualCm=%.3f FishLineIntentCm=%.3f FishLineActualCm=%.3f "
				"FishWorldStep2DCm=%.3f FishWorldStep3DCm=%.3f FishWorldDeltaZCm=%.3f "
				"ReelRequestedCm=%.3f ReelActualCm=%.3f ReelMode=%s CatAction=%s AbsoluteRodWear=%.3f RodWearDelta=%.3f "
				"ReelDrain=%.4f RodDrain=%.4f HoldDrain=%.4f RodWorkDrain=%.4f RodSupportExtraDrain=%.4f CatModel=ActualWorkAndTimedSupport "
				"RodExertionSquaredSeconds=%.3f RodPositiveWorkRadians=%.3f HoldIntentCm=%.3f "
				"CatEffortLoad=%.3f RodEffortLoad=%.3f FishUnfulfilledDistanceCm=%.3f MovementIntentSource=IndividualPhysicalBody "
				"FishExhausted=%s World=%s PlayerState=%s NetMode=%d Authority=true LocalRole=%d"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			Config.PrimaryOperatorCatStrength,
			Step.OperatorCatStrength,
			Config.PrimaryOperatorMassKilograms,
			Config.FishMassKilograms,
			Config.StrengthPerKilogram,
			Step.CatDriveAccelerationCentimetersPerSecondSquared,
			Step.FishDriveAccelerationCentimetersPerSecondSquared,
			State.CatStamina,
			Step.CatStaminaDrain,
			State.FishStamina,
			Step.FishStaminaDrain,
			Step.bSlackRecoveryActive ? TEXT("true") : TEXT("false"),
			FMath::Max(0.0, -Step.CatStaminaDrain),
			*UEnum::GetValueAsString(State.MotionIntent),
			Step.CatIntendedLineDistanceCentimeters,
			Step.CatActualLineDistanceCentimeters,
			Step.FishIntendedLineDistanceCentimeters,
			Step.FishActualLineDistanceCentimeters,
			ResolvedFishDelta.Size2D(),
			ResolvedFishDelta.Size(),
			ResolvedFishDelta.Z,
			Step.RequestedReelDistanceCentimeters,
			Step.ActualReelDistanceCentimeters,
			State.bFishExhausted ? TEXT("ExhaustedAssistedForce") : TEXT("ForceLimitedReel"),
			State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
				: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None"),
			Step.AbsoluteRodWear,
			Step.RodWearDelta,
			Step.CatReelStaminaDrain, Step.CatRodStaminaDrain, Step.CatHoldStaminaDrain,
			Step.CatRodWorkStaminaDrain, Step.CatRodSupportStaminaDrain,
			Step.CatRodExertionSquaredSeconds, Step.CatRodPositiveWorkRadians, Step.CatHoldIntentCentimeters,
			Step.CatNormalizedEffortLoad, Step.CatRodNormalizedEffortLoad, Step.FishUnfulfilledDistanceCentimeters,
			State.bFishExhausted ? TEXT("true") : TEXT("false"),
			*GetNameSafe(World), *GetNameSafe(GetPrimaryOperator() ? GetPrimaryOperator()->PlayerState.Get() : nullptr),
			static_cast<int32>(World->GetNetMode()), static_cast<int32>(SessionActor->GetLocalRole()));
		LogFishStaminaBreakdown(TEXT("fishing_fish_stamina_sample"), TEXT("Periodic"));
		NextPowerDiagnosticWorldSeconds = WorldSeconds + 1.0;
	}
	if (bFishStaminaTerminalStep)
	{
		LogFishStaminaBreakdown(TEXT("fishing_fish_stamina_terminal_step"),
			bBeachedThisStep ? TEXT("ShoreLanding") : TEXT("StaminaDepleted"));
	}
	else if (bFishStaminaSpike)
	{
		LogFishStaminaBreakdown(TEXT("fishing_fish_stamina_spike"), TEXT("SingleStepThreshold"));
	}
	const double FixedStepWorldGap = LastFixedStepDiagnosticWorldSeconds >= 0.0
		? WorldSeconds - LastFixedStepDiagnosticWorldSeconds : 0.0;
	LastFixedStepDiagnosticWorldSeconds = WorldSeconds;
	++DiagnosticFixedStepSequence;
	if (CatFishingMotionDiagnostics::IsDetailedEnabled() || bConstraintActive != bLastConstraintDiagnosticActive
		|| (bConstraintActive && WorldSeconds >= NextConstraintDiagnosticWorldSeconds))
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_constraint_sample SessionId=%s RodActorId=%s Active=%s Action=%s "
				"Model=CommonLineForce Geometry=WaterPlaneSphereIntersection RodTorqueSource=ResolvedSurface "
				"ConstraintError=%.2f RelativeLineSpeed=%.2f Tension=%.3f FishCorrection=%.2f "
				"RodLeverage=%.3f "
				"RodPhysicsLengthCm=%.2f MaximumFishTorque=%.3f FishTorque=%.3f CatTorqueCapacity=%.3f "
				"OperatorActiveStrength=%.3f CatAcceleration=%.3f FishAcceleration=%.3f LineTensionN=%.3f RodActionEstimatedStaminaDrain=%.3f "
				"Stalemate=%s Fish=%s RodTip=%s Holder=%s NetMode=%d Authority=true "
				"StepId=%llu Frame=%llu WorldTime=%.6f WorldGapSeconds=%.6f FixedStepSeconds=%.6f World=%s LocalRole=%d "
				"Phase=%s Behavior=%s ForcedEscape=%s Outcome=%s FishBefore=%s FishVelocityBeforeCmS=%s ResolvedFishVelocityCmS=%s "
				"DesiredFishDirection=%s SteeringTarget=%s RetargetRemainingSeconds=%.4f BoundaryAvoidanceSeconds=%.4f "
				"PositionCorrectionCm=%.4f CMCEndpointPredicted=%d ConstraintRodEnd=%s "
				"RodForward=%s RodTipVelocityCmS=%s HolderVelocityCmS=%s FishStamina=%.4f CatStamina=%.4f"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			bConstraintActive ? TEXT("true") : TEXT("false"),
			CatActionName,
			Step.ConstraintErrorCentimeters,
			Step.RelativeConstraintSpeedCentimetersPerSecond,
			Step.NormalizedTension,
			Step.FishConstraintCorrectionCentimeters,
			Step.RodLeverageMultiplier,
			Config.RodPhysicsLengthCentimeters,
			RotationResistance.MaximumFishTorqueStrengthMeters,
			RotationResistance.FishResistingTorqueStrengthMeters,
			RotationResistance.CatTorqueCapacityStrengthMeters,
			Step.OperatorCatStrength,
			Step.CatDriveAccelerationCentimetersPerSecondSquared,
			Step.FishDriveAccelerationCentimetersPerSecondSquared,
			Step.LineTensionNewtons,
			Step.CatStaminaDrain,
			Step.bStalemate ? TEXT("true") : TEXT("false"),
			*Motion.FishWorldPosition.ToCompactString(),
			*RodTip.ToCompactString(),
			*GetNameSafe(Rod->GetHolderPawnFromAuthority()),
			static_cast<int32>(World->GetNetMode()), DiagnosticFixedStepSequence, GFrameCounter, WorldSeconds,
			FixedStepWorldGap, Config.FixedStepSeconds, *GetNameSafe(World), static_cast<int32>(SessionActor->GetLocalRole()),
			*UEnum::GetValueAsString(State.MotionIntent), *UEnum::GetValueAsString(SteeringState.Behavior),
			bExhaustedCatEscape ? TEXT("true") : TEXT("false"), SimulationOutcomeName,
			*State.FishWorldPosition.ToCompactString(), *State.FishVelocityCentimetersPerSecond.ToCompactString(),
			*Step.ResolvedFishVelocityCentimetersPerSecond.ToCompactString(),
			*DesiredFishDirection.ToCompactString(), *SteeringState.TargetDirection.ToCompactString(),
			SteeringState.RetargetSecondsRemaining, SteeringState.BoundaryAvoidanceSecondsRemaining,
			Step.Trace.FishPositionCorrectionCentimeters, Step.Trace.bCMCEndpointPredicted,
			*Step.Trace.ConstraintRodEndWorldPosition.ToCompactString(),
			*RodConstraint.RodForwardWorld.ToCompactString(), *RodConstraint.RodTipVelocityCentimetersPerSecond.ToCompactString(),
			*RodConstraint.CarrierVelocityCentimetersPerSecond.ToCompactString(),
			State.FishStamina, State.CatStamina);
		NextConstraintDiagnosticWorldSeconds = WorldSeconds + 1.0;
		bLastConstraintDiagnosticActive = bConstraintActive;
	}
	if (!ApplyOperatorStaminaChanges(Step))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("PrimaryAbilityStaminaWrite"));
		return;
	}
	// Replace the shared-cost estimate with the one authoritative individual + shared ledger total.
	Step.CatStaminaDrain = LastOperatorStaminaDrain;
	// Runner 只记录本场累计磨损；下面 Session 按差额立即写回绑定鱼竿实例，结束不退款。
	// 把新的运动意图/线长/位置应用到 Encounter Actor 失败时视为不可恢复，终止本次搏斗。
	if (!Encounter->ApplyFightStepFromAuthority(State.MotionIntent,
		Step.LineLengthCentimeters, Motion.FishWorldPosition, static_cast<float>(Config.FixedStepSeconds),
		static_cast<float>(Step.FishLineAlignment), static_cast<float>(Step.NormalizedLineLoad),
		static_cast<float>(Step.IntendedSwimSpeedCentimetersPerSecond), Step.bStrongConfrontation,
		bFishBeached, GroundSurfaceNormal, DesiredFishDirection,
		State.bFishExhausted ? ECatFishBehavior::None : SteeringState.Behavior,
		static_cast<float>(State.FishEffortRatio)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("EncounterFightStepWrite"));
		return;
	}
	// 所有副作用都成功落地后，才把本步结果正式写回 Runner 自己持有的状态，作为下一步 Step 的输入基准。
	State.FishStamina = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	const double PreviousLineLength = State.LineLengthCentimeters;
	State.LineLengthCentimeters = Step.LineLengthCentimeters;
	// 保留原始按键，按最终线杯容量刷新有效动作；本步放尽后发布的快照立即退出放线。
	// 本步强制拖水已经把动作覆盖为 None；最终刷新不得用残留右键重新标成放线。
	if (!Step.bExhaustedCatEscape) RefreshCatAction();
	const bool bLineAtMaximum = FCatFishingFightSimulator::IsLineAtMaximum(Config, State.LineLengthCentimeters);
	if (DiagnosticFixedStepSequence == 1
		|| bLineAtMaximum != FCatFishingFightSimulator::IsLineAtMaximum(Config, PreviousLineLength))
	{
		const FCatFightOperatorRuntime* Primary = GetPrimaryOperator();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_line_limit_changed SessionId=%s RodActorId=%s PlayerId=%d "
				"PreviousLineLengthCm=%.3f LineLengthCm=%.3f MaximumLineLengthCm=%.3f AtLimit=%s "
				"SlackHeld=%s EffectiveAction=%s SlackRecovery=%s ActualOperatorStaminaDrain=%.4f RodActionBillEstimate=%.4f "
				"FishStaminaDrain=%.4f RodWearDelta=%.4f Result=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			Primary && Primary->PlayerState.IsValid() ? Primary->PlayerState->GetPlayerId() : INDEX_NONE,
			PreviousLineLength, State.LineLengthCentimeters, Config.MaximumLineLengthCentimeters,
			bLineAtMaximum ? TEXT("true") : TEXT("false"), Primary && Primary->bSlackHeld ? TEXT("true") : TEXT("false"),
			State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull") : State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None"),
			Step.bSlackRecoveryActive ? TEXT("true") : TEXT("false"), Step.CatStaminaDrain, Step.GetRodActionStaminaDrain(),
			Step.FishStaminaDrain, Step.RodWearDelta, bLineAtMaximum ? TEXT("NormalLockedContest") : TEXT("LineCapacityAvailable"),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(SessionActor->GetLocalRole()));
	}
	State.AbsoluteRodWear = Step.AbsoluteRodWear;
	State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
	// 保存受力积分速度，地形修正在 ResolveFishSurface 中反馈；几何纠偏不能变成下一步惯性。
	State.FishVelocityCentimetersPerSecond = State.bFishExhausted || Step.Outcome != ECatFightStepOutcome::None
		? FVector::ZeroVector : Step.ResolvedFishVelocityCentimetersPerSecond
			+ (Encounter->GetActorLocation() - Step.ProposedFishWorldPosition) / Config.FixedStepSeconds;
	State.FishVelocityCentimetersPerSecond.Z = 0.0;
	State.FishWorldPosition = Encounter->GetActorLocation();
	// 把本步结果（含鱼竿磨损）、剩余体力与运动意图上报给 Session，由它决定是否切换阶段/终止会话。
	PreviousFishLineTensionNewtons = Step.LineTensionNewtons;
	PreviousFishEffortDirection = Step.FishEffortDirection;
	PreviousFishExpectedSwimSpeedCentimetersPerSecond = Step.IntendedSwimSpeedCentimetersPerSecond;
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent);
}
