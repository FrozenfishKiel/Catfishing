#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "UI/CatSurvivalWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSurvivalWidgetViewStateContractTest,
	"Catfishing.Unit.UI.SurvivalWidget.ViewStateCarriesHudFactsWithoutGameplayObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：验证 SurvivalWidget 的公开输入是纯只读 DTO；命令行测试不创建假 LocalPlayer Widget，也不直接观察私有 WidgetTree。
bool FCatSurvivalWidgetViewStateContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("SurvivalWidget 是 UUserWidget 派生 View"), UCatSurvivalWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));

	FCatSurvivalViewState ViewState;
	ViewState.Poison = 1.5f;
	ViewState.FishingStrength = 4.0f;
	ViewState.FightStamina = 8.0f;
	ViewState.Condition.bWet = true;
	ViewState.Condition.bDowned = false;
	ViewState.Equipment.RodDefinitionId = TEXT("StarterRod");
	ViewState.Equipment.RodDurability = 9.0;
	ViewState.Run.Phase.DayIndex = 2;
	ViewState.Run.Phase.Phase = ECatRunPhase::DayActive;
	ViewState.Run.Environment.Weather = ECatEnvironmentWeather::Fog;
	ViewState.Run.Environment.TimeOfDay = ECatEnvironmentTimeOfDay::Dusk;
	ViewState.HelpSignal.Kind = ECatHelpSignalKind::ManualFishing;

	const FCatSurvivalViewState CopiedState = ViewState;
	TestEqual(TEXT("DTO 保留 Poison"), CopiedState.Poison, 1.5f);
	TestEqual(TEXT("DTO 保留 FishingStrength"), CopiedState.FishingStrength, 4.0f);
	TestEqual(TEXT("DTO 保留 FightStamina"), CopiedState.FightStamina, 8.0f);
	TestTrue(TEXT("DTO 保留 Wet 状态"), CopiedState.Condition.bWet);
	TestFalse(TEXT("DTO 保留 Downed 状态"), CopiedState.Condition.bDowned);
	TestEqual(TEXT("DTO 保留装备定义"), CopiedState.Equipment.RodDefinitionId, FName(TEXT("StarterRod")));
	TestEqual(TEXT("DTO 保留局内天数"), CopiedState.Run.Phase.DayIndex, 2);
	TestEqual(TEXT("DTO 保留环境天气"), CopiedState.Run.Environment.Weather, ECatEnvironmentWeather::Fog);
	TestEqual(TEXT("DTO 保留求助类型"), CopiedState.HelpSignal.Kind, ECatHelpSignalKind::ManualFishing);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
