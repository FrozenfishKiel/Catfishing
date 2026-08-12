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

// 阶段 Task 进入流程：从 StateTree Context Owner 取得 Session，读取资产 Phase 并调用唯一写口；类型或 gate 失败都返回 Failed。
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
	return Session->EnterPhaseFromStateTree(InstanceData.Phase, InstanceData.bHasAuthoritativeNearShoreTarget,
		InstanceData.AuthoritativeNearShoreTarget).bApplied
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

// 搏斗交换 Task 构造流程：关闭 Tick 与 Tick/Exit 属性复制；每次 State 进入只消费一次双方短周期资源。
FCatFishingFightExchangeTask::FCatFishingFightExchangeTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 搏斗交换 Task 进入流程：从 Context Owner 取得 Session，读取资产显式消耗并调用唯一资源写口；力量/人数/体力不足时返回 Failed。
EStateTreeRunStatus FCatFishingFightExchangeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	if (!Session)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	return Session->ResolveFightExchangeFromStateTree(InstanceData.FishStaminaCost, InstanceData.ParticipantStaminaCost).bCommitted
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
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
	return Session->CommitFailureBudgetFromStateTree(InstanceData.Penalty).Command.bCommitted
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

// 重试耗尽 Task 构造流程：关闭 Tick 与属性复制；该终态不等待输入，也不保存第二份重试计数。
FCatFishingResolveRetryExhaustedTask::FCatFishingResolveRetryExhaustedTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 重试耗尽 Task 进入流程：从 Context Owner 取得 Session 并提交唯一已裁逃鱼资格；成功表示剪影 Grant 已建立且会话已终止。
EStateTreeRunStatus FCatFishingResolveRetryExhaustedTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishingSession* Session = Cast<ACatFishingSession>(Context.GetOwner());
	return Session && Session->ResolveRetryExhaustedEscapeFromStateTree().bCommitted
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
