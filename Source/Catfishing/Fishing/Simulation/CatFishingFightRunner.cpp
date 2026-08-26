#include "Fishing/Simulation/CatFishingFightRunner.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
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
		|| !Init.RodActor.IsValid() || !Init.AbilitySystem.IsValid() || !Init.Equipment.IsValid()
		|| !Init.FishingSessionId.IsValid() || !Init.WaterRegion.IsValid() || !Init.FrozenWaterBounds.IsValid
		|| !Init.Config.IsValid() || Init.RandomSeed == 0
		|| Init.CalmDurationRangeSeconds.X <= 0.0 || Init.CalmDurationRangeSeconds.Y < Init.CalmDurationRangeSeconds.X
		|| Init.StruggleDurationRangeSeconds.X <= 0.0 || Init.StruggleDurationRangeSeconds.Y < Init.StruggleDurationRangeSeconds.X
		|| !FMath::IsFinite(Init.LowStaminaRestThreshold) || Init.LowStaminaRestThreshold < 0.0 || Init.LowStaminaRestThreshold > 1.0
		|| !FMath::IsFinite(Init.LowStaminaRestMultiplier) || Init.LowStaminaRestMultiplier < 1.0)
	{
		return false;
	}
	// 逐一拷贝依赖引用与配置到成员变量，Runner 从此持有自己的一份快照，不再依赖调用方保留 Init 结构体。
	Session = Init.Session;
	FishActor = Init.FishActor;
	RodActor = Init.RodActor;
	AbilitySystem = Init.AbilitySystem;
	Equipment = Init.Equipment;
	FishingSessionId = Init.FishingSessionId;
	WaterRegion = Init.WaterRegion;
	FrozenWaterBounds = Init.FrozenWaterBounds;
	Config = Init.Config;
	State = Init.InitialState;
	CalmDurationRangeSeconds = Init.CalmDurationRangeSeconds;
	StruggleDurationRangeSeconds = Init.StruggleDurationRangeSeconds;
	LowStaminaRestThreshold = Init.LowStaminaRestThreshold;
	LowStaminaRestMultiplier = Init.LowStaminaRestMultiplier;
	// 记录鱼的初始体力（至少为一个极小正数，避免后面用它做分母时除零），用于低体力判定的比例基准。
	InitialFishStamina = FMath::Max(State.FishStamina, UE_DOUBLE_SMALL_NUMBER);
	// 用服务器分配的种子初始化随机流，保证同一次搏斗在权威端是确定性可复算的。
	Random.Initialize(static_cast<int32>(Init.RandomSeed));
	// 规格 4.6：上钩瞬间初始状态 = 向外游（发力）。
	State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	// 按发力时长区间随机抽一个本轮持续时间，倒计时用它触发下一次意图切换。
	MotionSecondsRemaining = Random.FRandRange(StruggleDurationRangeSeconds.X, StruggleDurationRangeSeconds.Y);
	State.CatAction = ECatFightCatAction::None; // 初始时玩家既未拖线也未放线
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
	// 按配置的定步长注册重复定时器，之后每隔 FixedStepSeconds 调用一次 HandleFixedStep 推进一步搏斗模拟。
	World->GetTimerManager().SetTimer(FixedStepTimer, this, &ThisClass::HandleFixedStep,
		static_cast<float>(Config.FixedStepSeconds), true);
	return true;
}

void UCatFishingFightRunner::Stop()
{
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
	bSlackHeld = bInSlacking; // 记录右键（放线）当前的按住/松开状态
	RefreshCatAction();
	return true;
}

// 定时交替（规格 4.6 临时规则）：发力 ⇄ 休息；鱼体力低于阈值后休息期变长。
void UCatFishingFightRunner::SelectNextMotionIntent()
{
	if (State.MotionIntent == ECatFishMotionIntent::StrugglingOutward)
	{
		// 当前是发力（外游）阶段，倒计时归零后切换到休息（向内游/平静）阶段。
		State.MotionIntent = ECatFishMotionIntent::CalmOrInward;
		// 体力比例低于阈值时，休息时长要乘以放大倍率，让体力低的鱼歇更久（临时平衡口径）。
		const double RestScale = State.FishStamina / InitialFishStamina < LowStaminaRestThreshold
			? LowStaminaRestMultiplier : 1.0;
		MotionSecondsRemaining = Random.FRandRange(CalmDurationRangeSeconds.X, CalmDurationRangeSeconds.Y) * RestScale;
	}
	else
	{
		// 当前是休息阶段，倒计时归零后切回发力（外游）阶段，按发力时长区间重新抽一个持续时间。
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		MotionSecondsRemaining = Random.FRandRange(StruggleDurationRangeSeconds.X, StruggleDurationRangeSeconds.Y);
	}
}

// 贴岸吸附：从当前位置沿指向竿尖的方向分步逼近，保留最后一个仍在水内的点；到达近岸阈值即停。
// 竿尖本身在岸上，直接搬到 D=0 会被水域校验拒绝，因此只能逼近到岸边水面。岸上态见规格 5.3（TODO）。
FVector UCatFishingFightRunner::SnapFishTowardShore(const FVector& FishPosition, const FVector& RodTip,
	const UCatWaterQuerySubsystem& Water) const
{
	const FVector ToRod = RodTip - FishPosition; // 从鱼指向竿尖（岸边方向）的向量
	const double TotalDistance = ToRod.Size();
	if (TotalDistance <= UE_DOUBLE_KINDA_SMALL_NUMBER) return FishPosition; // 鱼和竿尖几乎重合，无需再挪
	const FVector Direction = ToRod / TotalDistance; // 归一化的贴岸方向
	constexpr double StepCentimeters = 25.0; // 每次试探前进的步长，越小越精确但查询次数越多
	FVector Best = FishPosition; // 记录最后一个仍确认在水域内的合法点，作为兜底
	for (double Travelled = StepCentimeters; Travelled <= TotalDistance; Travelled += StepCentimeters)
	{
		const FVector Candidate = FishPosition + Direction * Travelled; // 沿贴岸方向再往前挪一步的候选点
		const FCatWaterSpatialResult Spatial = Water.QueryShoreRelation(Candidate, WaterRegion);
		// 一旦候选点查询失败或已经不在水域内（例如踩到岸上），就停止前进，保留上一个合法点。
		if (!Spatial.bSucceeded || Spatial.Containment != ECatWaterContainment::Inside) break;
		Best = Spatial.WaterSurfaceWorldPoint; // 候选点合法，更新为当前最佳（最贴近岸）的水面点
		if (Spatial.SignedDistanceToShoreCm <= Config.NearShoreLineLengthCentimeters) break; // 已经进入近岸阈值范围，不必再逼近
	}
	return Best;
}

void UCatFishingFightRunner::HandleFixedStep()
{
	// 逐个解析本步需要用到的弱引用，任何一个失效都说明搏斗依赖已经被销毁/离线，走失败收尾。
	ACatFishingSession* SessionActor = Session.Get();
	ACatFishEncounterActor* Encounter = FishActor.Get();
	ACatFishingRodActor* Rod = RodActor.Get();
	UCatAbilitySystemComponent* ASC = AbilitySystem.Get();
	UCatEquipmentComponent* EquipmentComponent = Equipment.Get();
	UCatWaterQuerySubsystem* Water = SessionActor && SessionActor->GetWorld()
		? SessionActor->GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!bRunning || !SessionActor || !SessionActor->HasAuthority() || !Encounter || !Rod || !ASC
		|| !EquipmentComponent || !Water)
	{
		Stop();
		if (SessionActor) SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}

	// 意图倒计时推进；归零则切换发力/休息（SelectNextMotionIntent 会重新抽下一段随机时长）。
	MotionSecondsRemaining -= Config.FixedStepSeconds;
	if (MotionSecondsRemaining <= 0.0) SelectNextMotionIntent();
	// 每步开始都以 Encounter Actor 的实际 Transform 为准同步鱼的位置，避免和上一步的建议位置产生累积误差。
	State.FishWorldPosition = Encounter->GetActorLocation();
	const FVector RodTip = Rod->GetRodTipWorldTransform().GetLocation();
	FVector Outward = State.FishWorldPosition - RodTip; // 竿尖指向鱼的方向即为“外游”方向
	if (Outward.IsNearlyZero()) Outward = FVector::ForwardVector; // 鱼和竿尖重合时退化用一个固定方向，避免零向量传入 Step
	// 调用无状态的纯数学 Step，得到体力/线长/磨损增量与建议新位置。
	FCatFightStepResult Step = FCatFishingFightSimulator::Step(Config, State, RodTip, Outward);
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
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	Motion.FishWorldPosition = Exact.WaterSurfaceWorldPoint; // 采用水域校正后的精确水面点作为鱼的最终位置

	// 翻肚 / 碾压：规格要求 D 归零，此处贴岸吸附到近岸水面（岸上态 TODO）。
	if (Step.Outcome == ECatFightStepOutcome::FishExhausted || Step.Outcome == ECatFightStepOutcome::Overpowered)
	{
		Motion.FishWorldPosition = SnapFishTowardShore(Motion.FishWorldPosition, RodTip, *Water);
	}
	// 近岸判定用真实岸距（规格 5.1 的可抄距离口径），而非鱼到竿尖的线长。
	else if (Step.Outcome == ECatFightStepOutcome::None)
	{
		const FCatWaterSpatialResult Shore = Water->QueryShoreRelation(Motion.FishWorldPosition, WaterRegion);
		// 鱼必须真的在水域内、岸距为正（还没上岸）且不超过近岸阈值，才判定进入 NearShore 阶段。
		if (Shore.bSucceeded && Shore.Containment == ECatWaterContainment::Inside
			&& Shore.SignedDistanceToShoreCm > 0.0
			&& Shore.SignedDistanceToShoreCm <= Config.NearShoreLineLengthCentimeters)
		{
			Step.Outcome = ECatFightStepOutcome::NearShore;
		}
	}

	// 把猫的体力消耗（正值）转成负的 Delta 施加到 ASC；只有真正非零变化才需要写一次，且写入失败也视为致命错误。
	if (!FMath::IsNearlyZero(Step.CatStaminaDrain)
		&& !ASC->ApplyFishingStaminaDelta(static_cast<float>(-Step.CatStaminaDrain)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	// 把本步累计的竿磨损写回装备组件（带自增 WearSequence 防止乱序应用旧值）。
	const FCatFishingUseOperationResult Wear = EquipmentComponent->SetAccumulatedFishingRodWear(
		FishingSessionId, ++WearSequence, Step.AbsoluteRodWear);
	// 磨损写入失败，或把新的运动意图/线长/位置应用到 Encounter Actor 失败，都视为不可恢复，终止本次搏斗。
	if (!Wear.bApplied || !Encounter->ApplyFightStepFromAuthority(State.MotionIntent,
		Step.LineLengthCentimeters, Motion.FishWorldPosition, static_cast<float>(Config.FixedStepSeconds)))
	{
		Stop();
		SessionActor->HandleFightRunnerFailureFromAuthority();
		return;
	}
	// 所有副作用都成功落地后，才把本步结果正式写回 Runner 自己持有的状态，作为下一步 Step 的输入基准。
	State.CatStamina = FMath::Clamp(State.CatStamina - Step.CatStaminaDrain, 0.0, Config.CatStaminaMaximum);
	State.FishStamina = FMath::Max(0.0, State.FishStamina - Step.FishStaminaDrain);
	State.LineLengthCentimeters = Step.LineLengthCentimeters;
	State.AbsoluteRodWear = Step.AbsoluteRodWear;
	State.FishWorldPosition = Encounter->GetActorLocation(); // 再次以 Actor 实际落点为准，覆盖掉建议值可能的浮点误差
	// 把本步结果、剩余体力、运动意图和剩余竿耐久上报给 Session，由它决定是否需要切换阶段/终止会话。
	SessionActor->HandleFightRunnerStepFromAuthority(Step, State.FishStamina, State.MotionIntent,
		Config.RodDurability - Step.AbsoluteRodWear);
}
