#include "Fishing/CatFishingStateTreeNodes.h"

#include "Fishing/CatFishingSession.h"
#include "StateTreeExecutionContext.h"

// 阶段 Task 构造流程：禁用 Tick 和 Tick/Exit 属性复制；每次 State 进入只调用一次 Session。
FCatFishingEnterPhaseTask::FCatFishingEnterPhaseTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 阶段 Task 进入流程：从 StateTree Context Owner 取得 Session，读取资产 Phase 并调用唯一写口（NearShore 的目标由
// Session 自己算，这里不传任何位置）；类型或 gate 失败都返回 Failed。
EStateTreeRunStatus FCatFishingEnterPhaseTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	if (!Session)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	return Session->EnterPhaseFromStateTree(InstanceData.Phase).bApplied
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

// 等待 Task 构造流程：关闭 Tick 和属性复制；响应窗口等计时必须由专用有界 Task/资产显式提供。
FCatFishingWaitTask::FCatFishingWaitTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 等待 Task 进入流程：直接保持 Running；本节点不轮询输入、容器或鱼体力。
EStateTreeRunStatus FCatFishingWaitTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Context;
	(void)Transition;
	return EStateTreeRunStatus::Running;
}

// 咬钩间隔等待 Task 构造流程：打开 Tick（靠它累计时间），关闭 Tick/Exit 属性复制。
FCatFishingBiteIntervalWaitTask::FCatFishingBiteIntervalWaitTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 咬钩间隔等待进入流程：定位 Session 并读取它冻结的间隔；非正或非有限值直接 Failed（fail-closed，不用任何默认秒数顶
// 上），合法则清零累计并保持 Running。
EStateTreeRunStatus FCatFishingBiteIntervalWaitTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	const ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	const double Interval = Session ? Session->GetBiteIntervalSeconds() : 0.0;
	if (!Session || !FMath::IsFinite(Interval) || Interval <= 0.0)
	{
		return EStateTreeRunStatus::Failed;
	}
	Context.GetInstanceData(*this).ElapsedSeconds = 0.0;
	return EStateTreeRunStatus::Running;
}

// 咬钩间隔等待 Tick 流程：累加流逝秒数，达到会话间隔返回 Succeeded；Session 丢失返回 Failed。
EStateTreeRunStatus FCatFishingBiteIntervalWaitTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	if (!Session)
	{
		return EStateTreeRunStatus::Failed;
	}
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedSeconds += DeltaTime;
	return InstanceData.ElapsedSeconds >= Session->GetBiteIntervalSeconds()
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

// 搏斗推进 Task 构造流程：打开 Tick（逐帧推进搏斗），关闭 Tick/Exit 属性复制。
FCatFishingFightExchangeTask::FCatFishingFightExchangeTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 搏斗推进 Task 进入流程：Owner 不是 Session 直接 Failed；否则保持 Running，等 Tick 推进。不在这里推进第一帧，让每帧的 DeltaTime 都是真实流逝时间。
EStateTreeRunStatus FCatFishingFightExchangeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	return Cast<ACatFishingSession>(Context.GetOwner()) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

// 搏斗推进 Task Tick 流程：把 DeltaTime 交给会话；会话拒绝（不在 HookedFight、钓手身体没了）或打出断竿/拖下水返回 Failed，
// 打出碾压/翻肚/遛到岸边返回 Succeeded（资产的 OnStateSucceeded 边进 NearShore），其余 Running。
EStateTreeRunStatus FCatFishingFightExchangeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	if (!Session)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FCatFishingFightStepResult Result = Session->ApplyFightExchangeFromStateTree(DeltaTime);
	if (Result.Error != ECatDomainCommandError::None)
	{
		return EStateTreeRunStatus::Failed;
	}
	switch (Result.Outcome)
	{
	case ECatFishingFightOutcome::None:
		return EStateTreeRunStatus::Running;
	case ECatFishingFightOutcome::RodBroken:
	case ECatFishingFightOutcome::CatDraggedIn:
		return EStateTreeRunStatus::Failed;
	case ECatFishingFightOutcome::Overpowered:
	case ECatFishingFightOutcome::FishExhausted:
	case ECatFishingFightOutcome::ReeledToShore:
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Failed;
}

// 失败预算 Task 构造流程：关闭 Tick 与属性复制；同一状态不会轮询或重复执行惩罚。
FCatFishingFailureBudgetTask::FCatFishingFailureBudgetTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 失败预算 Task 进入流程：定位 Session 并提交资产选择的唯一惩罚；Equipment/策略拒绝时返回 Failed，资产可转向无惩罚终止而非 C++ 备用边。
EStateTreeRunStatus FCatFishingFailureBudgetTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	if (!Session)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	return Session->ApplyFailureBudgetFromStateTree(InstanceData.Penalty).Command.bCommitted
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
