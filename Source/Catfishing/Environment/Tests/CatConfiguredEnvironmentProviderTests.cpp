#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatConfiguredEnvironmentProvider.h"
#include "Environment/CatEnvironmentSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConfiguredEnvironmentProviderRuntimeGateTest,
	"Catfishing.Unit.Environment.ConfiguredProvider.RequiresRuntimeSettingsAndReturnsReadOnlySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConfiguredEnvironmentProviderScheduleTest,
	"Catfishing.Unit.Environment.ConfiguredProvider.WeatherAndEventComeFromInRunSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatConfiguredEnvironmentProviderTest
{
	/**
	 * 测试期间整体接管默认 Environment Settings 的守卫；Provider 读的是 GetDefault，所以必须改默认对象再原样还回去，
	 * 否则本用例的配置会漏给后续所有 Environment 与 Fishing 用例。
	 */
	struct FEnvironmentSettingsScope
	{
		/** 被临时改写的默认配置对象。 */
		UCatEnvironmentSettings* Settings = GetMutableDefault<UCatEnvironmentSettings>();

		/** 进入作用域时的整份配置副本；析构时按值还原，不必逐个字段记住旧值。 */
		UCatEnvironmentSettings* Saved = nullptr;

		/** 构造流程：把默认对象整份拷进一个瞬态对象存起来，随后由调用方自己写入本用例需要的配置。 */
		FEnvironmentSettingsScope()
		{
			if (Settings)
			{
				Saved = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
				CopyValues(*Settings, *Saved);
			}
		}

		/** 析构流程：把开始时的整份配置写回默认对象。 */
		~FEnvironmentSettingsScope()
		{
			if (Settings && Saved)
			{
				CopyValues(*Saved, *Settings);
			}
		}

		/** 逐字段搬运本测试会改到的那些配置；只列参与天气/时段/事件裁决的项，窝料与咬钩公式不在本用例范围内。 */
		static void CopyValues(const UCatEnvironmentSettings& From, UCatEnvironmentSettings& To)
		{
			To.bEnableEnvironmentRuntime = From.bEnableEnvironmentRuntime;
			To.ConfiguredWeather = From.ConfiguredWeather;
			To.MorningEndFraction = From.MorningEndFraction;
			To.DuskStartFraction = From.DuskStartFraction;
			To.WeatherClearWeight = From.WeatherClearWeight;
			To.WeatherRainWeight = From.WeatherRainWeight;
			To.WeatherFogWeight = From.WeatherFogWeight;
			To.WeatherScheduleSeed = From.WeatherScheduleSeed;
			To.NaturalSchoolEmergenceChance = From.NaturalSchoolEmergenceChance;
		}
	};

	// 快照流程：构造 DayActive 且带有效截止的 Run 输入；Provider 只能消费这份 DTO，不回写 Run。
	static FCatRunPhaseSnapshot MakeDaySnapshot()
	{
		FCatRunPhaseSnapshot Snapshot;
		Snapshot.RunId = FGuid::NewGuid();
		Snapshot.DayIndex = 1;
		Snapshot.Phase = ECatRunPhase::DayActive;
		Snapshot.ServerTimeAnchorSeconds = 0.0;
		Snapshot.DeadlineServerTimeSeconds = 100.0;
		Snapshot.bHasDeadline = true;
		Snapshot.bFishingAllowed = true;
		return Snapshot;
	}
}

// 测试流程：先在显式关闭的配置下验证 Provider fail-closed，再启用一份完整配置并钉死天气覆盖，
// 确认输出只来自 Run 快照与显式配置，且 Revision 被原样带回。
bool FCatConfiguredEnvironmentProviderRuntimeGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Environment Provider 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatConfiguredEnvironmentProvider* Provider = NewObject<UCatConfiguredEnvironmentProvider>(World);
	TestNotNull(TEXT("可创建配置环境 Provider"), Provider);
	if (!Provider)
	{
		return false;
	}

	const FCatRunPhaseSnapshot InputSnapshot = CatConfiguredEnvironmentProviderTest::MakeDaySnapshot();
	CatConfiguredEnvironmentProviderTest::FEnvironmentSettingsScope Scope;
	UCatEnvironmentSettings* Settings = Scope.Settings;
	TestNotNull(TEXT("可改写默认 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->bEnableEnvironmentRuntime = false;
	Settings->ConfiguredWeather = ECatEnvironmentWeather::Unknown;
	Settings->MorningEndFraction = 0.0;
	Settings->DuskStartFraction = 0.0;
	Settings->WeatherClearWeight = 0.0;
	Settings->WeatherRainWeight = 0.0;
	Settings->WeatherFogWeight = 0.0;
	const FCatEnvironmentResult DisabledResult = Provider->EvaluateEnvironment(InputSnapshot, 1);
	TestFalse(TEXT("配置关闭时 Provider 拒绝生成环境"), DisabledResult.bSucceeded);
	TestEqual(TEXT("拒绝时天气保持 Unknown"), DisabledResult.Snapshot.Weather, ECatEnvironmentWeather::Unknown);

	Settings->bEnableEnvironmentRuntime = true;
	Settings->ConfiguredWeather = ECatEnvironmentWeather::Rain;
	Settings->MorningEndFraction = 0.25;
	Settings->DuskStartFraction = 0.75;
	Settings->NaturalSchoolEmergenceChance = 0.0;
	const FCatEnvironmentResult Result = Provider->EvaluateEnvironment(InputSnapshot, 7);
	TestTrue(TEXT("完整配置下 Provider 生成环境快照"), Result.bSucceeded);
	TestEqual(TEXT("天气来自显式覆盖"), Result.Snapshot.Weather, ECatEnvironmentWeather::Rain);
	TestEqual(TEXT("时段由 Run 时钟解析"), Result.Snapshot.TimeOfDay, ECatEnvironmentTimeOfDay::Morning);
	TestEqual(TEXT("环境快照记录来源 Run Revision"), Result.Snapshot.SourceRunRevision, int64(7));
	return !HasAnyErrors();
}

// 测试流程：把天气交给按槽调度，用同一份 Run 快照反复求值确认结果可复现（同一局同一槽必然同天气同事件），
// 再换一个 RunId 证明天气序列确实按局分叉，最后确认结算夜这类不拥有环境的阶段只会得到全 Unset 的快照而不是失败。
bool FCatConfiguredEnvironmentProviderScheduleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Environment Provider 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}
	UCatConfiguredEnvironmentProvider* Provider = NewObject<UCatConfiguredEnvironmentProvider>(World);
	TestNotNull(TEXT("可创建配置环境 Provider"), Provider);
	if (!Provider)
	{
		return false;
	}

	CatConfiguredEnvironmentProviderTest::FEnvironmentSettingsScope Scope;
	UCatEnvironmentSettings* Settings = Scope.Settings;
	TestNotNull(TEXT("可改写默认 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}
	Settings->bEnableEnvironmentRuntime = true;
	Settings->ConfiguredWeather = ECatEnvironmentWeather::Unknown;
	Settings->MorningEndFraction = 0.25;
	Settings->DuskStartFraction = 0.75;
	Settings->WeatherClearWeight = 0.6;
	Settings->WeatherRainWeight = 0.25;
	Settings->WeatherFogWeight = 0.15;
	Settings->WeatherScheduleSeed = 0;
	Settings->NaturalSchoolEmergenceChance = 0.0;

	const FCatRunPhaseSnapshot DaySnapshot = CatConfiguredEnvironmentProviderTest::MakeDaySnapshot();
	const FCatEnvironmentResult First = Provider->EvaluateEnvironment(DaySnapshot, 3);
	const FCatEnvironmentResult Second = Provider->EvaluateEnvironment(DaySnapshot, 4);
	TestTrue(TEXT("按槽调度下 Provider 生成环境快照"), First.bSucceeded);
	TestNotEqual(TEXT("按槽调度产出了确定天气"), First.Snapshot.Weather, ECatEnvironmentWeather::Unknown);
	TestEqual(TEXT("同一局同一槽的天气可复现"), Second.Snapshot.Weather, First.Snapshot.Weather);
	TestEqual(TEXT("同一局同一槽的事件可复现"), Second.Snapshot.ActiveEventId, First.Snapshot.ActiveEventId);
	TestTrue(TEXT("白天必定有一个环境事件"), First.Snapshot.bHasActiveEvent);

	// 天气序列按 RunId 分叉：逐 RunId 试，只要出现过一个与首局不同的天气就说明种子真的进了抽样。
	// 不断言"下一个 RunId 一定不同"——三档天气里晴占多数，相邻两局撞上同一种是正常的。
	bool bFoundDifferentWeather = false;
	for (int32 Attempt = 0; Attempt < 16 && !bFoundDifferentWeather; ++Attempt)
	{
		FCatRunPhaseSnapshot OtherRun = DaySnapshot;
		OtherRun.RunId = FGuid::NewGuid();
		bFoundDifferentWeather = Provider->EvaluateEnvironment(OtherRun, 3).Snapshot.Weather != First.Snapshot.Weather;
	}
	TestTrue(TEXT("换一局会得到不同的天气序列"), bFoundDifferentWeather);

	FCatRunPhaseSnapshot SettlementSnapshot = DaySnapshot;
	SettlementSnapshot.Phase = ECatRunPhase::FailureSettlementNight;
	const FCatEnvironmentResult SettlementResult = Provider->EvaluateEnvironment(SettlementSnapshot, 5);
	TestTrue(TEXT("结算夜求值不算失败"), SettlementResult.bSucceeded);
	TestEqual(TEXT("结算夜没有天气"), SettlementResult.Snapshot.Weather, ECatEnvironmentWeather::Unknown);
	TestFalse(TEXT("结算夜没有环境事件"), SettlementResult.Snapshot.bHasActiveEvent);
	TestEqual(TEXT("结算夜仍带回来源 Run Revision"), SettlementResult.Snapshot.SourceRunRevision, int64(5));

	FCatRunPhaseSnapshot NightSnapshot = DaySnapshot;
	NightSnapshot.Phase = ECatRunPhase::NormalNight;
	NightSnapshot.bHasDeadline = false;
	NightSnapshot.bFishingAllowed = false;
	const FCatEnvironmentResult NightResult = Provider->EvaluateEnvironment(NightSnapshot, 6);
	TestTrue(TEXT("普通夜晚仍有环境"), NightResult.bSucceeded);
	TestEqual(TEXT("夜晚在时段轴上没有取值"), NightResult.Snapshot.TimeOfDay, ECatEnvironmentTimeOfDay::Unknown);
	TestTrue(TEXT("夜晚也会出自己那两类事件"), NightResult.Snapshot.bHasActiveEvent);
	TestTrue(TEXT("夜晚事件是月光湖面或萤火虫"),
		NightResult.Snapshot.ActiveEventId == CatEnvironmentEvents::MoonlitLake
		|| NightResult.Snapshot.ActiveEventId == CatEnvironmentEvents::Fireflies);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
