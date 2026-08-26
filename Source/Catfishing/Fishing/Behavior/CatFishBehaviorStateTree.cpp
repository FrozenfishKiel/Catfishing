#include "Fishing/Behavior/CatFishBehaviorStateTree.h"

#include "Fishing/Actors/CatFishEncounterActor.h"
#include "StateTreeExecutionContext.h"

FCatFishBehaviorStateTask::FCatFishBehaviorStateTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FCatFishBehaviorStateTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	ACatFishEncounterActor* Fish = Cast<ACatFishEncounterActor>(Context.GetOwner());
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.RemainingSeconds = 0.0;
	if (!Fish || !Fish->BeginBehaviorStateFromStateTree(
		InstanceData.MotionIntent, InstanceData.RemainingSeconds))
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCatFishBehaviorStateTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f
		|| !FMath::IsFinite(InstanceData.RemainingSeconds))
	{
		return EStateTreeRunStatus::Failed;
	}
	InstanceData.RemainingSeconds -= static_cast<double>(DeltaTime);
	return InstanceData.RemainingSeconds <= 0.0
		? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

UCatFishBehaviorStateTreeSchema::UCatFishBehaviorStateTreeSchema()
{
	ContextActorClass = ACatFishEncounterActor::StaticClass();
	ScheduledTickPolicy = EStateTreeComponentSchemaScheduledTickPolicy::Allowed;
}
