#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Run/CatRunStateTreeNodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunStateTreeNodesDefaultsTest,
	"Catfishing.Unit.Run.StateTreeNodes.DefaultParametersDoNotStartHiddenRunFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：核对 Run StateTree 节点对资产暴露的参数结构和默认值；真实 StateTree 资产事件边仍留给运行验收。
bool FCatRunStateTreeNodesDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatRunEnterPhaseTask EnterPhaseTask;
	TestTrue(TEXT("Run 阶段入口只暴露 Phase/Reason 参数结构"),
		EnterPhaseTask.GetInstanceDataType() == FCatRunEnterPhaseTaskInstanceData::StaticStruct());
	const FCatRunEnterPhaseTaskInstanceData EnterData;
	TestEqual(TEXT("Run 阶段默认 NotStarted"), EnterData.Phase, ECatRunPhase::NotStarted);
	TestEqual(TEXT("Run 阶段原因默认 None"), EnterData.Reason, ECatRunTransitionReason::None);

	const FCatRunWaitForEventTask WaitTask;
	TestTrue(TEXT("Run 等待节点暴露空参数结构"),
		WaitTask.GetInstanceDataType() == FCatRunWaitForEventTaskInstanceData::StaticStruct());

	const FCatRunResultReasonCondition ResultCondition;
	TestTrue(TEXT("Run Result 条件只暴露 ExpectedReason 参数结构"),
		ResultCondition.GetInstanceDataType() == FCatRunResultReasonConditionInstanceData::StaticStruct());
	const FCatRunResultReasonConditionInstanceData ConditionData;
	TestEqual(TEXT("Result 条件默认不匹配任何产品原因"), ConditionData.ExpectedReason, ECatRunTransitionReason::None);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
