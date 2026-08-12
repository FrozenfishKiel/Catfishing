#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemTypeContractTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只验证 UI 协调器的归属 seam 是 LocalPlayerSubsystem；真实 Controller/Pawn 换绑需要 LocalPlayer 生命周期测试，不在这里伪造。
bool FCatLocalPlayerUISubsystemTypeContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UClass* SubsystemClass = UCatLocalPlayerUISubsystem::StaticClass();
	TestNotNull(TEXT("LocalPlayer UI 子系统类型可反射"), SubsystemClass);
	TestTrue(TEXT("UI 协调器挂在 LocalPlayerSubsystem seam 上"),
		SubsystemClass && SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()));
	TestFalse(TEXT("UI 协调器不是 GameInstance 全局单例"),
		SubsystemClass && SubsystemClass->IsChildOf(UGameInstanceSubsystem::StaticClass()));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
