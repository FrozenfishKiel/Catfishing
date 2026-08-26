#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/CatLakeReachWidget.h"
#include "UI/CatUISettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUISettingsLakeReachWidgetClassTest,
	"Catfishing.Unit.UI.Settings.LakeReachUsesConfiguredWidgetClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：先验证正式 UIReach 默认启用但必须指向 WBP 软类；再显式开关同一 gate，证明装配策略由配置类承担且不会回退到原生白盒类。
bool FCatUISettingsLakeReachWidgetClassTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatUISettings* Settings = NewObject<UCatUISettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 UI Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("正式 UIReach 默认允许装配，缺 WBP 时由调用方 fail-closed"), Settings->IsLakeReachViewEnabled());
	TestEqual(TEXT("默认 LakeReach 前端指向正式 WBP 软类"),
		Settings->LakeReachWidgetClass.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/UI/WBP_CatLakeReach.WBP_CatLakeReach_C")));
	const TSubclassOf<UCatLakeReachWidget> LoadedClass = Settings->LoadLakeReachWidgetClass();
	if (TestNotNull(TEXT("正式 LakeReach WBP 类可加载"), LoadedClass.Get()))
	{
		TestTrue(TEXT("正式 WBP 继承 LakeReach View 基类"),
			LoadedClass->IsChildOf(UCatLakeReachWidget::StaticClass()));
		TestFalse(TEXT("正式配置不把原生 View 基类当玩家前端"),
			LoadedClass.Get() == UCatLakeReachWidget::StaticClass());
	}
	Settings->bEnableLakeReachView = false;
	TestFalse(TEXT("显式关闭后不装配 LakeReach MVC"), Settings->IsLakeReachViewEnabled());
	Settings->bEnableLakeReachView = true;
	TestTrue(TEXT("重新开启后仍走同一正式 WBP 配置"), Settings->IsLakeReachViewEnabled());
	TestEqual(TEXT("菜单输入 Action 指向正式资产"),
		Settings->LakeMenuToggleAction.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Input/InputAction/IA_LakeMenu.IA_LakeMenu")));
	TestEqual(TEXT("菜单输入 IMC 指向项目既有 InputContext"),
		Settings->LakeMenuInputMappingContext.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext")));
	TestNotNull(TEXT("菜单输入 Action 资产可加载"), Settings->LoadLakeMenuToggleAction());
	TestNotNull(TEXT("菜单输入 IMC 资产可加载"), Settings->LoadLakeMenuInputMappingContext());
	TestEqual(TEXT("菜单键名来自 IMC 映射"), Settings->ResolveLakeMenuToggleKeyName(), FName(TEXT("Tab")));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
