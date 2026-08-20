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

	Settings->bEnableRunRuntime = false;
	Settings->DayLengthSeconds = 0.0f;
	Settings->QuotaTarget = 0;
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::Undecided;
	Settings->NightJoinReadyPolicy = ECatRunPolicyDecision::Undecided;
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Undecided;
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Undecided;
	Settings->FinalDayIndex = 10;

	float DayLengthSeconds = 12.0f;
	int32 QuotaTarget = 7;
	TestFalse(TEXT("默认 Run runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("默认白天参数读取失败"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("失败时白天时长清零"), DayLengthSeconds, 0.0f);
	TestEqual(TEXT("失败时额度目标清零"), QuotaTarget, 0);
	TestFalse(TEXT("默认夜晚晚加入/重连准入关闭"), Settings->CanAdmitLateNightReady());
	TestFalse(TEXT("默认成功结算策略关闭"), Settings->IsSuccessSettlementEnabled());

	Settings->bEnableRunRuntime = true;
	// 白天时长使用飞书《参数与校准记录》rev10「当前拍定参数」表第一行的 20 分钟（1200 秒，暂定值，依据首轮模拟单人约 40 条/白天）。
	Settings->DayLengthSeconds = 1200.0f;
	Settings->QuotaTarget = 3;
	TestFalse(TEXT("未选择额度策略时 runtime 仍不可运行"), Settings->IsRuntimeReady());
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
	TestTrue(TEXT("显式 runtime 与固定额度策略启用 Run"), Settings->IsRuntimeReady());
	TestTrue(TEXT("完整白天参数可读取"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	TestEqual(TEXT("白天时长保持配置值"), DayLengthSeconds, 1200.0f);
	TestEqual(TEXT("额度目标保持配置值"), QuotaTarget, 3);

	Settings->NightJoinReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestFalse(TEXT("只开放晚加入不开放重连时仍拒绝夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->NightReconnectReadyPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("晚加入与重连都显式启用后允许夜晚准入"), Settings->CanAdmitLateNightReady());
	Settings->SuccessSettlementPolicy = ECatRunPolicyDecision::Enabled;
	TestTrue(TEXT("成功结算只在显式 Enabled 时开放"), Settings->IsSuccessSettlementEnabled());

	TestEqual(TEXT("成功终局默认锚定第 10 天"), Settings->FinalDayIndex, 10);
	int32 FinalDayIndex = 0;
	TestTrue(TEXT("成功策略启用且最终天数有效时可读取终局天数"),
		Settings->TryGetSuccessSettlementFinalDay(FinalDayIndex));
	TestEqual(TEXT("成功终局读取第 10 天"), FinalDayIndex, 10);
	TestFalse(TEXT("第 9 天不能进入成功结算夜"), Settings->CanEnterSuccessSettlementNight(9));
	TestTrue(TEXT("第 10 天可以进入成功结算夜"), Settings->CanEnterSuccessSettlementNight(10));
	TestFalse(TEXT("超过最终日不靠大于关系补票进入成功结算夜"), Settings->CanEnterSuccessSettlementNight(11));
	Settings->FinalDayIndex = 0;
	FinalDayIndex = 10;
	TestFalse(TEXT("非法最终天数保持 fail-closed"), Settings->TryGetSuccessSettlementFinalDay(FinalDayIndex));
	TestEqual(TEXT("读取失败时最终天数清零"), FinalDayIndex, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunSettingsDailyQuotaCurveTest,
	"Catfishing.Unit.Run.Settings.DailyQuotaCurveRequiresExplicitEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRunSettingsProjectDefaultsTest,
	"Catfishing.Unit.Run.Settings.ProjectDefaultsEnableWork02RuntimeSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：把额度策略切到逐日曲线档，依次验证空表、非法条目、重复裁定和完整表；它锁住的不变量是——
// 飞书只裁了"额度逐日上升、每天清晨按在场人数确定"这条规则，没给首日值、斜率和人数公式，所以曲线数值缺一条都不能启动
// Run，也不能靠邻近条目推算出一个额度。
bool FCatRunSettingsDailyQuotaCurveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatRunSettings* Settings = NewObject<UCatRunSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Run Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableRunRuntime = true;
	Settings->DayLengthSeconds = 1200.0f;
	Settings->QuotaTarget = 3;
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::DailyCurveByMorningPlayerCount;
	Settings->DailyQuotaCurve.Reset();

	int32 CurveQuotaTarget = 9;
	TestFalse(TEXT("曲线档空表时 Run 拒绝启动"), Settings->IsRuntimeReady());
	TestFalse(TEXT("曲线档空表时读不到当日额度"), Settings->TryGetDailyQuotaTarget(1, 2, CurveQuotaTarget));
	TestEqual(TEXT("读取失败时当日额度清零"), CurveQuotaTarget, 0);
	float CurveDayLengthSeconds = 5.0f;
	int32 FixedQuotaTarget = 5;
	TestFalse(TEXT("曲线档不能走固定额度读取口"),
		Settings->TryGetDayParameters(CurveDayLengthSeconds, FixedQuotaTarget));
	TestEqual(TEXT("曲线档固定读取失败时白天时长清零"), CurveDayLengthSeconds, 0.0f);
	TestEqual(TEXT("曲线档固定读取失败时额度清零"), FixedQuotaTarget, 0);

	FCatRunDailyQuotaEntry& IncompleteEntry = Settings->DailyQuotaCurve.AddDefaulted_GetRef();
	IncompleteEntry.DayIndex = 1;
	IncompleteEntry.PlayerCount = 2;
	IncompleteEntry.QuotaTarget = 0;
	TestFalse(TEXT("条目缺额度数值时整表 fail-closed"), Settings->IsRuntimeReady());

	Settings->DailyQuotaCurve[0].QuotaTarget = 4;
	TestTrue(TEXT("配齐一条裁定后曲线档可启动"), Settings->IsRuntimeReady());
	TestTrue(TEXT("已登记的天数与人数可读取额度"), Settings->TryGetDailyQuotaTarget(1, 2, CurveQuotaTarget));
	TestEqual(TEXT("额度原样返回登记值"), CurveQuotaTarget, 4);
	TestFalse(TEXT("未登记的人数不按邻近条目推算"), Settings->TryGetDailyQuotaTarget(1, 3, CurveQuotaTarget));
	TestEqual(TEXT("人数未登记时输出清零"), CurveQuotaTarget, 0);
	TestFalse(TEXT("未登记的天数不按前一天推算"), Settings->TryGetDailyQuotaTarget(2, 2, CurveQuotaTarget));
	TestEqual(TEXT("天数未登记时输出清零"), CurveQuotaTarget, 0);

	FCatRunDailyQuotaEntry& SecondDayEntry = Settings->DailyQuotaCurve.AddDefaulted_GetRef();
	SecondDayEntry.DayIndex = 2;
	SecondDayEntry.PlayerCount = 2;
	SecondDayEntry.QuotaTarget = 6;
	TestTrue(TEXT("多条裁定共存时曲线仍可启动"), Settings->IsRuntimeReady());
	TestTrue(TEXT("第二天裁定可独立读取"), Settings->TryGetDailyQuotaTarget(2, 2, CurveQuotaTarget));
	TestEqual(TEXT("第二天额度读取到自己的登记值"), CurveQuotaTarget, 6);

	FCatRunDailyQuotaEntry& DuplicateEntry = Settings->DailyQuotaCurve.AddDefaulted_GetRef();
	DuplicateEntry.DayIndex = 2;
	DuplicateEntry.PlayerCount = 2;
	DuplicateEntry.QuotaTarget = 7;
	TestFalse(TEXT("同一天同人数被裁两次时整表 fail-closed"), Settings->IsRuntimeReady());
	TestFalse(TEXT("重复裁定下不再输出任何额度"), Settings->TryGetDailyQuotaTarget(2, 2, CurveQuotaTarget));
	TestEqual(TEXT("重复裁定失败时输出清零"), CurveQuotaTarget, 0);

	Settings->DailyQuotaCurve.Pop();
	Settings->PlayerScalingPolicy = ECatRunScalingPolicy::FixedQuotaTarget;
	TestTrue(TEXT("固定档忽略天数与人数返回单一配置额度"), Settings->TryGetDailyQuotaTarget(7, 4, CurveQuotaTarget));
	TestEqual(TEXT("固定档额度就是 QuotaTarget"), CurveQuotaTarget, 3);
	return !HasAnyErrors();
}

// 测试流程：从项目 DefaultGame.ini 读取正式 Run Settings，而不是手动拼一个瞬态对象；验证 WORK-02 当前切片已经显式启用
// 固定白天、额度、夜晚准入和第 10 天成功结算，避免 Lake 入口因为漏接配置继续停在未裁策略。
bool FCatRunSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	TestNotNull(TEXT("项目默认 Run Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	float DayLengthSeconds = 0.0f;
	int32 QuotaTarget = 0;
	TestTrue(TEXT("项目默认值显式启用 Work2 Run runtime"), Settings->IsRuntimeReady());
	TestTrue(TEXT("项目默认白天参数可被 GameMode 读取"), Settings->TryGetDayParameters(DayLengthSeconds, QuotaTarget));
	// 1200 秒来自飞书《参数与校准记录》rev10 唯一挂过号的 Run 数值：白天时长 20 分钟（暂定）。
	// 这条断言故意直接锁飞书值，因此 Config/DefaultGame.ini 的 DayLengthSeconds 没改成 1200.0 之前它会失败。
	TestEqual(TEXT("项目默认白天时长对齐飞书拍定的 20 分钟"), DayLengthSeconds, 1200.0f);
	TestEqual(TEXT("项目默认额度目标保持工程切片值"), QuotaTarget, 3);
	TestTrue(TEXT("项目默认夜晚加入与重连 ready 策略均已启用"), Settings->CanAdmitLateNightReady());

	int32 FinalDayIndex = 0;
	TestTrue(TEXT("项目默认成功结算天数可读取"), Settings->TryGetSuccessSettlementFinalDay(FinalDayIndex));
	TestEqual(TEXT("项目默认成功结算锁定第 10 天"), FinalDayIndex, 10);
	TestFalse(TEXT("第 9 天不会提前进入成功结算夜"), Settings->CanEnterSuccessSettlementNight(9));
	TestTrue(TEXT("第 10 天可以进入成功结算夜"), Settings->CanEnterSuccessSettlementNight(10));
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
