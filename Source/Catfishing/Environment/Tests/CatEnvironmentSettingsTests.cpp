#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatEnvironmentSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEnvironmentSettingsRuntimeAndAggregationTest,
	"Catfishing.Unit.Environment.Settings.TimeOfDayAndNaturalChumFieldRequireExplicitRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用瞬态 Environment Settings 验证 runtime readiness、局内时段计算和自然聚鱼配置；失败路径必须清空输出，成功路径只复制显式数据。
bool FCatEnvironmentSettingsRuntimeAndAggregationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEnvironmentSettings* Settings = NewObject<UCatEnvironmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Environment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	FName ChumDefinitionId = TEXT("Dirty");
	FName AnchorId = TEXT("Dirty");
	TestFalse(TEXT("默认环境配置不可运行"), Settings->IsRuntimeReady());
	TestEqual(TEXT("默认时段解析 Unknown"), Settings->ResolveTimeOfDay(FCatRunPhaseSnapshot(), 10.0),
		ECatEnvironmentTimeOfDay::Unknown);
	TestFalse(TEXT("默认自然聚鱼读取失败"), Settings->TryGetNaturalChumField(ChumDefinitionId, AnchorId));
	TestTrue(TEXT("失败时自然聚鱼定义清空"), ChumDefinitionId.IsNone());
	TestTrue(TEXT("失败时自然聚鱼锚点清空"), AnchorId.IsNone());

	Settings->bEnableEnvironmentRuntime = true;
	Settings->ConfiguredWeather = ECatEnvironmentWeather::Clear;
	Settings->MorningEndFraction = 0.25;
	Settings->DuskStartFraction = 0.75;
	TestTrue(TEXT("显式天气和晨暮边界启用环境运行"), Settings->IsRuntimeReady());

	FCatRunPhaseSnapshot RunSnapshot;
	RunSnapshot.Phase = ECatRunPhase::DayActive;
	RunSnapshot.ServerTimeAnchorSeconds = 0.0;
	RunSnapshot.DeadlineServerTimeSeconds = 100.0;
	RunSnapshot.bHasDeadline = true;
	TestEqual(TEXT("白天前段解析为 Morning"), Settings->ResolveTimeOfDay(RunSnapshot, 10.0),
		ECatEnvironmentTimeOfDay::Morning);
	TestEqual(TEXT("白天中段解析为 Day"), Settings->ResolveTimeOfDay(RunSnapshot, 50.0),
		ECatEnvironmentTimeOfDay::Day);
	TestEqual(TEXT("白天后段解析为 Dusk"), Settings->ResolveTimeOfDay(RunSnapshot, 90.0),
		ECatEnvironmentTimeOfDay::Dusk);

	Settings->ActiveEventId = TEXT("RainBloom");
	Settings->NaturalChumDefinitionId = TEXT("NaturalChum");
	Settings->NaturalChumAnchorId = TEXT("LakeAAnchor");
	TestTrue(TEXT("完整自然事件聚鱼配置可读取"), Settings->TryGetNaturalChumField(ChumDefinitionId, AnchorId));
	TestEqual(TEXT("自然聚鱼定义保持配置值"), ChumDefinitionId, FName(TEXT("NaturalChum")));
	TestEqual(TEXT("自然聚鱼锚点保持配置值"), AnchorId, FName(TEXT("LakeAAnchor")));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
