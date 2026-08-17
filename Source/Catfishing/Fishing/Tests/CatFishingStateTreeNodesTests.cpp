#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/CatFishingStateTreeNodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingStateTreeNodesDefaultsTest,
	"Catfishing.Unit.Fishing.StateTreeNodes.DefaultParametersAreFailClosedAndExposeOnlyExpectedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只检查 Fishing StateTree 节点暴露给资产的实例数据类型和默认参数；不构造假执行上下文来冒充真实 StateTree 拓扑已跑通。
bool FCatFishingStateTreeNodesDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatFishingEnterPhaseTask EnterPhaseTask;
	TestTrue(TEXT("阶段入口节点只暴露阶段参数结构"),
		EnterPhaseTask.GetInstanceDataType() == FCatFishingEnterPhaseTaskInstanceData::StaticStruct());
	const FCatFishingEnterPhaseTaskInstanceData EnterPhaseData;
	TestEqual(TEXT("阶段入口默认保持 Created"), EnterPhaseData.Phase, ECatFishingPhase::Created);
	TestFalse(TEXT("NearShore 默认没有权威目标"), EnterPhaseData.bHasAuthoritativeNearShoreTarget);
	TestEqual(TEXT("NearShore 默认目标为零向量"), EnterPhaseData.AuthoritativeNearShoreTarget, FVector::ZeroVector);

	const FCatFishingWaitTask WaitTask;
	TestTrue(TEXT("等待节点暴露空参数结构"),
		WaitTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());

	const FCatFishingFightExchangeTask FightExchangeTask;
	TestTrue(TEXT("搏斗交换节点暴露消耗参数结构"),
		FightExchangeTask.GetInstanceDataType() == FCatFishingFightExchangeTaskInstanceData::StaticStruct());
	const FCatFishingFightExchangeTaskInstanceData FightData;
	TestEqual(TEXT("默认鱼体力消耗为 0，保持未裁 fail-closed"), FightData.FishStaminaCost, 0.0);
	TestEqual(TEXT("默认参与者体力消耗为 0，保持未裁 fail-closed"), FightData.ParticipantStaminaCost, 0.0);

	const FCatFishingFailureBudgetTask FailureBudgetTask;
	TestTrue(TEXT("失败预算节点暴露互斥惩罚参数结构"),
		FailureBudgetTask.GetInstanceDataType() == FCatFishingFailureBudgetTaskInstanceData::StaticStruct());
	const FCatFishingFailureBudgetTaskInstanceData FailureData;
	TestEqual(TEXT("默认失败惩罚为 None"), FailureData.Penalty, ECatFishingFailurePenalty::None);

	const FCatFishingResolveRetryExhaustedTask RetryExhaustedTask;
	TestTrue(TEXT("重试耗尽节点复用无参数结构"),
		RetryExhaustedTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	const FCatFishingScheduleWaitingProbeTask ScheduleProbeTask;
	const FCatFishingResolveTrueBiteSelectionTask ResolveSelectionTask;
	TestTrue(TEXT("waiting scheduler exposes no data overrides"),
		ScheduleProbeTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	TestTrue(TEXT("true-bite selection exposes no data overrides"),
		ResolveSelectionTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());
	TestEqual(TEXT("true-bite task data has no reflected fields"),
		FCatFishingWaitTaskInstanceData::StaticStruct()->PropertyLink, static_cast<FProperty*>(nullptr));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
