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
	// 近岸目标不再是资产参数：实例数据里只剩 Phase 一个属性，资产没有任何能填世界位置的入口。
	const UScriptStruct* EnterPhaseStruct = FCatFishingEnterPhaseTaskInstanceData::StaticStruct();
	TestTrue(TEXT("阶段入口实例数据只暴露 Phase 一个属性"),
		EnterPhaseStruct->PropertyLink && !EnterPhaseStruct->PropertyLink->PropertyLinkNext);

	const FCatFishingWaitTask WaitTask;
	TestTrue(TEXT("等待节点暴露空参数结构"),
		WaitTask.GetInstanceDataType() == FCatFishingWaitTaskInstanceData::StaticStruct());

	const FCatFishingFightExchangeTask FightExchangeTask;
	TestTrue(TEXT("搏斗推进节点暴露空参数结构"),
		FightExchangeTask.GetInstanceDataType() == FCatFishingFightExchangeTaskInstanceData::StaticStruct());
	// 消耗公式全部来自飞书规则、写死在会话的纯模型里：实例数据不得再暴露任何可调消耗参数，否则资产就能绕过产品数值。
	TestNull(TEXT("搏斗推进节点实例数据没有任何属性"), FCatFishingFightExchangeTaskInstanceData::StaticStruct()->PropertyLink);

	const FCatFishingFailureBudgetTask FailureBudgetTask;
	TestTrue(TEXT("失败预算节点暴露互斥惩罚参数结构"),
		FailureBudgetTask.GetInstanceDataType() == FCatFishingFailureBudgetTaskInstanceData::StaticStruct());
	const FCatFishingFailureBudgetTaskInstanceData FailureData;
	TestEqual(TEXT("默认失败惩罚为 None"), FailureData.Penalty, ECatFishingFailurePenalty::None);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
