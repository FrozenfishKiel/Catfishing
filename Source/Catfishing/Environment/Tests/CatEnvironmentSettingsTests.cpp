#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatEnvironmentSettings.h"

namespace CatEnvironmentSettingsTest
{
	/** 构造一个处于白天且带有效服务器白天区间的 Run 快照；DayIndex 取 1，因为调度槽号以它为进位基数，0 不是合法天序。 */
	static FCatRunPhaseSnapshot MakeDaySnapshot(const double DeadlineSeconds = 100.0)
	{
		FCatRunPhaseSnapshot Snapshot;
		Snapshot.DayIndex = 1;
		Snapshot.Phase = ECatRunPhase::DayActive;
		Snapshot.ServerTimeAnchorSeconds = 0.0;
		Snapshot.DeadlineServerTimeSeconds = DeadlineSeconds;
		Snapshot.bHasDeadline = true;
		return Snapshot;
	}

	/** 构造一个已启用、晨末 0.25 暮初 0.75、天气交给调度的瞬态配置；返回的对象只活在单个测试里，不碰磁盘配置。 */
	static UCatEnvironmentSettings* MakeScheduledSettings()
	{
		UCatEnvironmentSettings* Settings = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
		if (Settings)
		{
			Settings->bEnableEnvironmentRuntime = true;
			Settings->ConfiguredWeather = ECatEnvironmentWeather::Unknown;
			Settings->MorningEndFraction = 0.25;
			Settings->DuskStartFraction = 0.75;
			Settings->WeatherClearWeight = 0.6;
			Settings->WeatherRainWeight = 0.25;
			Settings->WeatherFogWeight = 0.15;
		}
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsRuntimeAndAggregationTest,
	"Catfishing.Unit.Environment.Settings.TimeOfDayAndNaturalAggregationRequireExplicitRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Environment Settings 验证 runtime readiness、局内时段计算和自然投窝量读取；
// 失败路径必须清空输出，成功路径只复制显式数据。天气权重全部清零是这里唯一能让"没有天气来源"成立的方式，
// 因为显式覆盖和权重抽样是两条并列的天气来源，只关掉一条不足以 fail-closed。
bool FCatEnvironmentSettingsRuntimeAndAggregationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
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
	Settings->NaturalAggregationContribution = FCatChumVector();

	FCatChumVector Contribution;
	Contribution.Fishy = 9.0;
	TestFalse(TEXT("默认环境配置不可运行"), Settings->IsRuntimeReady());
	TestEqual(TEXT("默认时段解析 Unknown"), Settings->ResolveTimeOfDay(FCatRunPhaseSnapshot(), 10.0),
		ECatEnvironmentTimeOfDay::Unknown);
	TestFalse(TEXT("默认自然投窝量读取失败"), Settings->TryGetNaturalAggregationContribution(Contribution));
	TestEqual(TEXT("失败时自然投窝贡献清空"), Contribution.Fishy, 0.0);

	Settings->bEnableEnvironmentRuntime = true;
	Settings->MorningEndFraction = 0.25;
	Settings->DuskStartFraction = 0.75;
	TestFalse(TEXT("既没有显式天气也没有权重时仍不可运行"), Settings->IsRuntimeReady());
	Settings->WeatherClearWeight = 0.6;
	Settings->WeatherRainWeight = 0.25;
	Settings->WeatherFogWeight = 0.15;
	TestTrue(TEXT("有天气权重和晨暮边界即可运行"), Settings->IsRuntimeReady());

	const FCatRunPhaseSnapshot RunSnapshot = CatEnvironmentSettingsTest::MakeDaySnapshot();
	TestEqual(TEXT("白天前段解析为 Morning"), Settings->ResolveTimeOfDay(RunSnapshot, 10.0),
		ECatEnvironmentTimeOfDay::Morning);
	TestEqual(TEXT("白天中段解析为 Day"), Settings->ResolveTimeOfDay(RunSnapshot, 50.0),
		ECatEnvironmentTimeOfDay::Day);
	TestEqual(TEXT("白天后段解析为 Dusk"), Settings->ResolveTimeOfDay(RunSnapshot, 90.0),
		ECatEnvironmentTimeOfDay::Dusk);

	Settings->NaturalAggregationContribution.Fishy = 1.0;
	TestTrue(TEXT("配好三轴后自然投窝量可读取"), Settings->TryGetNaturalAggregationContribution(Contribution));
	TestEqual(TEXT("自然投窝贡献保持配置值"), Contribution.Fishy, 1.0);

	Settings->NaturalAggregationContribution = FCatChumVector();
	TestFalse(TEXT("三轴全零时自然投窝量读取失败"), Settings->TryGetNaturalAggregationContribution(Contribution));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsScheduleSlotTest,
	"Catfishing.Unit.Environment.Settings.ScheduleSlotComesOnlyFromInRunClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：验证调度槽完全由 Run 的天序号与白天进度决定——一天四段各占一个槽、夜晚整段共用最后一个槽、
// 天数推进时槽号单调递增；不拥有环境的阶段一律拿不到槽。这里推进的只有 RunSnapshot 里的服务器秒，不涉及任何现实时间。
bool FCatEnvironmentSettingsScheduleSlotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = CatEnvironmentSettingsTest::MakeScheduledSettings();
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	FCatRunPhaseSnapshot RunSnapshot = CatEnvironmentSettingsTest::MakeDaySnapshot();
	int64 MorningSlot = 0;
	int64 DaySlot = 0;
	int64 DuskSlot = 0;
	int64 NightSlot = 0;
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;

	TestTrue(TEXT("白天前段可解析调度槽"), Settings->TryResolveScheduleSlot(RunSnapshot, 10.0, MorningSlot, TimeOfDay));
	TestEqual(TEXT("白天前段槽的时段是 Morning"), TimeOfDay, ECatEnvironmentTimeOfDay::Morning);
	TestTrue(TEXT("白天中段可解析调度槽"), Settings->TryResolveScheduleSlot(RunSnapshot, 50.0, DaySlot, TimeOfDay));
	TestEqual(TEXT("白天中段槽的时段是 Day"), TimeOfDay, ECatEnvironmentTimeOfDay::Day);
	TestTrue(TEXT("白天后段可解析调度槽"), Settings->TryResolveScheduleSlot(RunSnapshot, 90.0, DuskSlot, TimeOfDay));
	TestEqual(TEXT("白天后段槽的时段是 Dusk"), TimeOfDay, ECatEnvironmentTimeOfDay::Dusk);
	TestEqual(TEXT("晨槽与昼槽相邻"), DaySlot, MorningSlot + 1);
	TestEqual(TEXT("昼槽与暮槽相邻"), DuskSlot, DaySlot + 1);

	// 夜晚在时段轴上没有取值，所以它靠"解析成功但时段是 Unknown"来表达；这与"根本拿不到槽"是两件事。
	RunSnapshot.Phase = ECatRunPhase::NormalNight;
	RunSnapshot.bHasDeadline = false;
	TestTrue(TEXT("普通夜晚可解析调度槽"), Settings->TryResolveScheduleSlot(RunSnapshot, 0.0, NightSlot, TimeOfDay));
	TestEqual(TEXT("夜晚槽在时段轴上没有取值"), TimeOfDay, ECatEnvironmentTimeOfDay::Unknown);
	TestEqual(TEXT("夜槽紧接在暮槽之后"), NightSlot, DuskSlot + 1);

	// 第二天的第一个槽必须严格大于第一天的夜槽，否则天气序列会在跨天时回到前一天的取值。
	FCatRunPhaseSnapshot NextDay = CatEnvironmentSettingsTest::MakeDaySnapshot();
	NextDay.DayIndex = 2;
	int64 NextDayMorningSlot = 0;
	TestTrue(TEXT("第二天白天前段可解析调度槽"),
		Settings->TryResolveScheduleSlot(NextDay, 10.0, NextDayMorningSlot, TimeOfDay));
	TestEqual(TEXT("跨天后槽号继续递增"), NextDayMorningSlot, NightSlot + 1);

	int64 RejectedSlot = -1;
	FCatRunPhaseSnapshot Settlement = CatEnvironmentSettingsTest::MakeDaySnapshot();
	Settlement.Phase = ECatRunPhase::FailureSettlementNight;
	TestFalse(TEXT("结算夜不拥有环境调度槽"),
		Settings->TryResolveScheduleSlot(Settlement, 10.0, RejectedSlot, TimeOfDay));
	TestEqual(TEXT("拒绝时槽号清零"), RejectedSlot, int64(0));

	FCatRunPhaseSnapshot NotStarted;
	NotStarted.Phase = ECatRunPhase::NotStarted;
	TestFalse(TEXT("未开局时拿不到调度槽"),
		Settings->TryResolveScheduleSlot(NotStarted, 10.0, RejectedSlot, TimeOfDay));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsWeatherScheduleTest,
	"Catfishing.Unit.Environment.Settings.WeatherScheduleIsDeterministicPerSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：验证按槽抽样的天气是纯函数（同槽同种子必然同结果）、三种天气在长序列里都真的会出现（不会退化成恒晴）、
// 显式覆盖能盖过抽样，以及权重全零时 fail-closed 成 Unknown。整条断言都不依赖真随机，只依赖显式槽号与显式种子。
bool FCatEnvironmentSettingsWeatherScheduleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = CatEnvironmentSettingsTest::MakeScheduledSettings();
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	constexpr int32 FixedSeed = 20260820;
	TestEqual(TEXT("同一槽同一种子的天气可复现"),
		Settings->ResolveWeatherForSlot(4, FixedSeed), Settings->ResolveWeatherForSlot(4, FixedSeed));

	int32 ClearCount = 0;
	int32 RainCount = 0;
	int32 FogCount = 0;
	for (int64 SlotIndex = 0; SlotIndex < 200; ++SlotIndex)
	{
		switch (Settings->ResolveWeatherForSlot(SlotIndex, FixedSeed))
		{
		case ECatEnvironmentWeather::Clear: ++ClearCount; break;
		case ECatEnvironmentWeather::Rain: ++RainCount; break;
		case ECatEnvironmentWeather::Fog: ++FogCount; break;
		default: break;
		}
	}
	TestTrue(TEXT("长序列里出现过晴天"), ClearCount > 0);
	TestTrue(TEXT("长序列里出现过雨天"), RainCount > 0);
	TestTrue(TEXT("长序列里出现过雾天"), FogCount > 0);
	TestEqual(TEXT("每一槽都得到了一种已知天气"), ClearCount + RainCount + FogCount, 200);
	// 权重口径是"晴最多"，这条断言保证权重真的被读进了抽样，而不是三档等概率。
	TestTrue(TEXT("晴天按权重占多数"), ClearCount > RainCount && ClearCount > FogCount);

	Settings->ConfiguredWeather = ECatEnvironmentWeather::Rain;
	TestEqual(TEXT("显式覆盖盖过按槽抽样"), Settings->ResolveWeatherForSlot(4, FixedSeed), ECatEnvironmentWeather::Rain);

	Settings->ConfiguredWeather = ECatEnvironmentWeather::Unknown;
	Settings->WeatherClearWeight = 0.0;
	Settings->WeatherRainWeight = 0.0;
	Settings->WeatherFogWeight = 0.0;
	TestEqual(TEXT("权重全零时天气 fail-closed"), Settings->ResolveWeatherForSlot(4, FixedSeed),
		ECatEnvironmentWeather::Unknown);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsNaturalEventTest,
	"Catfishing.Unit.Environment.Settings.NaturalEventsFollowAppearanceConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：逐条验证飞书环境册的六类事件出场条件。涌现概率被显式钉成 1 或 0，
// 因此"森林湖鱼群出不出场"完全由聚鱼时刻是否可用决定，不掺任何抽样结果；其余五类本来就只看时段与天气。
bool FCatEnvironmentSettingsNaturalEventTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = CatEnvironmentSettingsTest::MakeScheduledSettings();
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}
	constexpr int32 FixedSeed = 20260820;
	constexpr int64 SlotIndex = 8;

	// 先关掉自然涌现，单独看另外五类的出场条件。
	Settings->NaturalSchoolEmergenceChance = 0.0;
	TestEqual(TEXT("雨后放晴的白天出彩虹"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Day,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Rain, true),
		CatEnvironmentEvents::Rainbow);
	TestEqual(TEXT("没下过雨的白天不出彩虹而出鸟群蝴蝶"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Day,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Clear, true),
		CatEnvironmentEvents::BirdsAndButterflies);
	TestEqual(TEXT("黄昏出晚霞"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Dusk,
			ECatEnvironmentWeather::Fog, ECatEnvironmentWeather::Fog, true),
		CatEnvironmentEvents::SunsetGlow);
	TestEqual(TEXT("夜晚且晴出月光湖面"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Unknown,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Clear, true),
		CatEnvironmentEvents::MoonlitLake);
	TestEqual(TEXT("夜晚不晴出萤火虫"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Unknown,
			ECatEnvironmentWeather::Rain, ECatEnvironmentWeather::Rain, true),
		CatEnvironmentEvents::Fireflies);
	TestEqual(TEXT("天气未知时不产生任何事件"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Day,
			ECatEnvironmentWeather::Unknown, ECatEnvironmentWeather::Clear, true),
		FName(NAME_None));

	// 把涌现概率钉成 1：白天且聚鱼时刻可用时森林湖鱼群必然出场，并压过同槽也满足条件的彩虹。
	Settings->NaturalSchoolEmergenceChance = 1.0;
	TestEqual(TEXT("白天且聚鱼时刻可用时出森林湖鱼群"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Day,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Rain, true),
		CatEnvironmentEvents::ForestLakeSchool);
	TestEqual(TEXT("聚鱼时刻在冷却里时让位给同槽的彩虹"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Day,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Rain, false),
		CatEnvironmentEvents::Rainbow);
	TestEqual(TEXT("夜晚不出森林湖鱼群"),
		Settings->ResolveNaturalEventId(SlotIndex, FixedSeed, ECatEnvironmentTimeOfDay::Unknown,
			ECatEnvironmentWeather::Clear, ECatEnvironmentWeather::Clear, true),
		CatEnvironmentEvents::MoonlitLake);

	// 抛印记的名单直接对着飞书那一列锁死：六类里只有鸟群/蝴蝶不单独抛。
	TestTrue(TEXT("彩虹抛印记"), CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::Rainbow));
	TestTrue(TEXT("萤火虫抛印记"), CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::Fireflies));
	TestTrue(TEXT("晚霞抛印记"), CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::SunsetGlow));
	TestTrue(TEXT("月光湖面抛印记"), CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::MoonlitLake));
	TestTrue(TEXT("森林湖鱼群抛印记"),
		CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::ForestLakeSchool));
	TestFalse(TEXT("鸟群蝴蝶不单独抛印记"),
		CatEnvironmentEvents::DoesEventProduceImprintCandidate(CatEnvironmentEvents::BirdsAndButterflies));
	TestFalse(TEXT("未登记的事件名不抛印记"),
		CatEnvironmentEvents::DoesEventProduceImprintCandidate(FName(TEXT("MadeUpEvent"))));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsTimeOfDayScheduleTest,
	"Catfishing.Unit.Environment.Settings.NextTimeOfDayBoundaryOnlyExistsDuringDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsBiteIntervalTest,
	"Catfishing.Unit.Environment.Settings.BiteIntervalFollowsChumTotalFormula",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：验证时段分界调度接口只在白天给出下一个需要重新发布环境快照的服务器时刻；
// 越过暮初之后没有下一个分界（白天收尾由 Run 自己的截止事件负责），夜晚与无截止一律返回 false——夜晚不能有任何钟。
bool FCatEnvironmentSettingsTimeOfDayScheduleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = CatEnvironmentSettingsTest::MakeScheduledSettings();
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	FCatRunPhaseSnapshot RunSnapshot = CatEnvironmentSettingsTest::MakeDaySnapshot();

	double BoundarySeconds = -1.0;
	TestTrue(TEXT("白天前段存在下一个时段分界"), Settings->TryGetNextTimeOfDayBoundarySeconds(RunSnapshot, 10.0, BoundarySeconds));
	TestEqual(TEXT("下一个分界是晨末绝对时刻"), BoundarySeconds, 25.0, 1e-9);
	// 刚被晨末分界唤醒时必须给出暮初而不是同一个 25，否则调度会在同一时刻空转。
	TestTrue(TEXT("越过晨末后给出暮初分界"), Settings->TryGetNextTimeOfDayBoundarySeconds(RunSnapshot, 25.0, BoundarySeconds));
	TestEqual(TEXT("暮初分界为绝对时刻 75"), BoundarySeconds, 75.0, 1e-9);
	TestFalse(TEXT("进入 Dusk 后不再有下一个分界"),
		Settings->TryGetNextTimeOfDayBoundarySeconds(RunSnapshot, 80.0, BoundarySeconds));
	TestEqual(TEXT("无分界时输出清零"), BoundarySeconds, 0.0);

	RunSnapshot.bHasDeadline = false;
	TestFalse(TEXT("没有白天截止时不给分界"),
		Settings->TryGetNextTimeOfDayBoundarySeconds(RunSnapshot, 10.0, BoundarySeconds));
	RunSnapshot.bHasDeadline = true;
	RunSnapshot.Phase = ECatRunPhase::FailureSettlementNight;
	TestFalse(TEXT("夜晚不给任何分界计时"),
		Settings->TryGetNextTimeOfDayBoundarySeconds(RunSnapshot, 10.0, BoundarySeconds));
	return !HasAnyErrors();
}

// 测试流程：验证咬钩间隔按 T_base / (1 + Total / K) 求值，两档 T_base 分别对应有窝和无窝；
// 配置未就绪或 Total 非法时必须 fail-closed，而不是返回一个编造的秒数。
bool FCatEnvironmentSettingsBiteIntervalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	// 瞬态对象是从 CDO 复制的，而 CDO 的环境总 gate 已经被 DefaultGame.ini 打开；这里显式关掉它才能测到 fail-closed 分支。
	Settings->bEnableEnvironmentRuntime = false;
	double IntervalSeconds = -1.0;
	TestFalse(TEXT("未开启环境 runtime 时咬钩间隔 fail-closed"),
		Settings->TryResolveBiteIntervalSeconds(0.0, IntervalSeconds));
	TestEqual(TEXT("fail-closed 时输出清零"), IntervalSeconds, 0.0);

	Settings->bEnableEnvironmentRuntime = true;
	TestTrue(TEXT("窝料配置就绪"), Settings->IsChumRuntimeReady());
	TestEqual(TEXT("有窝基准秒数为 15"), Settings->ChumBiteBaseSecondsWithChum, 15.0);
	TestEqual(TEXT("无窝基准秒数为 120"), Settings->ChumBiteBaseSecondsWithoutChum, 120.0);
	TestEqual(TEXT("Total 缩放 K 为 100"), Settings->ChumBiteTotalScaleK, 100.0);

	TestTrue(TEXT("无窝料可求咬钩间隔"), Settings->TryResolveBiteIntervalSeconds(0.0, IntervalSeconds));
	TestEqual(TEXT("Total 为零时咬钩间隔 120 秒"), IntervalSeconds, 120.0, 1e-9);
	TestTrue(TEXT("Total 等于 K 时可求咬钩间隔"), Settings->TryResolveBiteIntervalSeconds(100.0, IntervalSeconds));
	TestEqual(TEXT("Total 等于 K 时咬钩间隔减半"), IntervalSeconds, 7.5, 1e-9);
	TestTrue(TEXT("Total 为三倍 K 时可求咬钩间隔"), Settings->TryResolveBiteIntervalSeconds(300.0, IntervalSeconds));
	TestEqual(TEXT("Total 为三倍 K 时咬钩间隔为四分之一"), IntervalSeconds, 3.75, 1e-9);
	TestFalse(TEXT("负 Total 咬钩间隔 fail-closed"), Settings->TryResolveBiteIntervalSeconds(-1.0, IntervalSeconds));
	TestEqual(TEXT("负 Total 时输出清零"), IntervalSeconds, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsProjectDefaultsTest,
	"Catfishing.Unit.Environment.Settings.ProjectDefaultsEnableWeatherAndEventSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目 DefaultGame.ini 读取正式 Environment Settings，验证正式配置确实把天气交给了按局内时钟的调度
// （而不是钉死一种天气），六类事件所需的自然聚鱼落点水域与投放量都已接线，并且时段仍只由局内服务器白天区间解析。
bool FCatEnvironmentSettingsProjectDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	TestNotNull(TEXT("项目默认 Environment Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("项目默认环境 runtime 显式启用"), Settings->IsRuntimeReady());
	TestEqual(TEXT("项目默认不钉死天气，交给按槽调度"), Settings->ConfiguredWeather, ECatEnvironmentWeather::Unknown);
	TestTrue(TEXT("项目默认三种天气都有正权重"),
		Settings->WeatherClearWeight > 0.0 && Settings->WeatherRainWeight > 0.0 && Settings->WeatherFogWeight > 0.0);
	TestEqual(TEXT("项目默认按 RunId 派生天气种子"), Settings->WeatherScheduleSeed, 0);
	TestEqual(TEXT("项目默认自然聚鱼落在森林湖"), Settings->NaturalAggregationRegionId, FName(TEXT("ForestLake")));
	TestTrue(TEXT("项目默认自然涌现概率为正的暂定值"), Settings->NaturalSchoolEmergenceChance > 0.0);
	TestTrue(TEXT("项目默认聚鱼时刻共享冷却为正的暂定值"), Settings->AggregationMomentCooldownSeconds > 0.0);

	FCatChumVector Contribution;
	TestTrue(TEXT("项目默认已配置自然聚鱼投放量"), Settings->TryGetNaturalAggregationContribution(Contribution));
	TestTrue(TEXT("自然聚鱼投放量为正"), Contribution.Total() > 0.0);

	const FCatRunPhaseSnapshot RunSnapshot = CatEnvironmentSettingsTest::MakeDaySnapshot(120.0);
	TestEqual(TEXT("项目默认白天前段解析为 Morning"), Settings->ResolveTimeOfDay(RunSnapshot, 10.0),
		ECatEnvironmentTimeOfDay::Morning);
	TestEqual(TEXT("项目默认白天中段解析为 Day"), Settings->ResolveTimeOfDay(RunSnapshot, 60.0),
		ECatEnvironmentTimeOfDay::Day);
	TestEqual(TEXT("项目默认白天后段解析为 Dusk"), Settings->ResolveTimeOfDay(RunSnapshot, 110.0),
		ECatEnvironmentTimeOfDay::Dusk);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
