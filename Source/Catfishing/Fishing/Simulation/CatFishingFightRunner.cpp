#include "Fishing/Simulation/CatFishingFightRunner.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
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
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatWorldSurfaceResolver.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"

bool UCatFishingFightRunner::InitializeFromAuthority(const FCatFishingFightRunnerInit& Init)
{
	ACatFishingSession* SessionActor = Init.Session.Get();
	// 一次性初始化守卫：已初始化过、Session 无效/非权威、任何依赖弱引用失效、配置非法都直接拒绝。
	if (bInitialized || !SessionActor || !SessionActor->HasAuthority() || !Init.FishActor.IsValid()
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid() || !Init.PrimaryPlayerState.IsValid()
		|| !Init.WaterRegion.IsValid()
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
	Config = Init.Config;
	State = Init.InitialState;
	State.bOperatorPresent = true;
	bFishBeached = false;
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
	if (!AddParticipantFromAuthority(Init.PrimaryPlayerState.Get(), true,
		Init.bInitialPullHeld, Init.bInitialSlackHeld, Init.InitialInputSequence))
	{
		return false;
	}
	Config.PrimaryOperatorCatStrength = 0.0;
	Config.SecondCatStrength = 0.0;
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
		&& !Rod->SetCarrierConstraintFromAuthority(InitialPullDirection,
			0.0, 0.0, 1.0, 0.0, 0.0, true, 0.0, Config.GetCombinedCatStrength(),
			InitialPullDirection.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector))))
	{
		Encounter->StopFishBehaviorFromAuthority();
		bRunning = false;
		return false;
	}
	// 按配置的定步长注册重复定时器，之后每隔 FixedStepSeconds 调用一次 HandleFixedStep 推进一步搏斗模拟。
	RotationEffortSampler.Reset(Rod->GetAuthoritativeRotationEffortSnapshot());
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
	Participant.ActiveFishingStrength = Strength;
	Participant.StaminaMaximum = StaminaMaximum;
	Participant.LastInputSequence = InitialInputSequence;
	Participant.bPullHeld = bInitialPullHeld;
	// 线杯只属于主位。辅助位右键在命令入口是 no-op。
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
		// 新辅助位与接手主位立即输出意图；体力直接读取该角色当下 ASC，不做补满。
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

bool UCatFishingFightRunner::UpdateParticipantIntentAndProperties()
{
	double PrimaryStrength = 0.0;
	double HelperStrength = 0.0;
	double PrimaryMass = 0.0;
	double HelperMass = 0.0;
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
		const double CurrentStamina = ASC->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		if (!FMath::IsFinite(CurrentStamina) || CurrentStamina < 0.0)
		{
			return false;
		}
		const double ActiveStrength = Participant.BaseFishingStrength
			* FMath::Clamp(CurrentStamina / Participant.StaminaMaximum, 0.0, 1.0);
		Participant.ActiveFishingStrength = ActiveStrength;
		if (Participant.bPrimary)
		{
			Primary = &Participant;
			PrimaryStrength = ActiveStrength;
			PrimaryMass = Participant.BaseFishingStrength / Config.StrengthPerKilogram;
		}
		else if (Participant.bPullHeld)
		{
			HelperStrength += ActiveStrength;
			// 辅助猫与主位使用同一力量/质量换算；体力只衰减当前驱动力，不改变等效质量。
			HelperMass += Participant.BaseFishingStrength / Config.StrengthPerKilogram;
		}
	}

	if (State.bOperatorPresent && !Primary)
	{
		return false;
	}
	Config.PrimaryOperatorCatStrength = PrimaryStrength;
	Config.SecondCatStrength = HelperStrength;
	Config.PrimaryOperatorMassKilograms = PrimaryMass;
	Config.HelperMassKilograms = HelperMass;
	RefreshCatAction();
	return Config.IsValid();
}

bool UCatFishingFightRunner::ApplyHelperStaminaChanges(const double TotalGroupDrain)
{
	if (TotalGroupDrain <= UE_DOUBLE_SMALL_NUMBER)
	{
		return true;
	}
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
		const double StrengthShare = Participant.bPullHeld
			? Participant.ActiveFishingStrength / FMath::Max(Config.GetCombinedCatStrength(), UE_DOUBLE_SMALL_NUMBER)
			: 0.0;
		const double Drain = FMath::Min(Current, TotalGroupDrain * StrengthShare);
		if (Drain > UE_DOUBLE_SMALL_NUMBER
			&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Drain)))
		{
			return false;
		}
	}
	return true;
}

void UCatFishingFightRunner::RefreshCatAction()
{
	const FCatFightParticipantRuntime* Primary = FindPrimaryParticipant();
	State.CatAction = !State.bOperatorPresent || !Primary
		? ECatFightCatAction::Slack
		: Primary->bPullHeld ? ECatFightCatAction::Pull
		: Primary->bSlackHeld ? ECatFightCatAction::Slack : ECatFightCatAction::None;
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

bool UCatFishingFightRunner::SetFishExhaustedFromAuthority()
{
	if (!bInitialized || !bRunning)
	{
		return false;
	}
	State.bFishExhausted = true;
	State.FishStamina = 0.0;
	State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	State.StrongConfrontationBuildUpSeconds = 0.0;
	// 终止鱼端驱动力的同一权威写口立即清掉上一固定步的猫端目标，不能再多拖一个模拟帧。
	if (ACatFishingRodActor* Rod = RodActor.Get())
	{
		Rod->ClearCarrierConstraintFromAuthority();
	}
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
	Config.PrimaryOperatorCatStrength = NewCatStrength;
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

bool UCatFishingFightRunner::TryResolveGroundedFishPosition(const FVector& DesiredPosition,
	FVector& OutGroundedPosition, FVector& OutSurfaceNormal, AActor*& OutSurfaceActor) const
{
	const FVector QueryPosition = DesiredPosition;
	OutGroundedPosition = FVector::ZeroVector;
	OutSurfaceNormal = FVector::UpVector;
	OutSurfaceActor = nullptr;
	UWorld* World = Session.IsValid() ? Session->GetWorld() : nullptr;
	const UCatWorldItemSettings* ItemSettings = GetDefault<UCatWorldItemSettings>();
	if (!World || !ItemSettings || QueryPosition.ContainsNaN())
	{
		return false;
	}

	TArray<const AActor*> IgnoredActors;
	IgnoredActors.Reserve(4 + Participants.Num());
	IgnoredActors.Add(Session.Get());
	IgnoredActors.Add(FishActor.Get());
	IgnoredActors.Add(RodActor.Get());
	IgnoredActors.Add(Session->GetSnapshot().HookActor.Get());
	for (const TPair<TWeakObjectPtr<APlayerState>, FCatFightParticipantRuntime>& Pair : Participants)
	{
		IgnoredActors.Add(Pair.Value.Character.Get());
	}
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
	FVector& OutGroundNormal, AActor*& OutGroundActor)
{
	FCatFishMotionSolveResult Motion;
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
		&& Step.CombinedCatStrength > UE_DOUBLE_SMALL_NUMBER ? Step.FishConstraintCorrectionCentimeters : 0.0;
	Intent.bLineTaut = Step.bLineTaut;
	const bool bCatHaulingFish = FCatFishFightMotionSolver::IsIntentionalLandwardHaul(Intent);
	// 力竭鱼没有自主游动，所有候选位移都来自同一根鱼线，不再为它加活鱼的防甩杆力竭门槛。
	const bool bSurfaceTow = (State.bFishExhausted || bCatHaulingFish)
		&& Step.Outcome != ECatFightStepOutcome::LineBroken && Step.Outcome != ECatFightStepOutcome::RodBroken
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
		ShoreInput.bSlacking = State.CatAction == ECatFightCatAction::Slack;
		const FCatFishShoreContactResult ShoreContact = FCatFishFightMotionSolver::ResolveLiveFishShoreContact(ShoreInput);
		if (!ShoreContact.bSucceeded) return Motion;
		Motion.bSucceeded = true;
		Motion.FishWorldPosition = ShoreContact.FishWorldPosition;
		Step.LineLengthCentimeters = ShoreContact.LineLengthCentimeters;
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
		const FCatFightParticipantRuntime* Primary = FindPrimaryParticipant();
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
	Step.bLineTaut = Step.SlackLineLengthCentimeters <= UE_DOUBLE_KINDA_SMALL_NUMBER;
	if (!Step.bLineTaut)
	{
		Step.TensionCentimeters = 0.0;
		Step.NormalizedTension = 0.0;
		Step.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
		Step.CarrierTargetPullSpeedCentimetersPerSecond = 0.0;
		Step.CarrierConstraintCorrectionCentimeters = 0.0;
		Step.CarrierAwaySpeedMultiplier = 1.0;
		Step.ConstraintErrorCentimeters = 0.0;
		Step.RelativeConstraintSpeedCentimetersPerSecond = 0.0;
	}
	if (bOutBeachedThisStep)
	{
		Step.bFishBeached = true;
		if (!State.bFishExhausted && Step.Outcome != ECatFightStepOutcome::LineBroken
			&& Step.Outcome != ECatFightStepOutcome::RodBroken
			&& Step.Outcome != ECatFightStepOutcome::Escaped)
		{
			Step.FishStaminaDrain = State.FishStamina;
			Step.Outcome = ECatFightStepOutcome::FishExhausted;
			Step.CarrierPullAccelerationCentimetersPerSecondSquared = 0.0;
			Step.CarrierTargetPullSpeedCentimetersPerSecond = 0.0;
			Step.CarrierConstraintCorrectionCentimeters = 0.0;
			Step.CarrierAwaySpeedMultiplier = 1.0;
		}
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
	if (!RefreshParticipantsFromRod()
		|| !UpdateParticipantIntentAndProperties())
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
		FCatFightParticipantRuntime* Primary = FindPrimaryParticipant();
		UCatConditionComponent* Condition = Primary && Primary->Character.IsValid()
			? Primary->Character->GetConditionComponent() : nullptr;
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
		if (Exposure == ECatWaterExposureUpdate::DangerousEntered)
		{
			SessionActor->HandleCatEnteredDangerousWaterFromAuthority(ImmersionDepth);
			return;
		}
	}

	// 高层意图与状态持续时间由鱼行为 StateTree 推进；Runner 只读取当前意图，不保存第二份阶段倒计时。
	// 每步开始都以 Encounter Actor 的实际 Transform 为准同步鱼的位置，避免和上一步的建议位置产生累积误差。
	State.FishWorldPosition = Encounter->GetActorLocation();
	const FVector RodTip = Rod->GetRodTipWorldTransform().GetLocation();
	FVector Outward = State.FishWorldPosition - RodTip; // 竿尖指向鱼的方向即为鱼线“向外”方向
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector;
	FVector DesiredFishDirection = FVector::ZeroVector;
	// 服务器按性格和固定随机种子生成连续游向；客户端不参与随机，只接收最终权威 Transform/表现状态。
	const double FishStaminaRatio = FMath::Clamp(State.FishStamina / InitialFishStamina, 0.0, 1.0);
	if (!State.bFishExhausted && !FCatFishSteeringModel::Step(SteeringConfig, Outward, State.MotionIntent, FishStaminaRatio,
		Config.FixedStepSeconds, SteeringRandom, SteeringState, DesiredFishDirection))
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
	if (const ACatCharacter* PrimaryCharacter = FindPrimaryParticipant()
		? FindPrimaryParticipant()->Character.Get() : nullptr)
	{
		const UCharacterMovementComponent* Movement = PrimaryCharacter->GetCharacterMovement();
		// 网络移动包在服务器更新 Acceleration，不会填充 Pawn 的本地 LastControlInputVector。
		// 本地与远端统一读取 CharacterMovement 已接受的加速度，避免客户端后退意图丢失。
		RodConstraint.CarrierDesiredVelocityCentimetersPerSecond = Movement && Movement->GetMaxAcceleration() > UE_SMALL_NUMBER
			? (Movement->GetCurrentAcceleration() / Movement->GetMaxAcceleration()).GetClampedToMaxSize(1.0)
				* Movement->GetMaxSpeed()
			: FVector::ZeroVector;
	}
	const FCatFishingRodRotationEffortSnapshot RotationEffort = RotationEffortSampler.Consume(
		Rod->GetAuthoritativeRotationEffortSnapshot(), Config.FixedStepSeconds);
	RodConstraint.CatRodIntentArcCentimeters = RotationEffort.IntentArcCentimeters;
	RodConstraint.CatRodActualArcCentimeters = RotationEffort.ActualArcCentimeters;
	// 纯模拟器把鱼游向、竿向和持竿者移动合成为有效力量，再得到双方体力、线长、负载和建议位置。
	FCatFightStepResult Step = FCatFishingFightSimulator::Step(
		Config, State, RodConstraint, DesiredFishDirection);
	FCatFishingRodResistanceInput RotationInput;
	// 转杆由主位独立操作与扣体；辅助收线力量不能让已经力竭的主位免费施加转矩。
	RotationInput.CatStrength = Config.PrimaryOperatorCatStrength;
	RotationInput.FishStrength = State.bFishExhausted ? 0.0 : Config.FishStrength;
	RotationInput.RodPhysicsLengthCentimeters = Config.RodPhysicsLengthCentimeters;
	RotationInput.NormalizedTension = Step.NormalizedTension;
	RotationInput.NormalizedFishLineLoad = Step.NormalizedLineLoad;
	RotationInput.RodLineAlignment = Step.RodLineAlignment;
	const FCatFishingRodResistanceResult RotationResistance =
		FCatFishingRodResistanceModel::Evaluate(RotationInput);
	if (!Step.bSucceeded || !RotationResistance.bSucceeded)
	{
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_fight_step_rejected SessionId=%s Stage=%s FishExhausted=%s "
				"Fish=%s RodTip=%s DesiredFishDirection=%s LineLength=%.3f NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			!Step.bSucceeded ? TEXT("FightSimulation") : TEXT("RodRotationResistance"),
			State.bFishExhausted ? TEXT("true") : TEXT("false"),
			*State.FishWorldPosition.ToCompactString(), *RodTip.ToCompactString(),
			*DesiredFishDirection.ToCompactString(), State.LineLengthCentimeters,
			static_cast<int32>(World->GetNetMode()));
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(
			!Step.bSucceeded ? TEXT("FightSimulation") : TEXT("RodRotationResistance"));
		return;
	}
	Step.ActiveHelperCount = FMath::Max(0, Participants.Num() - (State.bOperatorPresent ? 1 : 0));
	FCatWaterSpatialResult Exact;
	bool bBeachedThisStep = false;
	FVector GroundSurfaceNormal = FVector::UpVector;
	AActor* GroundSurfaceActor = nullptr;
	const FCatFishMotionSolveResult Motion = ResolveFishSurfaceFromAuthority(Step, RodConstraint,
		Exact, bBeachedThisStep, GroundSurfaceNormal, GroundSurfaceActor);
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

	// Runner 只发布这一固定步的统一约束目标；Rod 在服务器和拥有客户端的移动帧内平滑追赶目标速度。
	// CharacterMovement 仍负责碰撞与网络移动，不直接插值或瞬移 Character Transform。
	const bool bCarrierConstraintActive = RodConstraint.bRodHeld
		&& (Step.CarrierTargetPullSpeedCentimetersPerSecond > UE_DOUBLE_SMALL_NUMBER
			|| Step.CarrierAwaySpeedMultiplier < 1.0 - UE_DOUBLE_SMALL_NUMBER);
	if (RodConstraint.bRodHeld)
	{
		const APawn* Holder = Rod->GetHolderPawnFromAuthority();
		FVector PullDirection = Motion.FishWorldPosition
			- (Holder ? Holder->GetActorLocation() : RodTip);
		PullDirection.Z = 0.0;
		if (!Rod->SetCarrierConstraintFromAuthority(PullDirection,
			Step.CarrierPullAccelerationCentimetersPerSecondSquared,
			Step.CarrierTargetPullSpeedCentimetersPerSecond,
			Step.CarrierAwaySpeedMultiplier, Step.NormalizedTension,
			Step.ConstraintErrorCentimeters, true,
			RotationResistance.MaximumFishTorqueStrengthMeters,
			RotationResistance.CatTorqueCapacityStrengthMeters,
			(Motion.FishWorldPosition - RodTip)
				.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Rod->GetAuthoritativeRodForwardVector())))
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
	const TCHAR* CatActionName = State.CatAction == ECatFightCatAction::Pull ? TEXT("Pull")
		: State.CatAction == ECatFightCatAction::Slack ? TEXT("Slack") : TEXT("None");
	const double FishRealizedEffortDistance = FMath::Min(
		Step.FishActualLineDistanceCentimeters, Step.FishIntendedLineDistanceCentimeters);
	const double FishBlockedEffortDistance = FMath::Max(0.0,
		Step.FishIntendedLineDistanceCentimeters - FishRealizedEffortDistance);
	const double FishEffectiveEffortDistance = FishRealizedEffortDistance
		+ FishBlockedEffortDistance * Config.IsometricEffortMultiplier;
	const double FishPhaseDrainMultiplier = State.MotionIntent == ECatFishMotionIntent::StrugglingOutward
		? Config.StruggleDrainMultiplier : Config.BaseDrainMultiplier;
	const double FishUncappedStaminaDrain = Step.FishUncappedStaminaDrain;
	const double FishStaminaAfterStep = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	const FVector SimulatorFishDelta = Step.ProposedFishWorldPosition - State.FishWorldPosition;
	const FVector ResolvedFishDelta = Motion.FishWorldPosition - State.FishWorldPosition;
	const double ResolvedLineDistance2D = FVector::Dist2D(RodTip, Motion.FishWorldPosition);
	const double ResolvedLineVerticalDistance = FMath::Abs(RodTip.Z - Motion.FishWorldPosition.Z);
	const double ResolvedLineDistance3D = FVector::Distance(RodTip, Motion.FishWorldPosition);
	// 活鱼与尚未上岸的鱼干都在本步解析过水面；已经吸附地面的鱼干只解析地面，不能把 Exact 的默认零值冒充水面。
	const bool bWaterSurfaceAvailableForDiagnostic = !State.bFishExhausted || !bFishBeached;
	const double WaterSurfaceZ = bWaterSurfaceAvailableForDiagnostic
		? Exact.WaterSurfaceWorldPoint.Z : Motion.FishWorldPosition.Z;
	const double FishWaterSurfaceOffsetZ = bWaterSurfaceAvailableForDiagnostic
		? Motion.FishWorldPosition.Z - WaterSurfaceZ : 0.0;
	const bool bFishStaminaTerminalStep = !State.bFishExhausted
		&& Step.Outcome == ECatFightStepOutcome::FishExhausted;
	const bool bFishStaminaSpike = !State.bFishExhausted && !bFishStaminaTerminalStep
		&& Step.FishStaminaDrain >= FMath::Max(5.0, InitialFishStamina * 0.1);
	const auto LogFishStaminaBreakdown = [&](const TCHAR* EventName, const TCHAR* Trigger)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=%s SessionId=%s Trigger=%s CatAction=%s MotionIntent=%s "
				"FishStaminaBefore=%.3f FishStaminaDrain=%.3f FishStaminaAfter=%.3f "
				"DrainPerSecond=%.3f UncappedDrain=%.3f FishStrength=%.3f StaminaReferenceStrength=%.3f CostPerStrengthCm=%.6f EffortLoad=%.3f LoadMultiplier=%.3f "
				"PhaseMultiplier=%.3f IsometricMultiplier=%.3f FixedStepSeconds=%.3f IntendedSwimSpeedCmPerSec=%.3f "
				"FishIntentLineCm=%.3f FishActualLineCm=%.3f FishRealizedEffortCm=%.3f "
				"FishBlockedEffortCm=%.3f FishEffectiveEffortCm=%.3f "
				"FishBefore=%s SimulatorCandidate=%s ResolvedFish=%s SimulatorDelta=%s ResolvedDelta=%s "
				"ResolvedDelta2DCm=%.3f ResolvedDelta3DCm=%.3f ResolvedDeltaZCm=%.3f "
				"WaterSurfaceAvailable=%s WaterSurfaceZ=%.3f FishWaterSurfaceOffsetZ=%.3f "
				"DesiredFishDirection=%s RodTip=%s "
				"LineLengthBefore=%.3f LineLengthAfter=%.3f LineDistance2D=%.3f LineVerticalDistance=%.3f "
				"LineDistance3D=%.3f Tension=%.3f LineLoad=%.3f Alignment=%.3f Beached=%s NetMode=%d Authority=true"),
			EventName,
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			Trigger,
			CatActionName,
			*UEnum::GetValueAsString(State.MotionIntent),
			State.FishStamina,
			Step.FishStaminaDrain,
			FishStaminaAfterStep,
			Step.FishStaminaDrain / Config.FixedStepSeconds,
			FishUncappedStaminaDrain,
			Config.FishStrength,
			Config.StrengthPerKilogram,
			Config.FishStaminaCostPerStrengthCentimeter,
			Step.FishNormalizedEffortLoad,
			Config.FishLoadStaminaMultiplier,
			FishPhaseDrainMultiplier,
			Config.IsometricEffortMultiplier,
			Config.FixedStepSeconds,
			Step.IntendedSwimSpeedCentimetersPerSecond,
			Step.FishIntendedLineDistanceCentimeters,
			Step.FishActualLineDistanceCentimeters,
			FishRealizedEffortDistance,
			FishBlockedEffortDistance,
			FishEffectiveEffortDistance,
			*State.FishWorldPosition.ToCompactString(),
			*Step.ProposedFishWorldPosition.ToCompactString(),
			*Motion.FishWorldPosition.ToCompactString(),
			*SimulatorFishDelta.ToCompactString(),
			*ResolvedFishDelta.ToCompactString(),
			ResolvedFishDelta.Size2D(),
			ResolvedFishDelta.Size(),
			ResolvedFishDelta.Z,
			bWaterSurfaceAvailableForDiagnostic ? TEXT("true") : TEXT("false"),
			WaterSurfaceZ,
			FishWaterSurfaceOffsetZ,
			*DesiredFishDirection.ToCompactString(),
			*RodTip.ToCompactString(),
			State.LineLengthCentimeters,
			Step.LineLengthCentimeters,
			ResolvedLineDistance2D,
			ResolvedLineVerticalDistance,
			ResolvedLineDistance3D,
			Step.NormalizedTension,
			Step.NormalizedLineLoad,
			Step.FishLineAlignment,
			Step.bFishBeached ? TEXT("true") : TEXT("false"),
			static_cast<int32>(World->GetNetMode()));
	};
	const bool bLogWorkSample = WorldSeconds >= NextPowerDiagnosticWorldSeconds;
	if (bLogWorkSample)
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_coupled_work_sample SessionId=%s RodActorId=%s "
				"PrimaryStrength=%.3f HelperStrength=%.3f CombinedStrength=%.3f CatSystemMassKg=%.3f FishMassKg=%.3f MassMode=StrengthDerived StrengthPerKg=%.3f ActiveHelpers=%d "
				"CatAcceleration=%.3f FishAcceleration=%.3f NetFishPullAcceleration=%.3f "
				"PrimaryStamina=%.3f GroupStaminaDrain=%.3f FishStamina=%.3f FishStaminaDrain=%.3f "
				"MotionIntent=%s CatIntentCm=%.3f CatActualCm=%.3f FishIntentCm=%.3f FishActualCm=%.3f "
				"FishWorldStep2DCm=%.3f FishWorldStep3DCm=%.3f FishWorldDeltaZCm=%.3f "
				"ReelRequestedCm=%.3f ReelActualCm=%.3f AbsoluteRodWear=%.3f RodWearDelta=%.3f "
				"MovementDrain=%.4f ReelDrain=%.4f RodDrain=%.4f HoldDrain=%.4f "
				"MovementIntentCm=%.3f MovementActualCm=%.3f RodIntentArcCm=%.3f RodActualArcCm=%.3f HoldIntentCm=%.3f "
				"CatEffortLoad=%.3f RodEffortLoad=%.3f FishEffortLoad=%.3f MovementIntentSource=CharacterMovementAcceleration "
				"FishExhausted=%s World=%s PlayerState=%s NetMode=%d Authority=true LocalRole=%d"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			Config.PrimaryOperatorCatStrength,
			Config.SecondCatStrength,
			Step.CombinedCatStrength,
			Config.GetCombinedCatMass(),
			Config.FishMassKilograms,
			Config.StrengthPerKilogram,
			Step.ActiveHelperCount,
			Step.CatDriveAccelerationCentimetersPerSecondSquared,
			Step.FishDriveAccelerationCentimetersPerSecondSquared,
			Step.NetFishPullAccelerationCentimetersPerSecondSquared,
			State.CatStamina,
			Step.CatStaminaDrain,
			State.FishStamina,
			Step.FishStaminaDrain,
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
			Step.AbsoluteRodWear,
			Step.RodWearDelta,
			Step.CatMovementStaminaDrain, Step.CatReelStaminaDrain, Step.CatRodStaminaDrain, Step.CatHoldStaminaDrain,
			Step.CatMovementIntentCentimeters, Step.CatMovementActualCentimeters,
			Step.CatRodIntentArcCentimeters, Step.CatRodActualArcCentimeters, Step.CatHoldIntentCentimeters,
			Step.CatNormalizedEffortLoad, Step.CatRodNormalizedEffortLoad, Step.FishNormalizedEffortLoad,
			State.bFishExhausted ? TEXT("true") : TEXT("false"),
			*GetNameSafe(World), *GetNameSafe(FindPrimaryParticipant() ? FindPrimaryParticipant()->PlayerState.Get() : nullptr),
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
	if (bConstraintActive != bLastConstraintDiagnosticActive
		|| (bConstraintActive && WorldSeconds >= NextConstraintDiagnosticWorldSeconds))
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_constraint_sample SessionId=%s RodActorId=%s Active=%s CarrierActive=%s Action=%s "
				"ConstraintError=%.2f RelativeLineSpeed=%.2f Tension=%.3f FishCorrection=%.2f CarrierCorrection=%.2f "
				"CarrierAcceleration=%.2f CarrierTargetPullSpeed=%.2f CarrierAwaySpeedMultiplier=%.3f RodLeverage=%.3f "
				"RodPhysicsLengthCm=%.2f MaximumFishTorque=%.3f FishTorque=%.3f CatTorqueCapacity=%.3f "
				"ActiveCombinedStrength=%.3f CatAcceleration=%.3f FishAcceleration=%.3f NetFishPullAcceleration=%.3f FishDominance=%.3f ActiveHelpers=%d GroupStaminaDrain=%.3f "
				"Stalemate=%s Fish=%s RodTip=%s Holder=%s NetMode=%d Authority=true"),
			*SessionActor->GetSnapshot().FishingSessionId.ToString(),
			*Rod->GetPresentationState().RodActorId.ToString(),
			bConstraintActive ? TEXT("true") : TEXT("false"),
			bCarrierConstraintActive ? TEXT("true") : TEXT("false"),
			CatActionName,
			Step.ConstraintErrorCentimeters,
			Step.RelativeConstraintSpeedCentimetersPerSecond,
			Step.NormalizedTension,
			Step.FishConstraintCorrectionCentimeters,
			Step.CarrierConstraintCorrectionCentimeters,
			Step.CarrierPullAccelerationCentimetersPerSecondSquared,
			Step.CarrierTargetPullSpeedCentimetersPerSecond,
			Step.CarrierAwaySpeedMultiplier,
			Step.RodLeverageMultiplier,
			Config.RodPhysicsLengthCentimeters,
			RotationResistance.MaximumFishTorqueStrengthMeters,
			RotationResistance.FishResistingTorqueStrengthMeters,
			RotationResistance.CatTorqueCapacityStrengthMeters,
			Step.CombinedCatStrength,
			Step.CatDriveAccelerationCentimetersPerSecondSquared,
			Step.FishDriveAccelerationCentimetersPerSecondSquared,
			Step.NetFishPullAccelerationCentimetersPerSecondSquared,
			Step.FishForceDominance,
			Step.ActiveHelperCount,
			Step.CatStaminaDrain,
			Step.bStalemate ? TEXT("true") : TEXT("false"),
			*Motion.FishWorldPosition.ToCompactString(),
			*RodTip.ToCompactString(),
			*GetNameSafe(Rod->GetHolderPawnFromAuthority()),
			static_cast<int32>(World->GetNetMode()));
		NextConstraintDiagnosticWorldSeconds = WorldSeconds + 1.0;
		bLastConstraintDiagnosticActive = bConstraintActive;
	}
	// 把猫的体力消耗（正值）转成负的 Delta 施加到 ASC；只有真正非零变化才需要写一次，且写入失败也视为致命错误。
	const double PrimaryDrainShare = Config.PrimaryOperatorCatStrength
		/ FMath::Max(Config.GetCombinedCatStrength(), UE_DOUBLE_SMALL_NUMBER);
	const double PrimaryStaminaDrain = Step.CatStaminaDrain < 0.0
		? Step.CatStaminaDrain
		: FMath::Min(State.CatStamina, Step.GetPrimaryCatStaminaDrain()
			+ Step.GetSharedCatStaminaDrain() * PrimaryDrainShare);
	if (State.bOperatorPresent && !FMath::IsNearlyZero(PrimaryStaminaDrain)
		&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-PrimaryStaminaDrain)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("AbilityStaminaWrite"));
		return;
	}
	if (!ApplyHelperStaminaChanges(Step.GetSharedCatStaminaDrain()))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("HelperAbilityStaminaWrite"));
		return;
	}
	// Runner 只记录本场累计磨损；下面 Session 按差额立即写回绑定鱼竿实例，结束不退款。
	// 把新的运动意图/线长/位置应用到 Encounter Actor 失败时视为不可恢复，终止本次搏斗。
	if (!Encounter->ApplyFightStepFromAuthority(State.MotionIntent,
		Step.LineLengthCentimeters, Motion.FishWorldPosition, static_cast<float>(Config.FixedStepSeconds),
		static_cast<float>(Step.FishLineAlignment), static_cast<float>(Step.NormalizedLineLoad),
		static_cast<float>(Step.IntendedSwimSpeedCentimetersPerSecond), Step.bStrongConfrontation,
		bFishBeached, GroundSurfaceNormal))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("EncounterFightStepWrite"));
		return;
	}
	// 所有副作用都成功落地后，才把本步结果正式写回 Runner 自己持有的状态，作为下一步 Step 的输入基准。
	State.CatStamina = FMath::Clamp(State.CatStamina - PrimaryStaminaDrain, 0.0, Config.CatStaminaMaximum);
	State.FishStamina = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	if (bLogWorkSample)
	{
		for (const auto& Pair : Participants)
		{
			const FCatFightParticipantRuntime& Participant = Pair.Value;
			const UCatAbilitySystemComponent* ParticipantASC = Participant.AbilitySystem.Get();
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_cat_stamina_applied SessionId=%s RodActorId=%s PlayerState=%s PlayerId=%d CatActor=%s "
					"Primary=%s PullHeld=%s ActiveStrength=%.3f GroupSharedDrain=%.4f PrimaryOnlyDrain=%.4f "
					"StaminaAfter=%.4f Result=Applied World=%s NetMode=%d Authority=true LocalRole=%d"),
				*SessionActor->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(Participant.PlayerState.Get()),
				Participant.PlayerState.IsValid() ? Participant.PlayerState->GetPlayerId() : INDEX_NONE,
				*GetNameSafe(Participant.Character.Get()),
				Participant.bPrimary ? TEXT("true") : TEXT("false"), Participant.bPullHeld ? TEXT("true") : TEXT("false"),
				Participant.ActiveFishingStrength, Step.GetSharedCatStaminaDrain(),
				Participant.bPrimary ? Step.GetPrimaryCatStaminaDrain() : 0.0,
				ParticipantASC ? static_cast<double>(ParticipantASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())) : 0.0,
				*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(SessionActor->GetLocalRole()));
		}
	}
	State.LineLengthCentimeters = Step.LineLengthCentimeters;
	State.AbsoluteRodWear = Step.AbsoluteRodWear;
	State.StrongConfrontationBuildUpSeconds = Step.StrongConfrontationBuildUpSeconds;
	State.FishWorldPosition = Encounter->GetActorLocation(); // 再次以 Actor 实际落点为准，覆盖掉建议值可能的浮点误差
	// 把本步结果（含鱼竿磨损）、剩余体力与运动意图上报给 Session，由它决定是否切换阶段/终止会话。
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent);
}
