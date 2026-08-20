#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Input/CatInputSettings.h"
#include "Misc/ScopeExit.h"

#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/CatLocalPlayerUISubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemTypeContractTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLocalPlayerUISubsystemNoViewportLifecycleTest,
	"Catfishing.Unit.UI.LocalPlayerUISubsystem.NoViewportLifecycleFailsClosed",
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

// 测试流程：构造无 viewport、无 GameInstance 的 LocalPlayer，让引擎初始化真实 LocalPlayerSubsystem 集合；期间全局输入
// 配置先关闭，避免命令行环境把输入资产装配误当作 UI 生命周期条件。
bool FCatLocalPlayerUISubsystemNoViewportLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("LocalPlayer UI 生命周期测试存在引擎 Outer"), GEngine);
	if (!GEngine)
	{
		return false;
	}

	UCatInputSettings* InputSettings = GetMutableDefault<UCatInputSettings>();
	TestNotNull(TEXT("可取得输入配置默认对象"), InputSettings);
	if (!InputSettings)
	{
		return false;
	}
	const bool bSavedEnableGlobalInputContexts = InputSettings->bEnableGlobalInputContexts;
	const TArray<FCatInputMappingContextConfig> SavedMappingContexts = InputSettings->MappingContexts;
	ON_SCOPE_EXIT
	{
		InputSettings->bEnableGlobalInputContexts = bSavedEnableGlobalInputContexts;
		InputSettings->MappingContexts = SavedMappingContexts;
	};
	InputSettings->bEnableGlobalInputContexts = false;
	InputSettings->MappingContexts.Reset();

	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	TestNotNull(TEXT("可创建无视口测试 LocalPlayer"), LocalPlayer);
	if (!LocalPlayer)
	{
		return false;
	}
	LocalPlayer->PlayerAdded(nullptr, 0);
	ON_SCOPE_EXIT
	{
		LocalPlayer->PlayerRemoved();
	};

	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>();
	TestNotNull(TEXT("UI 子系统在无视口 LocalPlayer 生命周期中可 fail-closed 初始化"), UISubsystem);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
