#include "Fishing/Behavior/CatFishBehaviorStateTree.h"

#include "Fishing/Actors/CatFishEncounterActor.h"
#include "StateTreeExecutionContext.h"

FCatFishBehaviorStateTask::FCatFishBehaviorStateTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FCatFishBehaviorStateTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishEncounterActor* Fish = Cast<ACatFishEncounterActor>(Context.GetOwner());
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!Fish || !Fish->BeginFishBehaviorFromStateTree(InstanceData.Behavior))
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

bool FCatFishBehaviorFeedbackCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const ACatFishEncounterActor* Fish = Cast<ACatFishEncounterActor>(Context.GetOwner());
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!Fish || !Fish->HasAuthority() || InstanceData.Condition == ECatFishBehaviorCondition::None) return false;
	const bool bResult = Fish->TestFishBehaviorConditionFromStateTree(InstanceData.Condition);
	return InstanceData.bInvert ? !bResult : bResult;
}

UCatFishBehaviorStateTreeSchema::UCatFishBehaviorStateTreeSchema()
{
	ContextActorClass = ACatFishEncounterActor::StaticClass();
	ScheduledTickPolicy = EStateTreeComponentSchemaScheduledTickPolicy::Denied;
}
