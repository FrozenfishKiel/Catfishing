#include "Fishing/Simulation/CatFishingFightRunner.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "Fishing/Simulation/CatFishingCooperativePowerModel.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"

bool UCatFishingFightRunner::InitializeFromAuthority(const FCatFishingFightRunnerInit& Init)
{
	ACatFishingSession* SessionActor = Init.Session.Get();
	// 一次性初始化守卫：已初始化过、Session 无效/非权威、任何依赖弱引用失效、配置非法都直接拒绝。
	if (bInitialized || !SessionActor || !SessionActor->HasAuthority() || !Init.FishActor.IsValid()
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid() || !Init.PrimaryPlayerState.IsValid()
		|| !Init.WaterRegion.IsValid() || !Init.FrozenWaterBounds.IsValid
		|| !Init.Config.IsValid() || Init.RandomSeed == 0
		|| Init.InitialInputSequence < 0
		|| Init.CalmDurationRangeSeconds.X <= 0.0 || Init.CalmDurationRangeSeconds.Y < Init.CalmDurationRangeSeconds.X
		|| Init.StruggleDurationRangeSeconds.X <= 0.0 || Init.StruggleDurationRangeSeconds.Y < Init.StruggleDurationRangeSeconds.X
		|| !FMath::IsFinite(Init.LowStaminaRestThreshold) || Init.LowStaminaRestThreshold < 0.0 || Init.LowStaminaRestThreshold > 1.0
		|| !FMath::IsFinite(Init.LowStaminaRestMultiplier) || Init.LowStaminaRestMultiplier < 1.0
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
	FrozenWaterBounds = Init.FrozenWaterBounds;
	Config = Init.Config;
	State = Init.InitialState;
	State.bOperatorPresent = true;
	CalmDurationRangeSeconds = Init.CalmDurationRangeSeconds;
	StruggleDurationRangeSeconds = Init.StruggleDurationRangeSeconds;
	LowStaminaRestThreshold = Init.LowStaminaRestThreshold;
	LowStaminaRestMultiplier = Init.LowStaminaRestMultiplier;
	SteeringConfig = Init.SteeringConfig;
	BehaviorStateTree = Init.BehaviorStateTree;
	// 记录鱼的初始体力（至少为一个极小正数，避免后面用它做分母时除零），用于低体力判定的比例基准。
	InitialFishStamina = FMath::Max(State.FishStamina, UE_DOUBLE_SMALL_NUMBER);
	// 用服务器分配的种子初始化随机流，保证同一次搏斗在权威端是确定性可复算的。
	Random.Initialize(static_cast<int32>(Init.RandomSeed));
	// 转向与阶段时长使用两个独立随机流：以后增加转向抽样不会悄悄改变发力/休息时长序列。
	SteeringRandom.Initialize(static_cast<int32>(Init.RandomSeed ^ 0x9E3779B9u));
	// StateTree 的第一个可选状态同样是向外发力；这里先放一个合法初值，StartLogic 同步进入状态时会从唯一写口覆盖。
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	// 每场搏斗从 0% 力量开始；物理按键可以保持按住，但贡献必须经过 2 秒蓄力模型逐步建立。
	if (!AddParticipantFromAuthority(Init.PrimaryPlayerState.Get(), true,
		Init.bInitialPullHeld, Init.bInitialSlackHeld, Init.InitialInputSequence))
	{
		return false;
	}
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 0.0;
	Config.PrimaryPowerAlpha = 0.0;
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
	// 按配置的定步长注册重复定时器，之后每隔 FixedStepSeconds 调用一次 HandleFixedStep 推进一步搏斗模拟。
	World->GetTimerManager().SetTimer(FixedStepTimer, this, &ThisClass::HandleFixedStep,
		static_cast<float>(Config.FixedStepSeconds), true);
	return true;
}

void UCatFishingFightRunner::Stop()
{
	if (ACatFishEncounterActor* Encounter = FishActor.Get())
	{
		Encounter->StopFishBehaviorFromAuthority();
	}
	// 清掉定步长定时器，停止后续的 HandleFixedStep 调用；Session 失效时定时器本身已经不存在，直接跳过。
	if (ACatFishingSession* SessionActor = Session.Get())
	{
		if (UWorld* World = SessionActor->GetWorld()) World->GetTimerManager().ClearTimer(FixedStepTimer);
	}
	if (ACatFishingRodActor* Rod = RodActor.Get())
	{
		Rod->ClearCarrierConstraintFromAuthority();
	}
	bRunning = false;
}

FCatFightParticipantRuntime* UCatFishingFightRunner::FindParticipant(APlayerState* PlayerState)
{
	return PlayerState ? Participants.Find(TWeakObjectPtr<APlayerState>(PlayerState)) : nullptr;
}

FCatFightParticipantRuntime* UCatFishingFightRunner::FindPrimaryParticipant()
{
	for (TPair<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime>& Pair : Participants)
	{
		if (Pair.Value.bPrimary)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

bool UCatFishingFightRunner::AddParticipantFromAuthority(APlayerState* PlayerState, const bool bPrimary,
	const bool bInitialPullHeld, const bool bInitialSlackHeld, const int64 InitialInputSequence)
{
	ACatCharacter* Character = PlayerState ? Cast<ACatCharacter>(PlayerState->GetPawn()) : nullptr;
	UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	float StaminaMaximum = 0.0f;
	const double Strength = ASC
		? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()) : 0.0;
	const double Stamina = ASC
		? ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) : 0.0;
	if (!PlayerState || !Character || !ASC || InitialInputSequence < 0
		|| !FMath::IsFinite(Strength) || Strength <= 0.0
		|| !FMath::IsFinite(Stamina) || Stamina <= 0.0
		|| !GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(
			Character->GetCatDefinitionId(), StaminaMaximum)
		|| !FMath::IsFinite(StaminaMaximum) || StaminaMaximum <= 0.0f)
	{
		return false;
	}

	FCatFightParticipantRuntime& Participant = Participants.FindOrAdd(TWeakObjectPtr<APlayerState>(PlayerState));
	Participant.PlayerState = PlayerState;
	Participant.Character = Character;
	Participant.AbilitySystem = ASC;
	Participant.BaseFishingStrength = Strength;
	Participant.StaminaMaximum = StaminaMaximum;
	Participant.PowerAlpha = 0.0;
	Participant.StaminaDrainPerSecond = 0.0;
	Participant.LastInputSequence = InitialInputSequence;
	Participant.bPullHeld = bInitialPullHeld;
	// 线杯只属于主位。辅助位右键在命令入口是 no-op，这里也不能继承其加入前的物理按键状态，
	// 否则一次“按住右键加入”会让辅助位永久停在 0% 且 SlackReleased 同样无法清除它。
	Participant.bSlackHeld = bPrimary && bInitialSlackHeld;
	Participant.bPrimary = bPrimary;
	if (ACatFishingSession* SessionActor = Session.Get())
	{
		SessionActor->RegisterFightStaminaParticipantFromAuthority(Character);
	}
	return true;
}

bool UCatFishingFightRunner::RefreshParticipantsFromRod()
{
	ACatFishingRodActor* Rod = RodActor.Get();
	ACatFishingSession* SessionActor = Session.Get();
	if (!Rod || !SessionActor)
	{
		return false;
	}

	const TArray<TObjectPtr<APlayerState>>& Operators = Rod->GetPresentationState().OperatorPlayerStates;
	TSet<TWeakObjectPtr<APlayerState>> CurrentOperators;
	TArray<TWeakObjectPtr<APlayerState>> InvalidHelpers;
	for (int32 Index = 0; Index < Operators.Num(); ++Index)
	{
		APlayerState* PlayerState = Operators[Index];
		if (!PlayerState)
		{
			continue;
		}
		CurrentOperators.Add(PlayerState);
		if (FCatFightParticipantRuntime* Existing = FindParticipant(PlayerState))
		{
			Existing->bPrimary = Index == 0;
			continue;
		}

		bool bPullHeld = false;
		bool bSlackHeld = false;
		int64 InputSequence = 0;
		if (const ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(
			PlayerState->GetPawn() ? PlayerState->GetPawn()->GetController() : nullptr))
		{
			if (const UCatFishingCommandComponent* Commands = Controller->GetFishingCommandComponent())
			{
				Commands->TryGetHeldFightInputStateFromAuthority(bPullHeld, bSlackHeld, InputSequence);
			}
		}
		// 新辅助位与接手主位都从 0% 开始蓄力；体力直接读取该角色当下 ASC，不做补满。
		if (!AddParticipantFromAuthority(PlayerState, Index == 0, bPullHeld, bSlackHeld, InputSequence)
			&& Index > 0)
		{
			CurrentOperators.Remove(PlayerState);
			InvalidHelpers.Add(PlayerState);
		}
	}
	for (const TWeakObjectPtr<APlayerState>& WeakHelper : InvalidHelpers)
	{
		APlayerState* Helper = WeakHelper.Get();
		const int32 SlotIndex = Helper ? Rod->GetOperatorSlotIndex(Helper) : INDEX_NONE;
		APlayerState* IgnoredPromotion = nullptr;
		const int64 ExpectedRevision = Rod->GetPresentationState().RodActorRevision;
		if (SlotIndex > 0 && Rod->RemoveOperatorFromAuthority(Helper, ExpectedRevision, IgnoredPromotion))
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_helper_join_rejected SessionId=%s RodActorId=%s Slot=%d Reason=FightCapabilityUnavailable Result=AutoUnloaded"),
				*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), SlotIndex);
		}
	}

	for (auto It = Participants.CreateIterator(); It; ++It)
	{
		if (!CurrentOperators.Contains(It.Key()))
		{
			SessionActor->UnregisterFightStaminaParticipantFromAuthority(It.Value().Character.Get());
			It.RemoveCurrent();
		}
	}
	return !State.bOperatorPresent || FindPrimaryParticipant() != nullptr;
}

bool UCatFishingFightRunner::UpdateParticipantPowerAndStrength(double& OutCombinedHelperDrainPerSecond)
{
	OutCombinedHelperDrainPerSecond = 0.0;
	double PrimaryStrength = 0.0;
	double HelperStrength = 0.0;
	FCatFightParticipantRuntime* Primary = nullptr;
	for (TPair<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime>& Pair : Participants)
	{
		FCatFightParticipantRuntime& Participant = Pair.Value;
		UCatAbilitySystemComponent* ASC = Participant.AbilitySystem.Get();
		if (!ASC)
		{
			return false;
		}
		const double CurrentStrength = ASC->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
		if (!FMath::IsFinite(CurrentStrength) || CurrentStrength <= 0.0)
		{
			return false;
		}
		Participant.BaseFishingStrength = CurrentStrength;
		// 右键仍保留为主位的主动立即放线；它会清空当前蓄力，松开后必须重新从 0 开始。
		if (Participant.bSlackHeld)
		{
			Participant.PowerAlpha = 0.0;
		}
		const FCatFightPowerStepResult Power = FCatFishingCooperativePowerModel::StepParticipant(
			Config.PowerTuning, Config.FixedStepSeconds, Participant.PowerAlpha,
			Participant.bPullHeld && !Participant.bSlackHeld, Participant.bPrimary,
			Participant.BaseFishingStrength);
		if (!Power.bSucceeded)
		{
			return false;
		}
		Participant.PowerAlpha = Power.PowerAlpha;
		Participant.StaminaDrainPerSecond = Power.StaminaDrainPerSecond;
		if (Participant.bPrimary)
		{
			Primary = &Participant;
			PrimaryStrength = Power.StrengthContribution;
		}
		else
		{
			HelperStrength += Power.StrengthContribution;
			OutCombinedHelperDrainPerSecond += Power.StaminaDrainPerSecond;
		}
	}

	if (State.bOperatorPresent && !Primary)
	{
		return false;
	}
	Config.PrimaryOperatorCatStrength = PrimaryStrength;
	Config.SecondCatStrength = HelperStrength;
	Config.PrimaryPowerAlpha = Primary ? Primary->PowerAlpha : 0.0;
	Config.PrimaryDisruptionStaminaDrainPerSecond =
		FCatFishingCooperativePowerModel::ComputePrimaryDisruptionDrainPerSecond(
			Config.PowerTuning, Config.PrimaryPowerAlpha, OutCombinedHelperDrainPerSecond);
	RefreshCatAction();
	return Config.IsValid();
}

bool UCatFishingFightRunner::ApplyHelperStaminaChanges(
	TArray<TWeakObjectPtr<APlayerState>>& OutDepletedHelpers)
{
	OutDepletedHelpers.Reset();
	for (TPair<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime>& Pair : Participants)
	{
		FCatFightParticipantRuntime& Participant = Pair.Value;
		if (Participant.bPrimary)
		{
			continue;
		}
		UCatAbilitySystemComponent* ASC = Participant.AbilitySystem.Get();
		if (!ASC)
		{
			return false;
		}
		const double Current = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		const double Drain = FMath::Min(Current,
			Participant.StaminaDrainPerSecond * Config.FixedStepSeconds);
		if (Drain > UE_DOUBLE_SMALL_NUMBER
			&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Drain)))
		{
			return false;
		}
		if (Current - Drain <= UE_DOUBLE_SMALL_NUMBER)
		{
			Participant.PowerAlpha = 0.0;
			Participant.bPullHeld = false;
			OutDepletedHelpers.Add(Pair.Key);
		}
	}
	return true;
}

void UCatFishingFightRunner::RefreshCatAction()
{
	// 主位有蓄力（包括松开后的 1 秒衰减）时继续收线；降到 0 后自动进入放线。
	const FCatFightParticipantRuntime* Primary = FindPrimaryParticipant();
	State.CatAction = !State.bOperatorPresent || !Primary || Primary->bSlackHeld
		|| Primary->PowerAlpha <= UE_DOUBLE_SMALL_NUMBER
		? ECatFightCatAction::Slack : ECatFightCatAction::Pull;
}

bool UCatFishingFightRunner::SetReeling(APlayerState* InputPlayerState,
	const int64 InputSequence, const bool bInReeling)
{
	FCatFightParticipantRuntime* Participant = FindParticipant(InputPlayerState);
	if (!Participant && RefreshParticipantsFromRod())
	{
		Participant = FindParticipant(InputPlayerState);
	}
	if (!bInitialized || !bRunning || !Participant || InputSequence <= Participant->LastInputSequence)
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
	FCatFightParticipantRuntime* Participant = FindParticipant(InputPlayerState);
	if (!Participant && RefreshParticipantsFromRod())
	{
		Participant = FindParticipant(InputPlayerState);
	}
	if (!bInitialized || !bRunning || !Participant || !Participant->bPrimary
		|| InputSequence <= Participant->LastInputSequence)
	{
		return false;
	}
	Participant->LastInputSequence = InputSequence;
	Participant->bSlackHeld = bInSlacking;
	RefreshCatAction();
	return true;
}

bool UCatFishingFightRunner::BeginUnattendedSlackFromAuthority()
{
	if (!bInitialized) return false;
	State.bOperatorPresent = false;
	State.StrongConfrontationBuildUpSeconds = 0.0;
	AbilitySystem.Reset();
	RefreshCatAction();
	return true;
}

bool UCatFishingFightRunner::TransferOperatorFromAuthority(APlayerState* NewPlayerState,
	UCatAbilitySystemComponent* NewAbilitySystem,
	const double NewCatStrength, const double NewCatStaminaMaximum, const double NewCatStamina,
	const int64 InitialInputSequence, const bool bInitialPullHeld, const bool bInitialSlackHeld)
{
	FCatFightSimulationConfig CandidateConfig = Config;
	CandidateConfig.PrimaryOperatorCatStrength = NewCatStrength;
	CandidateConfig.CatStaminaMaximum = NewCatStaminaMaximum;
	if (!bInitialized || !NewPlayerState || !NewAbilitySystem || InitialInputSequence < 0 || !CandidateConfig.IsValid()
		|| !FMath::IsFinite(NewCatStamina) || NewCatStamina <= 0.0
		|| NewCatStamina > NewCatStaminaMaximum + UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	AbilitySystem = NewAbilitySystem;
	Config = CandidateConfig;
	State.CatStamina = FMath::Clamp(NewCatStamina, 0.0, NewCatStaminaMaximum);
	State.bOperatorPresent = true;
	State.StrongConfrontationBuildUpSeconds = 0.0;
	for (TPair<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime>& Pair : Participants)
	{
		Pair.Value.bPrimary = false;
	}
	if (!AddParticipantFromAuthority(NewPlayerState, true, bInitialPullHeld,
		bInitialSlackHeld, InitialInputSequence))
	{
		return false;
	}
	AbilitySystem = NewAbilitySystem;
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.PrimaryPowerAlpha = 0.0;
	RefreshCatAction();
	return true;
}

bool UCatFishingFightRunner::BeginBehaviorStateFromStateTree(const ECatFishMotionIntent MotionIntent,
	double& OutDurationSeconds)
{
	OutDurationSeconds = 0.0;
	if (!bInitialized || !bRunning
		|| (MotionIntent != ECatFishMotionIntent::StrugglingOutward
			&& MotionIntent != ECatFishMotionIntent::CalmOrInward))
	{
		return false;
	}
	State.MotionIntent = MotionIntent;
	if (MotionIntent == ECatFishMotionIntent::StrugglingOutward)
	{
		OutDurationSeconds = Random.FRandRange(StruggleDurationRangeSeconds.X, StruggleDurationRangeSeconds.Y);
	}
	else
	{
		const double RestScale = State.FishStamina / InitialFishStamina < LowStaminaRestThreshold
			? LowStaminaRestMultiplier : 1.0;
		OutDurationSeconds = Random.FRandRange(CalmDurationRangeSeconds.X, CalmDurationRangeSeconds.Y) * RestScale;
	}
	return FMath::IsFinite(OutDurationSeconds) && OutDurationSeconds > 0.0;
}

void UCatFishingFightRunner::HandleFixedStep()
{
	// [FishLogic 总调度]
	// 每 0.05 秒按 1节奏 → 2方向 → 3受力公式 → 4服务器落位/复制 的顺序推进一次。
	// 逐个解析本步需要用到的弱引用，任何一个失效都说明搏斗依赖已经被销毁/离线，走失败收尾。
	ACatFishingSession* SessionActor = Session.Get();
	ACatFishEncounterActor* Encounter = FishActor.Get();
	ACatFishingRodActor* Rod = RodActor.Get();
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
	double CombinedHelperDrainPerSecond = 0.0;
	if (!RefreshParticipantsFromRod()
		|| !UpdateParticipantPowerAndStrength(CombinedHelperDrainPerSecond))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("CooperativeParticipantRefresh"));
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

	// 高层意图与状态持续时间由鱼行为 StateTree 推进；Runner 只读取当前意图，不保存第二份阶段倒计时。
	// 每步开始都以 Encounter Actor 的实际 Transform 为准同步鱼的位置，避免和上一步的建议位置产生累积误差。
	State.FishWorldPosition = Encounter->GetActorLocation();
	const FVector RodTip = Rod->GetRodTipWorldTransform().GetLocation();
	FVector Outward = State.FishWorldPosition - RodTip; // 竿尖指向鱼的方向即为鱼线“向外”方向
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector;
	FVector DesiredFishDirection;
	// 服务器按性格和固定随机种子生成连续游向；客户端不参与随机，只接收最终权威 Transform/表现状态。
	const double FishStaminaRatio = FMath::Clamp(State.FishStamina / InitialFishStamina, 0.0, 1.0);
	if (!FCatFishSteeringModel::Step(SteeringConfig, Outward, State.MotionIntent, FishStaminaRatio,
		Config.FixedStepSeconds,
		SteeringRandom, SteeringState, DesiredFishDirection))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("FishSteering"));
		return;
	}
	// 从同一根权威 Rod Actor 读取规范竿尖/竿向/速度；客户端动画 Socket 与客户端自报受力均不参与裁决。
	FCatFightRodConstraintInput RodConstraint;
	RodConstraint.RodTipWorldPosition = RodTip;
	RodConstraint.RodForwardWorld = Rod->GetAuthoritativeRodForwardVector();
	RodConstraint.RodTipVelocityCentimetersPerSecond = Rod->GetAuthoritativeRodTipVelocity();
	RodConstraint.CarrierVelocityCentimetersPerSecond = Rod->GetAuthoritativeHolderVelocity();
	RodConstraint.bRodHeld = Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held;
	// 纯模拟器把鱼游向、竿向和持竿者移动合成为有效力量，再得到双方体力、线长、负载和建议位置。
	FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, RodConstraint, DesiredFishDirection);
	Step.ActiveHelperCount = FMath::Max(0, Participants.Num() - (State.bOperatorPresent ? 1 : 0));
	FCatFishMotionSolveInput MotionInput;
	MotionInput.RodTipWorldPosition = RodTip;
	MotionInput.ProposedFishWorldPosition = Step.ProposedFishWorldPosition; // Step 算出的理想新位置（未考虑水域边界）
	MotionInput.WaterBounds = FrozenWaterBounds;
	MotionInput.MaximumLineLengthCentimeters = Config.MaximumLineLengthCentimeters;
	// 运动解算器把理想位置约束到水域边界/线长范围内，得到一个几何上合法的候选位置。
	FCatFishMotionSolveResult Motion = FCatFishFightMotionSolver::Solve(MotionInput);
	// 再用水域子系统把候选点精确吸附到真实水面上（Solver 只做粗略几何约束，这里做权威校正）。
	const FCatWaterSpatialResult Exact = Motion.bSucceeded
		? Water->ResolveCandidatePointToWater(Motion.FishWorldPosition, WaterRegion) : FCatWaterSpatialResult{};
	if (!Step.bSucceeded || !Motion.bSucceeded || !Exact.bSucceeded)
	{
		// 数学计算、运动解算或水域校正任一环节失败，说明状态已不可信，直接终止搏斗而不是继续跑坏数据。
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(!Step.bSucceeded ? TEXT("FightSimulation")
			: !Motion.bSucceeded ? TEXT("MotionSolve") : TEXT("WaterResolve"));
		return;
	}
	// [FishLogic 2/5：岸线碰撞反馈]
	// 水域解析为了保证点严格在水内，可能直接跳到“岸线 + MinimumWaterInset”；这个安全修正适合抛竿落点，
	// 但用于每 0.05 秒的活鱼运动会形成明显回弹。活鱼撞岸当帧因此保留上一合法位置，下一步再靠反射游向平滑离开。
	FCatFishShoreContactInput ShoreInput;
	ShoreInput.CurrentFishWorldPosition = State.FishWorldPosition;
	ShoreInput.CandidateFishWorldPosition = Motion.FishWorldPosition;
	ShoreInput.ResolvedWaterWorldPosition = Exact.WaterSurfaceWorldPoint;
	ShoreInput.WaterwardDirection = Exact.WaterwardDirection;
	ShoreInput.RodTipWorldPosition = RodTip;
	ShoreInput.PreviousLineLengthCentimeters = State.LineLengthCentimeters;
	ShoreInput.ProposedLineLengthCentimeters = Step.LineLengthCentimeters;
	// 手持双端约束允许端点暂时存在可诊断的张力误差；岸线修正只能在本步已求得的实际距离内移动，
	// 不能再用较短的 L_paid 把鱼偷偷硬投影一次。
	ShoreInput.MaximumConstraintDistanceCentimeters = FMath::Max(
		Step.LineLengthCentimeters, FVector::Distance(RodTip, Motion.FishWorldPosition));
	ShoreInput.bReeling = State.CatAction == ECatFightCatAction::Pull;
	ShoreInput.bSlacking = State.CatAction == ECatFightCatAction::Slack;
	FCatFishShoreContactResult ShoreContact = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(ShoreInput);
	if (!ShoreContact.bSucceeded)
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("ShoreContactSolve"));
		return;
	}
	if (ShoreContact.bShoreContact
		&& !FCatFishSteeringModel::RedirectFromWaterBoundary(SteeringConfig,
			Exact.WaterwardDirection, SteeringRandom, SteeringState))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("ShoreSteeringRedirect"));
		return;
	}
	Motion.FishWorldPosition = ShoreContact.FishWorldPosition;
	Step.LineLengthCentimeters = ShoreContact.LineLengthCentimeters;
	Step.StraightLineDistanceCentimeters = FVector::Distance(RodTip, Motion.FishWorldPosition);
	Step.SlackLineLengthCentimeters = FMath::Max(0.0,
		Step.LineLengthCentimeters - Step.StraightLineDistanceCentimeters);
	Step.bLineTaut = Step.SlackLineLengthCentimeters <= UE_DOUBLE_KINDA_SMALL_NUMBER;
	if (!Step.bLineTaut)
	{
		// 岸线修正若让线重新产生余量，本步已经不再受线端约束，清掉只用于表现的瞬时张力。
		Step.TensionCentimeters = 0.0;
		Step.NormalizedTension = 0.0;
		Step.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
		Step.CarrierAwaySpeedMultiplier = 1.0;
		Step.ConstraintErrorCentimeters = 0.0;
		Step.RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	}

	// 靠近岸线只是空间事实，不再终止搏斗。抄网随时用自己的服务器射线判定；
	// 只有 FishExhausted/Overpowered 才切入后续“侧翻并收向竿尖水面投影”阶段。

	// Runner 只发布这一固定步的统一约束结果；Rod 在服务器与持竿本地客户端每帧把它应用到 CharacterMovement。
	// 因而普通移动仍负责碰撞与网络预测，但持续输入不能再最终跑回完全不受限的 MaxWalkSpeed。
	if (RodConstraint.bRodHeld && Step.NormalizedTension > UE_DOUBLE_SMALL_NUMBER)
	{
		const APawn* Holder = Rod->GetHolderPawnFromAuthority();
		FVector PullDirection = Motion.FishWorldPosition
			- (Holder ? Holder->GetActorLocation() : RodTip);
		PullDirection.Z = 0.0;
		if (!Rod->SetCarrierConstraintFromAuthority(PullDirection,
			Step.CarrierPullAccelerationCentimetersPerSecondSquared,
			Step.CarrierAwaySpeedMultiplier, Step.NormalizedTension,
			Step.ConstraintErrorCentimeters))
		{
			Stop();
			SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("CarrierConstraintWrite"));
			return;
		}
	}
	else
	{
		Rod->ClearCarrierConstraintFromAuthority();
	}
	const bool bConstraintActive = RodConstraint.bRodHeld
		&& Step.NormalizedTension > UE_DOUBLE_SMALL_NUMBER;
	const double WorldSeconds = World->GetTimeSeconds();
	if (WorldSeconds >= NextPowerDiagnosticWorldSeconds)
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_cooperative_power_sample SessionId=%s RodActorId=%s PrimaryPower=%.3f "
				"PrimaryStrength=%.3f HelperStrength=%.3f CombinedStrength=%.3f ActiveHelpers=%d "
				"PrimaryStamina=%.3f PrimaryStaminaDrain=%.3f FishStamina=%.3f FishStaminaDrain=%.3f "
				"DisruptionDrainPerSecond=%.3f NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			Step.PrimaryPowerAlpha,
			Config.PrimaryOperatorCatStrength,
			Config.SecondCatStrength,
			Step.CombinedCatStrength,
			Step.ActiveHelperCount,
			State.CatStamina,
			Step.CatStaminaDrain,
			State.FishStamina,
			Step.FishStaminaDrain,
			Config.PrimaryDisruptionStaminaDrainPerSecond,
			static_cast<int32>(World->GetNetMode()));
		NextPowerDiagnosticWorldSeconds = WorldSeconds + 1.0;
	}
	if (bConstraintActive != bLastConstraintDiagnosticActive
		|| (bConstraintActive && WorldSeconds >= NextConstraintDiagnosticWorldSeconds))
	{
		const TCHAR* Action = State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
			: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None");
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_constraint_sample SessionId=%s RodActorId=%s Active=%s Action=%s "
				"ConstraintError=%.2f RelativeLineSpeed=%.2f Tension=%.3f FishCorrection=%.2f "
				"CarrierAcceleration=%.2f CarrierAwaySpeedMultiplier=%.3f RodLeverage=%.3f "
				"CarrierMovementAlpha=%.3f PrimaryPower=%.3f ActiveCombinedStrength=%.3f ActiveHelpers=%d "
				"PrimaryStaminaDrain=%.3f DisruptionDrainPerSecond=%.3f Fish=%s RodTip=%s Holder=%s NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(),
			*Rod->GetPresentationState().RodActorId.ToString(),
			bConstraintActive ? TEXT("true") : TEXT("false"),
			Action,
			Step.ConstraintErrorCentimeters,
			Step.RelativeConstraintSpeedCentimetersPerSecond,
			Step.NormalizedTension,
			Step.FishConstraintCorrectionCentimeters,
			Step.CarrierPullAccelerationCentimetersPerSecondSquared,
			Step.CarrierAwaySpeedMultiplier,
			Step.RodLeverageMultiplier,
			Step.CarrierMovementAlpha,
			Step.PrimaryPowerAlpha,
			Step.CombinedCatStrength,
			Step.ActiveHelperCount,
			Step.CatStaminaDrain,
			Config.PrimaryDisruptionStaminaDrainPerSecond,
			*Motion.FishWorldPosition.ToCompactString(),
			*RodTip.ToCompactString(),
			*GetNameSafe(Rod->GetHolderPawnFromAuthority()),
			static_cast<int32>(World->GetNetMode()));
		NextConstraintDiagnosticWorldSeconds = WorldSeconds + 1.0;
		bLastConstraintDiagnosticActive = bConstraintActive;
	}
	// 把猫的体力消耗（正值）转成负的 Delta 施加到 ASC；只有真正非零变化才需要写一次，且写入失败也视为致命错误。
	if (State.bOperatorPresent && !FMath::IsNearlyZero(Step.CatStaminaDrain)
		&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Step.CatStaminaDrain)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("AbilityStaminaWrite"));
		return;
	}
	TArray<TWeakObjectPtr<APlayerState>> DepletedHelpers;
	if (!ApplyHelperStaminaChanges(DepletedHelpers))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("HelperAbilityStaminaWrite"));
		return;
	}
	// 鱼线磨损只存在于本场 Runner 状态，不写入装备永久耐久；新会话从配置上限重新开始。
	// 把新的运动意图/线长/位置应用到 Encounter Actor 失败时视为不可恢复，终止本次搏斗。
	if (!Encounter->ApplyFightStepFromAuthority(State.MotionIntent,
		Step.LineLengthCentimeters, Motion.FishWorldPosition, static_cast<float>(Config.FixedStepSeconds),
		static_cast<float>(Step.FishLineAlignment), static_cast<float>(Step.NormalizedLineLoad),
		static_cast<float>(Step.IntendedSwimSpeedCentimetersPerSecond), Step.bStrongConfrontation))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("EncounterFightStepWrite"));
		return;
	}
	// 所有副作用都成功落地后，才把本步结果正式写回 Runner 自己持有的状态，作为下一步 Step 的输入基准。
	State.CatStamina = FMath::Clamp(State.CatStamina - Step.CatStaminaDrain, 0.0, Config.CatStaminaMaximum);
	State.FishStamina = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	State.LineLengthCentimeters = Step.LineLengthCentimeters;
	State.AbsoluteRodWear = Step.AbsoluteRodWear;
	State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
	State.FishWorldPosition = Encounter->GetActorLocation(); // 再次以 Actor 实际落点为准，覆盖掉建议值可能的浮点误差
	for (const TWeakObjectPtr<APlayerState>& WeakHelper : DepletedHelpers)
	{
		APlayerState* Helper = WeakHelper.Get();
		const int32 SlotIndex = Helper ? Rod->GetOperatorSlotIndex(Helper) : INDEX_NONE;
		if (SlotIndex <= 0)
		{
			continue;
		}
		APlayerState* IgnoredPromotion = nullptr;
		const int64 ExpectedRevision = Rod->GetPresentationState().RodActorRevision;
		if (Rod->RemoveOperatorFromAuthority(Helper, ExpectedRevision, IgnoredPromotion))
		{
			if (FCatFightParticipantRuntime* Participant = FindParticipant(Helper))
			{
				SessionActor->UnregisterFightStaminaParticipantFromAuthority(Participant->Character.Get());
			}
			Participants.Remove(WeakHelper);
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_helper_exhausted SessionId=%s RodActorId=%s Slot=%d Result=AutoUnloaded"),
				*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), SlotIndex);
		}
		else
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_helper_exhausted SessionId=%s RodActorId=%s Slot=%d Result=RemoveRejected Revision=%lld"),
				*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
				SlotIndex, ExpectedRevision);
		}
	}
	// 把本步结果、剩余体力、运动意图和本场剩余鱼线耐久上报给 Session，由它决定是否切换阶段/终止会话。
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent,
		Config.RodDurability - Step.AbsoluteRodWear);
}
