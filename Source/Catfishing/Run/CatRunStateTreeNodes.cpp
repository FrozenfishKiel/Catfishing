#include "Run/CatRunStateTreeNodes.h"

#include "CatGameplayTypes.h"
#include "StateTreeExecutionContext.h"

// EnterPhase Task 构造流程：禁用 Tick 与无意义的 Tick/Exit 属性复制，使每次 State 进入只执行一次 GameMode 提交。
FCatRunEnterPhaseTask::FCatRunEnterPhaseTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 阶段进入流程：从 UE 5.8 StateTree Context Owner 取得承载组件的 GameMode Actor，读取资产参数并调用唯一写口；宿主缺失或 Result 拒绝时让节点失败，不在节点内选择回退状态。
EStateTreeRunStatus FCatRunEnterPhaseTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatfishingGameModeBase* GameMode = Cast<ACatfishingGameModeBase>(Context.GetOwner());
	if (!GameMode)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FCatRunTransitionResult Result = GameMode->EnterRunPhaseFromStateTree(InstanceData.Phase, InstanceData.Reason);
	return Result.bApplied ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

// 等待 Task 构造流程：关闭常规 Tick 和属性复制；事件到达后由 StateTree 自己调度 Transition，本节点不轮询 GameMode 或复制一份 pending 标志。
FCatRunWaitForEventTask::FCatRunWaitForEventTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

// 事件等待流程：进入时直接保持 Running；无超时、无计时器，夜晚因而不会被该节点引入隐藏倒计时，退出完全由资产事件边负责。
EStateTreeRunStatus FCatRunWaitForEventTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	(void)Context;
	(void)Transition;
	return EStateTreeRunStatus::Running;
}

// Result 条件流程：读取资产配置的 ExpectedReason，再向 Context Owner 的 GameMode 查询唯一最新结构化 Result；类型不符时 fail-closed，不尝试从 Phase 推断原因。
bool FCatRunResultReasonCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const ACatfishingGameModeBase* GameMode = Cast<ACatfishingGameModeBase>(Context.GetOwner());
	if (!GameMode)
	{
		return false;
	}
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	return GameMode->DoesLastRunFlowResultMatch(InstanceData.ExpectedReason);
}
