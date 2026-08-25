#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/CatFishingSettings.h"
#include "StateTree.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSettingsRuntimeReadinessTest,
	"Catfishing.Unit.Fishing.Settings.RuntimeRequiresStateTreeAndPositiveWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：构造一份只在内存存在的 Fishing Settings，先显式关闭运行态，再逐步补齐 StateTree、真咬窗口、近岸验证、抢抄距离和终态复制窗口；读取接口必须随 runtime readiness 同步 fail-closed。
bool FCatFishingSettingsRuntimeReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishingSettings* Settings = NewObject<UCatFishingSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Fishing Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	double ScoopReach = 99.0;
	double TerminalWindow = 99.0;
	Settings->bEnableFishingRuntime = false;
	Settings->FishingSessionStateTree = nullptr;
	Settings->TrueBiteWindowSeconds = 0.0;
	Settings->bEnableNearShoreValidation = false;
	Settings->ScoopReachCentimeters = 0.0;
	Settings->TerminalReplicationWindowSeconds = 0.0;
	TestFalse(TEXT("显式关闭的 Fishing runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("显式关闭时抢抄距离读取失败"), Settings->TryGetScoopReach(ScoopReach));
	TestEqual(TEXT("失败时抢抄距离清零"), ScoopReach, 0.0);
	TestFalse(TEXT("显式关闭时终态复制窗口读取失败"), Settings->TryGetTerminalReplicationWindow(TerminalWindow));
	TestEqual(TEXT("失败时终态复制窗口清零"), TerminalWindow, 0.0);

	Settings->bEnableFishingRuntime = true;
	Settings->FishingSessionStateTree = NewObject<UStateTree>(GetTransientPackage());
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
