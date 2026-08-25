#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/CatFishingViewTypes.h"
#include "UI/CatFishingViewBridge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingViewStateProjectionTest,
	"Catfishing.Unit.UI.FishingViewState.ProjectsReplicatedFactsWithoutGameplayObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先构造一份 FishingSession 公开快照并投影成 UI DTO，再确认展示字段完整保留；最后创建 Bridge 并验证空会话会被拒绝。
// 该用例只覆盖只读 View 边界，不生成真实 Session Actor，也不把 UI 投影测试冒充 Fishing 命令或复制生命周期验收。
bool FCatFishingViewStateProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingSessionSnapshot Snapshot;
	Snapshot.FishingSessionId = FGuid::NewGuid();
	Snapshot.Phase = ECatFishingPhase::NearShore;
	Snapshot.FishDefinitionId = TEXT("Carp");
	Snapshot.NormalizedFishStamina = 0.25;
	Snapshot.bReeling = true;
	const FCatFishingViewState View = FCatFishingViewState::FromSnapshot(Snapshot);
	TestEqual(TEXT("session id projects"), View.FishingSessionId, Snapshot.FishingSessionId);
	TestEqual(TEXT("phase projects"), View.Phase, ECatFishingPhase::NearShore);
	TestEqual(TEXT("fish id projects"), View.FishDefinitionId, FName(TEXT("Carp")));
	TestEqual(TEXT("normalized stamina projects"), View.NormalizedFishStamina, 0.25);
	TestTrue(TEXT("reeling projects"), View.bReeling);
	UCatFishingViewBridge* Bridge = NewObject<UCatFishingViewBridge>();
	TestNotNull(TEXT("read-only fishing view bridge exists without a widget asset"), Bridge);
	TestFalse(TEXT("bridge rejects a missing session"), Bridge->BindSession(nullptr));
	return !HasAnyErrors();
}

#endif
