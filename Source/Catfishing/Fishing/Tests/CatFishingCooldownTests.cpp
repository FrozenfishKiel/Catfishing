#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Fishing/Integration/CatFishingCommandComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingScoopCooldownGateTest,
	"Catfishing.Unit.Fishing.ScoopCooldown.ServerGateIsPerPlayerAndDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingScoopCooldownGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishingCooldownGate PlayerA;
	FCatFishingCooldownGate PlayerB;
	double Remaining = -1.0;

	TestTrue(TEXT("玩家 A 第一次挥网通过"), PlayerA.TryConsume(10.0, 3.0, Remaining));
	TestEqual(TEXT("首次通过无剩余冷却"), Remaining, 0.0);
	TestFalse(TEXT("玩家 A 冷却中再次挥网被拒绝"), PlayerA.TryConsume(11.0, 3.0, Remaining));
	TestEqual(TEXT("一秒后还剩两秒"), Remaining, 2.0);
	TestTrue(TEXT("玩家 B 有自己的独立冷却"), PlayerB.TryConsume(11.0, 3.0, Remaining));
	TestTrue(TEXT("玩家 A 到三秒边界可再次挥网"), PlayerA.TryConsume(13.0, 3.0, Remaining));

	PlayerA.Reset();
	TestTrue(TEXT("旅行重置后新世界时间可立即使用"), PlayerA.TryConsume(0.0, 3.0, Remaining));
	TestFalse(TEXT("非法时长不会被当成有效冷却"), PlayerB.TryConsume(20.0, 0.0, Remaining));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
