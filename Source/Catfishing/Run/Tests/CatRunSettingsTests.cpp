#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Run/CatRunSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunSettingsRuntimePoliciesTest,
	"Catfishing.Unit.Run.Settings.RuntimeAdmissionAndSettlementPoliciesAreExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：逐项读取 Run Settings 的启动、白天参数、夜晚准入和成功结算 gate；测试先覆盖配置加载出的项目值，再分别制造总开关、策略、数值和准入缺口，证明 Run Core 不会替产品补策略。
bool FCatRunSettingsRuntimePoliciesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatRunSettings* Settings = NewObject<UCatRunSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Run Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableRunRuntime = false;
	Settings->DayLengthSeconds = 0.0f;
	Settings->QuotaTarget = 0;
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;
	Settings->NightJoinReadyPolicy = ECatRunPolicyDecision::Undecided;
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Undecided;
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;

	float DayLengthSeconds = 12.0f;
	int32 QuotaTarget = 7;
	TestFalse(TEXT("未裁 Run runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("未裁白天参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("失败时白天时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("失败时额度目标清零"), QuotaTarget, 0);
	TestFalse(TEXT("未裁夜晚晚加入/重连准入关闭"), Settings->CanAdmitLateNightReady());
	TestFalse(TEXT("未裁成功结算策略关闭"), Settings->IsSuccessSettlementEnabled());

	Settings->DayLengthSeconds = 120.0f;
	Settings->QuotaTarget = 3;
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
	DayLengthSeconds = 12.0f;
	QuotaTarget = 7;
	TestFalse(TEXT("只有固定额度策略但总开关关闭时 runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("总开关关闭时白天参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("总开关关闭时白天时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("总开关关闭时额度目标清零"), QuotaTarget, 0);

	Settings->bEnableRunRuntime = true;
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;
	TestFalse(TEXT("未选择固定额度策略时 runtime 仍不可运行"), Settings->IsRuntimeReady());
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
	Settings->DayLengthSeconds = 0.0f;
	DayLengthSeconds = 12.0f;
	QuotaTarget = 7;
	TestFalse(TEXT("完整 runtime gate 但白天时长未裁时参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("白天时长未裁时输出时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("白天时长未裁时输出额度清零"), QuotaTarget, 0);
	Settings->DayLengthSeconds = 120.0f;
	Settings->QuotaTarget = 0;
	DayLengthSeconds = 12.0f;
	QuotaTarget = 7;
	TestFalse(TEXT("完整 runtime gate 但额度未裁时参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("额度未裁时输出时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("额度未裁时输出额度清零"), QuotaTarget, 0);
	Settings->QuotaTarget = 3;
	TestTrue(TEXT("显式 runtime 与固定额度策略启用 Run"), Settings->IsRuntimeReady());
	TestTrue(TEXT("完整白天参数可读取"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("白天时长保持配置值"), DayLengthSeconds, 120.0f);
	TestEqual(TEXT("额度目标保持配置值"), QuotaTarget, 3);

	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestFalse(TEXT("只开放重连不开放晚加入时仍拒绝夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Undecided;
	Settings->NightJoinReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestFalse(TEXT("只开放晚加入不开放重连时仍拒绝夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("晚加入与重连都显式启用后允许夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Disabled;
	TestFalse(TEXT("成功结算显式 Disabled 时仍关闭"), Settings->IsSuccessSettlementEnabled());
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("成功结算只在显式 Enabled 时开放"), Settings->IsSuccessSettlementEnabled());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
