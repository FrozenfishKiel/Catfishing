#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "StateTree.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSettingsRuntimeReadinessTest,
	"Catfishing.Unit.Fishing.Settings.RuntimeRequiresStateTreeAndPositiveWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishWeightVisualScaleTest,
	"Catfishing.Unit.Fishing.Settings.FishWeightUsesCubeRootUniformVisualScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRodOperatorLayoutSettingsTest,
	"Catfishing.Unit.Fishing.Settings.RodOperatorLayoutIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodOperatorLayoutSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishingSettings* Settings = NewObject<UCatFishingSettings>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient fishing settings"), Settings)) return false;
	Settings->MaximumRodOperatorSlots = 2;
	Settings->RodOperatorSlotSpacingCentimeters = 140.0;
	int32 Slots = 99;
	double Spacing = 99.0;
	TestTrue(TEXT("two-slot layout is accepted"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	TestEqual(TEXT("layout returns two slots"), Slots, 2);
	TestEqual(TEXT("layout returns configured spacing"), Spacing, 140.0);
	Settings->MaximumRodOperatorSlots = 9;
	TestFalse(TEXT("unbounded replicated slot count is rejected"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	TestEqual(TEXT("rejected layout clears slot count"), Slots, 0);
	TestEqual(TEXT("rejected layout clears spacing"), Spacing, 0.0);
	Settings->MaximumRodOperatorSlots = 2;
	Settings->RodOperatorSlotSpacingCentimeters = -1.0;
	TestFalse(TEXT("negative spacing is rejected"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	return !HasAnyErrors();
}

bool FCatFishWeightVisualScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishingPresentationSettings* Settings = NewObject<UCatFishingPresentationSettings>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient presentation settings"), Settings))
	{
		return false;
	}
	Settings->FishMeshReferenceWeightKilograms = 1.0;
	Settings->FishMeshMinimumUniformScale = 0.5;
	Settings->FishMeshMaximumUniformScale = 2.0;
	TestEqual(TEXT("reference weight keeps unit scale"), Settings->ComputeFishUniformVisualScale(1.0), 1.0);
	TestEqual(TEXT("eight times weight doubles linear size"), Settings->ComputeFishUniformVisualScale(8.0), 2.0);
	TestEqual(TEXT("one eighth weight halves linear size"), Settings->ComputeFishUniformVisualScale(0.125), 0.5);
	TestEqual(TEXT("large values clamp to configured maximum"), Settings->ComputeFishUniformVisualScale(64.0), 2.0);
	TestEqual(TEXT("invalid weight safely falls back to unit scale"), Settings->ComputeFishUniformVisualScale(0.0), 1.0);
	return !HasAnyErrors();
}

// 测试流程：构造一份只在内存存在的 Fishing Settings，逐步补齐 StateTree、真咬窗口、近岸验证、抢抄距离和终态复制窗口；读取接口必须随 runtime readiness 同步 fail-closed。
bool FCatFishingSettingsRuntimeReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishingSettings* Settings = NewObject<UCatFishingSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Fishing Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	// UDeveloperSettings 会从 DefaultGame.ini 装载项目配置；这里显式清空 runtime 必填项，
	// 才能稳定验证 fail-closed 行为，而不受当前项目默认配置影响。
	Settings->bEnableFishingRuntime = false;
	Settings->FishingSessionStateTree.Reset();
	Settings->FishBehaviorStateTree.Reset();
	Settings->TrueBiteWindowSeconds = 0.0;
	Settings->bEnableNearShoreValidation = false;
	Settings->ScoopReachCentimeters = 0.0;
	Settings->TerminalReplicationWindowSeconds = 0.0;

	double ScoopReach = 99.0;
	double ScoopCooldown = 99.0;
	double BiteWarning = 99.0;
	double TerminalWindow = 99.0;
	TestFalse(TEXT("空 Fishing runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("空配置抢抄距离读取失败"), Settings->TryGetScoopReach(ScoopReach));
	TestEqual(TEXT("失败时抢抄距离清零"), ScoopReach, 0.0);
	TestTrue(TEXT("默认抄网冷却可读取"), Settings->TryGetScoopCooldown(ScoopCooldown));
	TestEqual(TEXT("默认抄网冷却为三秒"), ScoopCooldown, 3.0);
	Settings->ScoopCooldownSeconds = 0.0;
	TestFalse(TEXT("非正抄网冷却配置被拒绝"), Settings->TryGetScoopCooldown(ScoopCooldown));
	TestEqual(TEXT("冷却读取失败时输出清零"), ScoopCooldown, 0.0);
	Settings->ScoopCooldownSeconds = 3.0;
	TestTrue(TEXT("默认真咬预警可读取"), Settings->TryGetBiteWarning(BiteWarning));
	TestEqual(TEXT("默认真咬预警为1.5秒"), BiteWarning, 1.5);
	Settings->BiteWarningSeconds = 0.0;
	TestFalse(TEXT("非正真咬预警配置被拒绝"), Settings->TryGetBiteWarning(BiteWarning));
	TestEqual(TEXT("预警读取失败时输出清零"), BiteWarning, 0.0);
	Settings->BiteWarningSeconds = 3.0;
	Settings->MaximumBiteDelaySeconds = 7.0;
	TestFalse(TEXT("总时间上限不足以容纳慢浮下限和完整预警时拒绝"),
		Settings->TryGetBiteWarning(BiteWarning));
	Settings->MaximumBiteDelaySeconds = 8.0;
	TestTrue(TEXT("总时间上限恰好容纳五秒慢浮与三秒预警"), Settings->TryGetBiteWarning(BiteWarning));
	Settings->MaximumBiteDelaySeconds = 15.0;
	TestTrue(TEXT("恢复合法咬钩延迟后预警可读取"), Settings->TryGetBiteWarning(BiteWarning));
	TestFalse(TEXT("空配置终态复制窗口读取失败"), Settings->TryGetTerminalReplicationWindow(TerminalWindow));
	TestEqual(TEXT("失败时终态复制窗口清零"), TerminalWindow, 0.0);

	Settings->bEnableFishingRuntime = true;
	Settings->FishingSessionStateTree = NewObject<UStateTree>(GetTransientPackage());
	Settings->FishBehaviorStateTree = NewObject<UStateTree>(GetTransientPackage());
	Settings->TrueBiteWindowSeconds = 1.25;
	Settings->bEnableNearShoreValidation = true;
	Settings->ScoopReachCentimeters = 250.0;
	TestFalse(TEXT("缺少终态复制窗口时仍不可运行"), Settings->IsRuntimeReady());
	Settings->TerminalReplicationWindowSeconds = 5.0;
	TestTrue(TEXT("完整 Fishing 配置可运行"), Settings->IsRuntimeReady());
	TestTrue(TEXT("完整配置可读取抢抄距离"), Settings->TryGetScoopReach(ScoopReach));
	TestEqual(TEXT("抢抄距离保持配置值"), ScoopReach, 250.0);
	TestTrue(TEXT("完整配置可读取终态复制窗口"), Settings->TryGetTerminalReplicationWindow(TerminalWindow));
	TestEqual(TEXT("终态复制窗口保持配置值"), TerminalWindow, 5.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
