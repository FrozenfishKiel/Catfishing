#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Run/CatRunSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunSettingsRuntimePoliciesTest,
	"Catfishing.Unit.Run.Settings.RuntimeAdmissionAndSettlementPoliciesAreExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：逐项读取 Run Settings 的启动、白天参数、夜晚准入和成功结算 gate；默认、半配置和完整配置共同证明 Run Core 不会替产品补策略。
bool FCatRunSettingsRuntimePoliciesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatRunSettings* Settings = NewObject<UCatRunSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Run Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	float DayLengthSeconds = 12.0f;
	int32 QuotaTarget = 7;
	TestFalse(TEXT("默认 Run runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("默认白天参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("失败时白天时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("失败时额度目标清零"), QuotaTarget, 0);
	TestFalse(TEXT("默认夜晚晚加入/重连准入关闭"), Settings->CanAdmitLateNightReady());
	TestFalse(TEXT("默认成功结算策略关闭"), Settings->IsSuccessSettlementEnabled());

	Settings->bEnableRunRuntime = true;
	Settings->DayLengthSeconds = 120.0f;
	Settings->QuotaTarget = 3;
	TestFalse(TEXT("未选择固定额度策略时 runtime 仍不可运行"), Settings->IsRuntimeReady());
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
	TestTrue(TEXT("显式 runtime 与固定额度策略启用 Run"), Settings->IsRuntimeReady());
	TestTrue(TEXT("完整白天参数可读取"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("白天时长保持配置值"), DayLengthSeconds, 120.0f);
	TestEqual(TEXT("额度目标保持配置值"), QuotaTarget, 3);

	Settings->NightJoinReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestFalse(TEXT("只开放晚加入不开放重连时仍拒绝夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("晚加入与重连都显式启用后允许夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("成功结算只在显式 Enabled 时开放"), Settings->IsSuccessSettlementEnabled());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
