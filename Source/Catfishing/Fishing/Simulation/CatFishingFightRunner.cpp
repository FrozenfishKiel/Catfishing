#include "Fishing/Simulation/CatFishingFightRunner.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishFightMotionSolver.h"
#include "TimerManager.h"

bool UCatFishingFightRunner::InitializeFromAuthority(const FCatFishingFightRunnerInit& Init)
{
	ACatFishingSession* SessionActor = Init.Session.Get();
	// 一次性初始化守卫：已初始化过、Session 无效/非权威、任何依赖弱引用失效、配置非法都直接拒绝。
	if (bInitialized || !SessionActor || !SessionActor->HasAuthority() || !Init.FishActor.IsValid()
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid()
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
	// 连续按键属于玩家输入生命周期，不属于旧 Session；新 Runner 原子恢复服务器已确认的按住状态。
	LastInputSequence = Init.InitialInputSequence;
	bPullHeld = Init.bInitialPullHeld;
	bSlackHeld = Init.bInitialSlackHeld;
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
	bRunning = false;
}

void UCatFishingFightRunner::RefreshCatAction()
{
	// 拖优先于放：两个键都按住时按拖处理；都没按则视为不动。
	State.CatAction = bPullHeld ? ECatFightCatAction::Pull
		: bSlackHeld ? ECatFightCatAction::Slack : ECatFightCatAction::None;
}

bool UCatFishingFightRunner::SetReeling(const int64 InputSequence, const bool bInReeling)
{
	// 未初始化/未运行，或者这是一条比已处理过的更旧（乱序/重复）的输入，直接丢弃。
	if (!bInitialized || !bRunning || InputSequence <= LastInputSequence) return false;
	LastInputSequence = InputSequence; // 推进已处理的最大输入序号，防止旧包回放
	bPullHeld = bInReeling; // 记录左键（拖线）当前的按住/松开状态
	RefreshCatAction(); // 按最新的拖/放状态重新计算本步的猫动作
	return true;
}

bool UCatFishingFightRunner::SetSlacking(const int64 InputSequence, const bool bInSlacking)
{
	if (!bInitialized || !bRunning || InputSequence <= LastInputSequence) return false;
	LastInputSequence = InputSequence;
	bSlackHeld = bInSlacking; // 记录右键（松开线杯）当前的按住/松开状态
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
	UCatWaterQuerySubsystem* Water = SessionActor && SessionActor->GetWorld()
		? SessionActor->GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!bRunning || !SessionActor || !SessionActor->HasAuthority() || !Encounter || !Rod || !ASC
		|| !Water)
	{
		Stop();
		if (SessionActor) SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("DependencyResolution"));
		return;
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
	// 纯模拟器用游向与鱼线夹角计算有效力量，再得到体力/线长/磨损和建议新位置。
	FCatFightStepResult Step = FCatFishingFightSimulator::Step(Config, State, RodTip, DesiredFishDirection);
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
	}

	// 靠近岸线只是空间事实，不再终止搏斗。抄网随时用自己的服务器射线判定；
	// 只有 FishExhausted/Overpowered 才切入后续“侧翻并收向竿尖水面投影”阶段。

	// 把猫的体力消耗（正值）转成负的 Delta 施加到 ASC；只有真正非零变化才需要写一次，且写入失败也视为致命错误。
	if (!FMath::IsNearlyZero(Step.CatStaminaDrain)
		&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Step.CatStaminaDrain)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority(TEXT("AbilityStaminaWrite"));
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
	// 把本步结果、剩余体力、运动意图和本场剩余鱼线耐久上报给 Session，由它决定是否切换阶段/终止会话。
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent,
		Config.RodDurability - Step.AbsoluteRodWear);
}
